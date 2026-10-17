#include "seqw/Sampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace seqw {

using namespace mangle;

namespace {
constexpr float kPiF = 3.14159265358979f;
inline float finiteOr0(float x) noexcept { return std::isfinite(x) ? x : 0.f; }

inline void readLin(const SampleData& d, double pos, float& l, float& r) noexcept
{
    const int64_t last = d.frames() - 1;
    if (last < 0) { l = r = 0.f; return; }
    int64_t i0 = static_cast<int64_t>(std::floor(pos));
    const float f = static_cast<float>(pos - static_cast<double>(i0));
    int64_t i1 = i0 + 1;
    i0 = std::clamp<int64_t>(i0, 0, last);
    i1 = std::clamp<int64_t>(i1, 0, last);
    l = d.left[static_cast<size_t>(i0)] + (d.left[static_cast<size_t>(i1)] - d.left[static_cast<size_t>(i0)]) * f;
    r = d.right[static_cast<size_t>(i0)] + (d.right[static_cast<size_t>(i1)] - d.right[static_cast<size_t>(i0)]) * f;
}

// normalised fx value -> musical value
inline float semisOf(float n) { return (std::clamp(n, 0.f, 1.f) - 0.5f) * 24.f; }            // -12..12
inline float formantOf(float n) { return (std::clamp(n, 0.f, 1.f) - 0.5f) * 2.f; }           // -1..1
inline float cutoffOf(float n) { return 20.f * std::pow(1000.f, std::clamp(n, 0.f, 1.f)); } // 20..20000 Hz
} // namespace

double stepBeats(int rate)
{
    static const double t[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 6.0 };
    return t[std::clamp(rate, 0, SQ_RATE_COUNT - 1)];
}

SqState defaultState()
{
    SqState s;
    std::memset(&s, 0, sizeof(s));
    for (int b = 0; b < SQ_BANKS; ++b)
        for (int i = 0; i < SQ_STEPS; ++i)
        {
            s.steps[b][i].velocity = 1.f;
            s.steps[b][i].prob = 1.f;
            s.steps[b][i].slice = i;
        }
    s.length = SQ_STEPS;
    s.playMode = SQ_MODE_FORWARD;
    s.rate = SQ_RATE_16;
    s.probability = 1.f;
    s.bpm = 120.f;
    s.loop = 1;
    s.volume = 0.8f;
    s.fx[SQ_FX_PITCH][0] = 0.5f; s.fx[SQ_FX_PITCH][1] = 0.5f;
    s.fx[SQ_FX_GRAIN][0] = 0.f;  s.fx[SQ_FX_GRAIN][1] = 0.3f;
    s.fx[SQ_FX_REPEAT][0] = 0.f; s.fx[SQ_FX_REPEAT][1] = 0.5f;
    s.fx[SQ_FX_FILTER][0] = 1.f; s.fx[SQ_FX_FILTER][1] = 0.15f;
    s.fx[SQ_FX_SPACE][0] = 0.f;  s.fx[SQ_FX_SPACE][1] = 0.5f;
    s.fx[SQ_FX_DRIVE][0] = 0.f;  s.fx[SQ_FX_DRIVE][1] = 1.f;
    s.trimStart = 0.f;
    s.trimEnd = 1.f;
    s.numSlices = 0;
    s.sliceEdge[0] = 0.f;
    s.sliceEdge[1] = 1.f;
    return s;
}

int equalSlices(float a, float b, int count, float* edges)
{
    count = std::clamp(count, 1, SQ_MAX_SLICES);
    a = std::clamp(a, 0.f, 1.f);
    b = std::clamp(b, a + 0.001f, 1.f);
    for (int i = 0; i <= count; ++i) edges[i] = a + (b - a) * static_cast<float>(i) / static_cast<float>(count);
    return count;
}

int autoSlice(const SampleData& d, float sensitivity, float trimStart, float trimEnd, float* edges, int maxSlices)
{
    maxSlices = std::clamp(maxSlices, 1, SQ_MAX_SLICES);
    const int64_t total = d.frames();
    if (total < 2048) return equalSlices(trimStart, trimEnd, 1, edges);
    const int64_t f0 = static_cast<int64_t>(std::clamp(trimStart, 0.f, 1.f) * static_cast<float>(total));
    const int64_t f1 = std::max<int64_t>(f0 + 1024, static_cast<int64_t>(std::clamp(trimEnd, 0.f, 1.f) * static_cast<float>(total)));
    const int64_t len = std::min(f1, total) - f0;
    const size_t hop = std::max<size_t>(16, static_cast<size_t>(d.sampleRate * 0.004));   // 4 ms
    const size_t frames = static_cast<size_t>(len) / hop;
    if (frames < 16) return equalSlices(trimStart, trimEnd, 1, edges);

    // onset strength = rise of the log energy of the high-passed (first difference) signal
    std::vector<float> env(frames), on(frames, 0.f);
    for (size_t k = 0; k < frames; ++k)
    {
        double acc = 0.0;
        const size_t s0 = static_cast<size_t>(f0) + k * hop;
        for (size_t i = 1; i < hop; ++i)
        {
            const double m0 = 0.5 * (static_cast<double>(d.left[s0 + i]) + d.right[s0 + i]);
            const double m1 = 0.5 * (static_cast<double>(d.left[s0 + i - 1]) + d.right[s0 + i - 1]);
            acc += (m0 - m1) * (m0 - m1);
        }
        env[k] = std::log1p(300.f * static_cast<float>(std::sqrt(acc / static_cast<double>(hop))));
    }
    // compare with the average of the previous ~40 ms so slow swells don't count
    const size_t back = 10;
    double sum = 0.0, sq = 0.0;
    for (size_t k = back; k < frames; ++k)
    {
        float ref = 0.f;
        for (size_t j = 1; j <= back; ++j) ref += env[k - j];
        ref /= static_cast<float>(back);
        on[k] = std::max(0.f, env[k] - ref);
        sum += on[k];
        sq += static_cast<double>(on[k]) * on[k];
    }
    const double mean = sum / static_cast<double>(frames);
    const double sd = std::sqrt(std::max(0.0, sq / static_cast<double>(frames) - mean * mean));
    const float sens = std::clamp(sensitivity, 0.f, 1.f);
    const double thr = mean + sd * (2.6 - 2.1 * static_cast<double>(sens));      // sens 1: mean + 0.5 sd
    const size_t minGap = std::max<size_t>(2, static_cast<size_t>(0.09 * d.sampleRate / static_cast<double>(hop)));

    struct Peak { size_t k; float v; };
    std::vector<Peak> peaks;
    for (size_t k = back + 1; k + 1 < frames; ++k)
        if (on[k] > thr && on[k] >= on[k - 1] && on[k] > on[k + 1]) peaks.push_back({ k, on[k] });
    // enforce the minimum gap, keeping the stronger of two close peaks
    std::vector<Peak> kept;
    for (const auto& p : peaks)
    {
        if (!kept.empty() && p.k - kept.back().k < minGap)
        {
            if (p.v > kept.back().v) kept.back() = p;
        }
        else kept.push_back(p);
    }
    // the first onset may sit right at the start: drop those (the region start is already an edge)
    kept.erase(std::remove_if(kept.begin(), kept.end(), [&](const Peak& p) { return p.k < minGap / 2 + 1; }), kept.end());
    if (static_cast<int>(kept.size()) > maxSlices - 1)
    {
        std::sort(kept.begin(), kept.end(), [](const Peak& a, const Peak& b) { return a.v > b.v; });
        kept.resize(static_cast<size_t>(maxSlices - 1));
        std::sort(kept.begin(), kept.end(), [](const Peak& a, const Peak& b) { return a.k < b.k; });
    }
    if (kept.size() < 2) return equalSlices(trimStart, trimEnd, std::max(4, static_cast<int>(8 * (0.5f + sens) / 1.f)) > maxSlices ? maxSlices : std::max(4, static_cast<int>(8 * (0.5f + sens))), edges);

    const float ft = static_cast<float>(total);
    int count = 0;
    edges[count++] = static_cast<float>(f0) / ft;
    for (const auto& p : kept)
    {
        // the onset is detected a little after it starts: step back to the previous quiet point / zero crossing
        int64_t pos = f0 + static_cast<int64_t>(p.k * hop) - static_cast<int64_t>(hop);
        const int64_t lo = std::max<int64_t>(f0, pos - static_cast<int64_t>(hop) * 3);
        for (int64_t i = pos; i > lo; --i)
        {
            const float a = 0.5f * (d.left[static_cast<size_t>(i)] + d.right[static_cast<size_t>(i)]);
            const float b = 0.5f * (d.left[static_cast<size_t>(i - 1)] + d.right[static_cast<size_t>(i - 1)]);
            if ((a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f)) { pos = i; break; }
        }
        edges[count++] = static_cast<float>(pos) / ft;
    }
    edges[count++] = static_cast<float>(std::min(f1, total)) / ft;
    return count - 1;
}

std::shared_ptr<SampleData> makeDemoBreak(double sr)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = sr;
    const double bpm = 120.0;
    const double stepSec = 60.0 / bpm / 4.0;             // 16th
    const int steps = 32;                                 // two bars
    const size_t n = static_cast<size_t>(std::llround(steps * stepSec * sr));
    d->left.assign(n, 0.f);
    d->right.assign(n, 0.f);
    Rng rng;
    rng.s = 0x1234567u;
    auto add = [&](size_t at, size_t len, const std::function<float(double)>& f, float gl, float gr) {
        for (size_t i = 0; i < len && at + i < n; ++i)
        {
            const float v = f(static_cast<double>(i) / sr);
            d->left[at + i] += v * gl;
            d->right[at + i] += v * gr;
        }
    };
    // kick pattern, snare, hats, bass stabs
    const int kicks[] = { 0, 6, 10, 16, 22, 26, 28 };
    const int snares[] = { 8, 24, 30 };
    for (int k : kicks)
        add(static_cast<size_t>(k * stepSec * sr), static_cast<size_t>(0.28 * sr), [](double t) {
            return 0.9f * static_cast<float>(std::sin(2.0 * M_PI * (46.0 * t + 95.0 * (1.0 - std::exp(-t * 30.0)) / 30.0)) * std::exp(-t * 15.0));
        }, 1.f, 1.f);
    for (int s : snares)
        add(static_cast<size_t>(s * stepSec * sr), static_cast<size_t>(0.22 * sr), [&](double t) {
            return 0.55f * (rng.bi() * static_cast<float>(std::exp(-t * 20.0)) + 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 185.0 * t) * std::exp(-t * 28.0)));
        }, 1.f, 1.f);
    for (int h = 0; h < steps; ++h)
    {
        const bool open = h % 8 == 6;
        const float pan = (h % 2) ? 0.7f : 1.f;
        add(static_cast<size_t>(h * stepSec * sr), static_cast<size_t>((open ? 0.14 : 0.04) * sr), [&](double t) {
            return 0.2f * rng.bi() * static_cast<float>(std::exp(-t * (open ? 25.0 : 110.0)));
        }, pan, 1.f + 0.f * pan);
    }
    const double roots[] = { 55.0, 55.0, 65.41, 49.0 };
    for (int b = 0; b < 4; ++b)
    {
        const int at = b * 8 + 2;
        add(static_cast<size_t>(at * stepSec * sr), static_cast<size_t>(0.5 * sr), [&](double t) {
            return 0.32f * static_cast<float>((std::sin(2.0 * M_PI * roots[b] * t) + 0.35 * std::sin(2.0 * M_PI * roots[b] * 3.0 * t)) * std::exp(-t * 3.5));
        }, 1.f, 1.f);
    }
    float peak = 0.f;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::max(std::abs(d->left[i]), std::abs(d->right[i])));
    if (peak > 0.f) for (size_t i = 0; i < n; ++i) { d->left[i] *= 0.9f / peak; d->right[i] *= 0.9f / peak; }
    return d;
}

// ---- Sampler -----------------------------------------------------------------------------------------------
void Sampler::prepare(double sampleRate, int maxBlock)
{
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    maxBlock_ = std::max(1, maxBlock);
    granular_.prepare(sr_, 0x51ed270bu);
    stretcher_.prepare(sr_, 0x2545f491u);
    grit_.prepare(sr_);
    filter_.prepare(sr_);
    repeat_.prepare(sr_);
    reverb_.prepare(sr_);
    for (auto& t : tmp_) t.assign(static_cast<size_t>(kSub), 0.f);
    live_ = staged_ = defaultState();
    prepared_ = true;
    reset();
}

void Sampler::reset() noexcept
{
    for (auto& v : voices_) v = Voice {};
    granular_.reset();
    stretcher_.reset();
    grit_.reset();
    filter_.reset();
    repeat_.reset();
    reverb_.reset();
    playing_ = false;
    countdown_ = 0.0;
    stepCounter_ = 0;
    stepIdx_ = 0;
    pass_ = 0;
    newest_ = -1;
    curStep_ = SqStep {};
    curStep_.velocity = 1.f;
    anchor_ = 0.0;
    anchorInc_ = 1.0;
    sGrain_.set(0.f); sScatter_.set(0.3f); sDrive_.set(0.f); sTone_.set(1.f); sSpace_.set(0.f); sSize_.set(0.5f);
    sCut_.set(20000.f); sReso_.set(0.15f); sFormant_.set(0.f); sVol_.set(live_.volume); sPan_.set(0.f); sRepMix_.set(0.f);
    formantLp_[0] = formantLp_[1] = toneLp_[0] = toneLp_[1] = 0.f;
    padR_.store(padW_.load());
    statPlaying_ = 0; statPlayhead_ = -1.f; statLevel_ = 0.f; statVoices_ = 0; statPass_ = 0;
}

void Sampler::setSeed(uint32_t seed) noexcept { rng_.s = seed ? seed : 1u; }

void Sampler::setState(const SqState& s)
{
    while (lock_.test_and_set(std::memory_order_acquire)) {}
    staged_ = s;
    lock_.clear(std::memory_order_release);
    dirty_.store(true);
}

void Sampler::purgeRetired()
{
    const SampleData* busy = inUse_.load();
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [busy](const std::shared_ptr<const SampleData>& p) { return p.get() != busy; }),
                   retired_.end());
}

void Sampler::collectGarbage() { purgeRetired(); }

void Sampler::setSample(std::shared_ptr<const SampleData> d)
{
    auto old = std::move(current_);
    current_ = std::move(d);
    active_.store(current_.get());
    if (old) retired_.push_back(std::move(old));
    purgeRetired();
}

void Sampler::triggerPad(int slice, float velocity) noexcept
{
    const unsigned w = padW_.load(std::memory_order_relaxed);
    if (w - padR_.load(std::memory_order_acquire) >= pads_.size()) return;   // full: drop
    pads_[w % pads_.size()] = { slice, velocity };
    padW_.store(w + 1, std::memory_order_release);
}

SqStatus Sampler::status() const noexcept
{
    SqStatus s;
    s.playing = statPlaying_.load();
    s.step = statStep_.load();
    s.playhead = statPlayhead_.load();
    s.level = statLevel_.load();
    s.voices = statVoices_.load();
    s.pass = statPass_.load();
    return s;
}

void Sampler::applyPending() noexcept
{
    if (dirty_.load() && !lock_.test_and_set(std::memory_order_acquire))
    {
        live_ = staged_;
        lock_.clear(std::memory_order_release);
        dirty_.store(false);
        live_.length = std::clamp(live_.length, 1, SQ_STEPS);
        live_.bank = std::clamp(live_.bank, 0, SQ_BANKS - 1);
        live_.bpm = std::clamp(live_.bpm, 30.f, 300.f);
    }
}

bool Sampler::sliceRange(const SampleData* d, int slice, double& a, double& b) const noexcept
{
    if (d == nullptr || d->empty()) return false;
    const double n = static_cast<double>(d->frames());
    if (live_.numSlices <= 0)
    {
        a = std::clamp(static_cast<double>(live_.trimStart), 0.0, 1.0) * n;
        b = std::clamp(static_cast<double>(live_.trimEnd), 0.0, 1.0) * n;
    }
    else
    {
        const int i = ((slice % live_.numSlices) + live_.numSlices) % live_.numSlices;
        a = std::clamp(static_cast<double>(live_.sliceEdge[i]), 0.0, 1.0) * n;
        b = std::clamp(static_cast<double>(live_.sliceEdge[i + 1]), 0.0, 1.0) * n;
    }
    if (b - a < 32.0) b = std::min(n, a + 32.0);
    return b - a >= 8.0;
}

void Sampler::trigger(const SampleData* d, int slice, float vel, const SqStep* step, bool fromSeq) noexcept
{
    double a, b;
    if (!sliceRange(d, slice, a, b)) return;
    if (fromSeq)   // the sequencer is monophonic: the previous step fades out
        for (auto& v : voices_) if (v.active && v.seq) v.releasing = true;
    int slot = -1;
    for (int i = 0; i < kVoices; ++i) if (!voices_[static_cast<size_t>(i)].active) { slot = i; break; }
    if (slot < 0)
    {
        float lowest = 9.f;
        for (int i = 0; i < kVoices; ++i)
        {
            const float e = voices_[static_cast<size_t>(i)].releasing ? voices_[static_cast<size_t>(i)].env - 1.f : voices_[static_cast<size_t>(i)].env;
            if (e < lowest) { lowest = e; slot = i; }
        }
    }
    Voice& v = voices_[static_cast<size_t>(slot)];
    v = Voice {};
    v.active = true;
    v.seq = fromSeq;
    v.start = a; v.end = b;
    v.vel = std::clamp(vel, 0.f, 1.f);
    v.reverse = step != nullptr && step->reverse;
    v.lofi = step != nullptr && step->lofi;
    v.pos = v.reverse ? b - 1.0 : a;
    if (step != nullptr && step->stretch && fromSeq)
    {
        const double frames = stepFrames();
        v.stretch = true;
        v.reverse = false;
        v.pos = a;
        v.stretchInc = (b - a) / std::max(1.0, frames);
        v.gateLeft = static_cast<int>(frames);
        for (auto& o : voices_) if (&o != &v && o.stretch) o.releasing = true;   // one stretcher voice at a time
    }
    else v.gateLeft = fromSeq ? static_cast<int>(stepFrames()) : -1;
    newest_ = slot;
}

double Sampler::stepFrames() const noexcept
{
    const double base = stepBeats(live_.rate) * 60.0 / static_cast<double>(live_.bpm) * sr_;
    const double sw = std::clamp(static_cast<double>(live_.swing), 0.0, 1.0) * 0.5;
    return base * ((stepCounter_ & 1) == 0 ? 1.0 + sw : 1.0 - sw);
}

float Sampler::eff(int fx, int k) const noexcept
{
    return curStep_.hasLock[fx] ? curStep_.lock[fx][k] : live_.fx[fx][k];
}

void Sampler::startStep(const SampleData* d) noexcept
{
    const int len = live_.length;
    int period = len;
    if (live_.playMode == SQ_MODE_PINGPONG) period = std::max(1, 2 * (len - 1));
    const long long cnt = stepCounter_;
    const int pass = static_cast<int>(cnt / period);
    if (!live_.loop && pass >= 1) { playing_ = false; playReq_.store(-1); for (auto& v : voices_) if (v.seq) v.releasing = true; return; }
    pass_ = pass;
    int idx = 0;
    switch (live_.playMode)
    {
        case SQ_MODE_REVERSE:  idx = len - 1 - static_cast<int>(cnt % len); break;
        case SQ_MODE_PINGPONG: { const int p = static_cast<int>(cnt % period); idx = p < len ? p : period - p; break; }
        case SQ_MODE_RANDOM:   idx = std::min(len - 1, static_cast<int>(uni() * static_cast<float>(len))); break;
        default:               idx = static_cast<int>(cnt % len); break;
    }
    idx = std::clamp(idx, 0, SQ_STEPS - 1);
    stepIdx_ = idx;
    lastStep_ = idx;
    statStep_.store(idx);
    const SqStep& st = live_.steps[live_.bank][idx];
    curStep_ = st;
    if (!st.active) { curStep_ = SqStep {}; curStep_.velocity = 1.f; return; }
    // conditions
    bool ok = true;
    switch (st.cond)
    {
        case SQ_COND_EVERY2:    ok = pass % 2 == 0; break;
        case SQ_COND_EVERY4:    ok = pass % 4 == 0; break;
        case SQ_COND_FIRST:     ok = pass == 0; break;
        case SQ_COND_NOT_FIRST: ok = pass > 0; break;
        case SQ_COND_HALF:      ok = uni() < 0.5f; break;
        default: break;
    }
    if (st.velCond == SQ_VEL_GT50) ok = ok && st.velocity > 0.5f;
    else if (st.velCond == SQ_VEL_LT50) ok = ok && st.velocity < 0.5f;
    if (ok)
    {
        const float p = std::clamp(live_.probability, 0.f, 1.f) * std::clamp(st.prob, 0.f, 1.f);
        if (p < 0.9999f && uni() >= p) ok = false;
    }
    if (!ok) { const SqStep keep = curStep_; curStep_ = SqStep {}; curStep_.velocity = 1.f; (void)keep; return; }
    trigger(d, st.slice, st.velocity, &st, true);
}

void Sampler::renderSegment(const SampleData* d, float* outL, float* outR, int n) noexcept
{
    const double updRate = sr_ / kSub;
    float* srcL = tmp_[0].data(); float* srcR = tmp_[1].data();
    float* gL = tmp_[2].data();   float* gR = tmp_[3].data();
    std::memset(srcL, 0, sizeof(float) * static_cast<size_t>(n));
    std::memset(srcR, 0, sizeof(float) * static_cast<size_t>(n));
    const bool have = d != nullptr && !d->empty();
    const double natural = have ? d->sampleRate / sr_ : 1.0;
    const float semis = semisOf(eff(SQ_FX_PITCH, 0));
    const double ratio = natural * std::pow(2.0, static_cast<double>(semis) / 12.0);
    const float atk = 1.f / static_cast<float>(0.002 * sr_), rel = 1.f / static_cast<float>(0.008 * sr_);
    const int lofiN = std::max(2, static_cast<int>(sr_ / 11025.0));

    int alive = 0;
    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        Voice& v = voices_[vi];
        if (!v.active) continue;
        if (!have) { v.active = false; continue; }
        if (v.stretch)
        {
            GrainEngine::Params gp;
            gp.sizeSamples = 2048.0 * sr_ / 44100.0;
            gp.overlap = 3.f;
            gp.pitchRatio = ratio;
            gp.align = true;
            const Region reg { v.start, std::max(1.0, v.end - v.start) };
            stretcher_.process(*d, reg, v.pos, v.stretchInc, gp, gL, gR, n, true);
            for (int i = 0; i < n; ++i)
            {
                if (!v.releasing && v.gateLeft >= 0 && --v.gateLeft < 0) v.releasing = true;
                if (v.releasing) v.env -= rel; else v.env = std::min(1.f, v.env + atk);
                if (v.env <= 0.f || v.pos >= v.end) { v.active = false; break; }
                srcL[i] += gL[i] * v.env * v.vel;
                srcR[i] += gR[i] * v.env * v.vel;
                v.pos += v.stretchInc;
            }
        }
        else
        {
            const double inc = v.reverse ? -ratio : ratio;
            for (int i = 0; i < n; ++i)
            {
                if (!v.releasing && v.gateLeft >= 0 && --v.gateLeft < 0) v.releasing = true;
                if (v.releasing) v.env -= rel; else v.env = std::min(1.f, v.env + atk);
                if (v.env <= 0.f || v.pos >= v.end || v.pos < v.start - 1.0) { v.active = false; break; }
                float l, r;
                readLin(*d, v.pos, l, r);
                if (v.lofi)
                {
                    if (v.holdCount-- <= 0) { v.holdCount = lofiN - 1; v.holdL = std::round(l * 48.f) / 48.f; v.holdR = std::round(r * 48.f) / 48.f; }
                    l = v.holdL; r = v.holdR;
                }
                srcL[i] += l * v.env * v.vel;
                srcR[i] += r * v.env * v.vel;
                v.pos += inc;
            }
        }
        if (v.active) { ++alive; }
    }
    // newest active voice defines the anchor for the grain effect and the playhead
    int newest = -1;
    if (newest_ >= 0 && voices_[static_cast<size_t>(newest_)].active) newest = newest_;
    if (newest < 0)
        for (int i = 0; i < kVoices; ++i) if (voices_[static_cast<size_t>(i)].active) { newest = i; break; }
    if (newest >= 0)
    {
        const Voice& nv = voices_[static_cast<size_t>(newest)];
        anchor_ = nv.pos;
        anchorInc_ = nv.stretch ? nv.stretchInc : (nv.reverse ? -ratio : ratio);
    }

    // ---- smoothed effect parameters (one update per segment) ------------------------------------------
    sGrain_.target = std::clamp(eff(SQ_FX_GRAIN, 0), 0.f, 1.f);
    sScatter_.target = std::clamp(eff(SQ_FX_GRAIN, 1), 0.f, 1.f);
    const float gmix = sGrain_.step(smoothCoeff(0.02, updRate));
    const float scatter = sScatter_.step(smoothCoeff(0.02, updRate));
    if (have && (gmix > 0.001f || sGrain_.target > 0.001f) && (alive > 0 || granular_.activeGrains() > 0))
    {
        GrainEngine::Params gp;
        gp.sizeSamples = (0.05 + 0.06 * (1.0 - static_cast<double>(scatter))) * d->sampleRate;
        gp.sizeSamples *= sr_ / d->sampleRate;
        gp.overlap = 4.f;
        gp.pitchRatio = ratio;
        gp.scatterFrames = static_cast<double>(scatter) * 0.25 * d->sampleRate;
        gp.pitchRandomSemis = scatter * 2.f;
        gp.spread = scatter;
        const Region reg = regionFor(*d, live_.trimStart, live_.trimEnd);
        granular_.process(*d, reg, anchor_, anchorInc_, gp, gL, gR, n, true);
        const float gg = alive > 0 ? 1.f : 0.f;
        for (int i = 0; i < n; ++i)
        {
            srcL[i] = srcL[i] * (1.f - gmix * 0.85f) + gL[i] * gmix * gg;
            srcR[i] = srcR[i] * (1.f - gmix * 0.85f) + gR[i] * gmix * gg;
        }
    }

    sDrive_.target = std::clamp(eff(SQ_FX_DRIVE, 0), 0.f, 1.f);
    sTone_.target = std::clamp(eff(SQ_FX_DRIVE, 1), 0.f, 1.f);
    const float drive = sDrive_.step(smoothCoeff(0.02, updRate));
    const float tone = sTone_.step(smoothCoeff(0.02, updRate));
    sCut_.target = cutoffOf(eff(SQ_FX_FILTER, 0));
    sReso_.target = std::clamp(eff(SQ_FX_FILTER, 1), 0.f, 1.f);
    const float cut = sCut_.step(smoothCoeff(0.015, updRate));
    const float reso = sReso_.step(smoothCoeff(0.015, updRate));
    const bool filterNeeded = cut < 19000.f;
    if (filterNeeded) filter_.set(cut, reso, false);
    sFormant_.target = formantOf(eff(SQ_FX_PITCH, 1));
    const float formant = sFormant_.step(smoothCoeff(0.03, updRate));
    const float formCoef = 1.f - std::exp(-2.f * kPiF * 1800.f / static_cast<float>(sr_));
    const float toneHz = 1200.f * std::pow(16.f, tone);   // 1.2k .. 19k
    const float toneCoef = 1.f - std::exp(-2.f * kPiF * std::min(toneHz, static_cast<float>(sr_) * 0.45f) / static_cast<float>(sr_));

    const float repAmt = std::clamp(eff(SQ_FX_REPEAT, 0), 0.f, 1.f);
    static const double divs[] = { 1.0, 0.5, 0.25, 0.125 };
    const int divIdx = std::clamp(static_cast<int>(std::lround(std::clamp(eff(SQ_FX_REPEAT, 1), 0.f, 1.f) * 3.f)), 0, 3);
    const double repBeats = repAmt > 0.03f ? divs[divIdx] : 0.0;
    sRepMix_.target = std::min(1.f, repAmt * 1.4f);
    const float repMix = sRepMix_.step(smoothCoeff(0.01, updRate));

    sSpace_.target = std::clamp(eff(SQ_FX_SPACE, 0), 0.f, 1.f);
    sSize_.target = std::clamp(eff(SQ_FX_SPACE, 1), 0.f, 1.f);
    const float space = sSpace_.step(smoothCoeff(0.03, updRate));
    const float size = sSize_.step(smoothCoeff(0.05, updRate));
    sVol_.target = std::clamp(live_.volume, 0.f, 1.5f);
    sPan_.target = std::clamp(live_.pan, -1.f, 1.f);
    const float vol = sVol_.step(smoothCoeff(0.02, updRate));
    const float pan = sPan_.step(smoothCoeff(0.02, updRate));
    const float gl = std::min(1.f, 1.f - pan) * vol, gr = std::min(1.f, 1.f + pan) * vol;
    const double bpm = static_cast<double>(live_.bpm);

    float peak = 0.f;
    for (int i = 0; i < n; ++i)
    {
        float l = srcL[i], r = srcR[i];
        if (drive > 0.f)
        {
            grit_.process(l, r, drive);
            toneLp_[0] += (l - toneLp_[0]) * toneCoef;
            toneLp_[1] += (r - toneLp_[1]) * toneCoef;
            l = toneLp_[0]; r = toneLp_[1];
        }
        if (filterNeeded) filter_.process(l, r);
        formantLp_[0] += (l - formantLp_[0]) * formCoef;
        formantLp_[1] += (r - formantLp_[1]) * formCoef;
        l += formant * (l - formantLp_[0]);
        r += formant * (r - formantLp_[1]);
        repeat_.process(l, r, repBeats, bpm, 1.f, repMix);
        if (space > 0.001f)
        {
            float wl, wr;
            reverb_.process(l, r, wl, wr, 0.55f + 0.44f * size, 0.45f);
            l += wl * space * 1.6f;
            r += wr * space * 1.6f;
        }
        l = finiteOr0(std::tanh(l * gl));
        r = finiteOr0(std::tanh(r * gr));
        outL[i] = l;
        outR[i] = r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    statLevel_.store(peak);
    statVoices_.store(alive);
    statPlayhead_.store(newest >= 0 && have ? static_cast<float>(std::clamp(anchor_ / static_cast<double>(d->frames()), 0.0, 1.0)) : -1.f);
}

void Sampler::render(float* outL, float* outR, int n) noexcept
{
    if (!prepared_ || n <= 0) return;
    // hazard pointer
    const SampleData* d = active_.load();
    inUse_.store(d);
    if (active_.load() != d) { d = active_.load(); inUse_.store(d); }

    applyPending();
    const int req = playReq_.exchange(-1);
    if (req == 1) { playing_ = true; countdown_ = 0.0; stepCounter_ = 0; pass_ = 0; repeat_.reset(); }
    else if (req == 0)
    {
        playing_ = false;
        for (auto& v : voices_) if (v.seq) v.releasing = true;
        curStep_ = SqStep {}; curStep_.velocity = 1.f;
    }
    // performance pads
    for (unsigned r = padR_.load(std::memory_order_relaxed); r != padW_.load(std::memory_order_acquire); ++r)
    {
        const Pad p = pads_[r % pads_.size()];
        trigger(d, p.slice, p.vel, nullptr, false);
        padR_.store(r + 1, std::memory_order_release);
    }

    int pos = 0;
    while (pos < n)
    {
        if (playing_ && countdown_ <= 0.0)
        {
            startStep(d);
            if (playing_)
            {
                countdown_ += stepFrames();
                ++stepCounter_;
            }
        }
        int seg = std::min(n - pos, kSub);
        if (playing_) seg = std::min(seg, std::max(1, static_cast<int>(std::ceil(countdown_))));
        renderSegment(d, outL + pos, outR + pos, seg);
        if (playing_) countdown_ -= seg;
        pos += seg;
    }
    statPlaying_.store(playing_ ? 1 : 0);
    statPass_.store(pass_);
    inUse_.store(nullptr);
}

} // namespace seqw
