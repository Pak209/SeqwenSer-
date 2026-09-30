#include "mangle/Dsp.h"

#include <cstring>

namespace mangle {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPi = 6.283185307179586476925286766559;
inline double frac(double x) noexcept { return x - std::floor(x); }
inline float hash01(long long n) noexcept
{
    uint64_t x = static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ull + 0x1234567ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) * (1.f / 16777216.f);
}
} // namespace

// ---- SvFilter -------------------------------------------------------------------------------------
void SvFilter::set(float cutoffHz, float reso, bool highPass) noexcept
{
    const float fc = std::clamp(cutoffHz, 16.f, static_cast<float>(sr_) * 0.45f);
    const float r = std::clamp(reso, 0.f, 1.f);
    const float q = 0.55f + r * r * 12.f;
    g_ = std::tan(kPi * fc / static_cast<float>(sr_));
    k_ = 1.f / q;
    a1_ = 1.f / (1.f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
    hp_ = highPass;
}

void SvFilter::processSample(int ch, float& x) noexcept
{
    const float v3 = x - ic2_[ch];
    const float v1 = a1_ * ic1_[ch] + a2_ * v3;
    const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
    ic1_[ch] = 2.f * v1 - ic1_[ch];
    ic2_[ch] = 2.f * v2 - ic2_[ch];
    x = hp_ ? x - k_ * v1 - v2 : v2;
}

void SvFilter::process(float& l, float& r) noexcept
{
    processSample(0, l);
    processSample(1, r);
}

// ---- Grit ------------------------------------------------------------------------------------------
void Grit::prepare(double sampleRate) noexcept
{
    dcR_ = static_cast<float>(1.0 - kTwoPi * 20.0 / sampleRate);
    reset();
}

void Grit::process(float& l, float& r, float amount) noexcept
{
    if (amount <= 0.f) return;
    const float a = std::min(amount, 1.f);
    const float drive = 1.f + a * 24.f;
    const float bias = 0.12f * a;               // slight asymmetry: even harmonics
    const float comp = 1.f / std::tanh(drive * 0.5f + 0.2f);
    const float wet = std::min(1.f, a * 5.f);   // first 20 % of the knob blends the effect in
    float* ch[2] = { &l, &r };
    for (int c = 0; c < 2; ++c)
    {
        const float x = *ch[c];
        float y = (std::tanh(x * drive + bias) - std::tanh(bias)) * comp * 0.7f;
        const float out = dcR_ * dcY_[c] + y - dcX_[c];   // DC blocker
        dcX_[c] = y;
        dcY_[c] = out;
        *ch[c] = x + (out - x) * wet;
    }
}

// ---- Reverb ----------------------------------------------------------------------------------------
void Reverb::prepare(double sampleRate)
{
    static const int combT[kCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int apT[kAllpass] = { 556, 441, 341, 225 };
    const double sc = sampleRate / 44100.0;
    for (int i = 0; i < kCombs; ++i)
    {
        combL_[i].buf.assign(static_cast<size_t>(std::max(8.0, combT[i] * sc)), 0.f);
        combR_[i].buf.assign(static_cast<size_t>(std::max(8.0, (combT[i] + 23) * sc)), 0.f);
    }
    for (int i = 0; i < kAllpass; ++i)
    {
        apL_[i].buf.assign(static_cast<size_t>(std::max(8.0, apT[i] * sc)), 0.f);
        apR_[i].buf.assign(static_cast<size_t>(std::max(8.0, (apT[i] + 23) * sc)), 0.f);
    }
    reset();
}

void Reverb::reset() noexcept
{
    for (auto* v : { &combL_[0], &combR_[0] })
        for (int i = 0; i < kCombs; ++i) { std::fill(v[i].buf.begin(), v[i].buf.end(), 0.f); v[i].idx = 0; v[i].store = 0.f; }
    for (auto* v : { &apL_[0], &apR_[0] })
        for (int i = 0; i < kAllpass; ++i) { std::fill(v[i].buf.begin(), v[i].buf.end(), 0.f); v[i].idx = 0; }
}

void Reverb::process(float inL, float inR, float& outL, float& outR, float size, float damp) noexcept
{
    const float feedback = 0.7f + std::clamp(size, 0.f, 1.f) * 0.28f;
    const float d = std::clamp(damp, 0.f, 1.f) * 0.4f;
    const float in = (inL + inR) * 0.015f;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < kCombs; ++i)
    {
        l += combL_[i].process(in, feedback, d);
        r += combR_[i].process(in, feedback, d);
    }
    for (int i = 0; i < kAllpass; ++i)
    {
        l = apL_[i].process(l);
        r = apR_[i].process(r);
    }
    outL = l * 3.f;
    outR = r * 3.f;
}

// ---- LFO -------------------------------------------------------------------------------------------
float lfoValue(int shape, double ppq, double periodBeats) noexcept
{
    const double cyc = ppq / std::max(1e-6, periodBeats);
    const double p = frac(cyc);
    switch (static_cast<LfoShape>(std::clamp(shape, 0, static_cast<int>(LfoShape::Count) - 1)))
    {
        case LfoShape::Sine:     return static_cast<float>(std::sin(kTwoPi * p));
        case LfoShape::Triangle: return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
        case LfoShape::Saw:      return static_cast<float>(2.0 * p - 1.0);
        case LfoShape::Square:   return p < 0.5 ? 1.f : -1.f;
        case LfoShape::SampleHold:
        {
            const long long k = static_cast<long long>(std::floor(cyc * 8.0));   // 8 held values per cycle
            return hash01(k) * 2.f - 1.f;
        }
        case LfoShape::Smooth:
        {
            const long long k = static_cast<long long>(std::floor(cyc * 2.0));   // 2 random points per cycle
            const float a = hash01(k) * 2.f - 1.f, b = hash01(k + 1) * 2.f - 1.f;
            const float t = static_cast<float>(frac(cyc * 2.0));
            const float s = t * t * (3.f - 2.f * t);
            return a + (b - a) * s;
        }
        default: return 0.f;
    }
}

// ---- steps -----------------------------------------------------------------------------------------
StepPos stepAt(double ppq, double stepBeats, double swing, int length) noexcept
{
    StepPos sp;
    length = std::clamp(length, 1, kMaxSteps);
    const double pos = std::max(0.0, ppq) / std::max(1e-6, stepBeats);
    const double pair = std::floor(pos / 2.0);
    const double x = pos - 2.0 * pair;                       // 0..2
    const double b = 1.0 + 0.5 * std::clamp(swing, 0.0, 1.0);
    const int which = x < b ? 0 : 1;
    sp.phase = which == 0 ? x / b : (x - b) / (2.0 - b);
    sp.phase = std::clamp(sp.phase, 0.0, 1.0);
    sp.number = static_cast<long long>(pair) * 2 + which;
    sp.index = static_cast<int>(sp.number % length);
    return sp;
}

// ---- sample access ---------------------------------------------------------------------------------
Region regionFor(const SampleData& d, float startFrac, float endFrac) noexcept
{
    const double n = static_cast<double>(d.frames());
    double a = std::clamp(static_cast<double>(startFrac), 0.0, 1.0) * n;
    double b = std::clamp(static_cast<double>(endFrac), 0.0, 1.0) * n;
    if (b - a < 64.0) b = std::min(n, a + 64.0);
    if (b - a < 64.0) a = std::max(0.0, b - 64.0);
    return { a, std::max(1.0, b - a) };
}

void readWrapped(const SampleData& d, const Region& r, double pos, float& l, float& rr) noexcept
{
    double rel = std::fmod(pos - r.start, r.length);
    if (rel < 0.0) rel += r.length;
    const double abs0 = r.start + rel;
    const int64_t i0 = static_cast<int64_t>(abs0);
    const float f = static_cast<float>(abs0 - static_cast<double>(i0));
    int64_t i1 = i0 + 1;
    const int64_t end = static_cast<int64_t>(r.start + r.length);
    if (i1 >= end) i1 = static_cast<int64_t>(r.start);      // wrap inside the region
    const int64_t last = d.frames() - 1;
    const int64_t a = std::min(i0, last), b = std::min(i1, last);
    l = d.left[static_cast<size_t>(a)] + (d.left[static_cast<size_t>(b)] - d.left[static_cast<size_t>(a)]) * f;
    rr = d.right[static_cast<size_t>(a)] + (d.right[static_cast<size_t>(b)] - d.right[static_cast<size_t>(a)]) * f;
}

// ---- GrainEngine -----------------------------------------------------------------------------------
void GrainEngine::prepare(double, uint32_t seed)
{
    window_.resize(2049);
    for (size_t i = 0; i < window_.size(); ++i)
        window_[i] = 0.5f * (1.f - std::cos(2.f * kPi * static_cast<float>(i) / 2048.f));   // periodic Hann
    rng_.s = seed ? seed : 1u;
    reset();
}

void GrainEngine::reset() noexcept
{
    for (auto& g : grains_) g.active = false;
    countdown_ = 0.0;
    lastIdx_ = -1;
}

namespace {
inline float readNearestL(const SampleData& d, const Region& r, double pos) noexcept
{
    double rel = pos - r.start;
    rel -= std::floor(rel / r.length) * r.length;
    int64_t i = static_cast<int64_t>(r.start + rel);
    i = std::min<int64_t>(std::max<int64_t>(i, 0), d.frames() - 1);
    return d.left[static_cast<size_t>(i)];
}
} // namespace

// Position offset (in file frames, within +-range) at which a new grain read at `inc` best continues
// the previous grain (normalised cross-correlation over the next ~512 output samples).
double GrainEngine::bestOffset(const SampleData& d, const Region& r, double nominal, double inc, int range) const noexcept
{
    if (lastIdx_ < 0) return 0.0;
    const Grain& pg = grains_[static_cast<size_t>(lastIdx_)];
    if (!pg.active || pg.age + 64 >= pg.len) return 0.0;
    constexpr int kLen = 256, kStride = 2;
    float ref[kLen];
    double refE = 0.0;
    for (int j = 0; j < kLen; ++j)
    {
        ref[j] = readNearestL(d, r, pg.pos + (pg.age + j * kStride) * pg.inc);
        refE += static_cast<double>(ref[j]) * ref[j];
    }
    if (refE < 1e-9) return 0.0;
    double best = -2.0, bestO = 0.0;
    for (int o = -range; o <= range; o += 2)
    {
        double dot = 0.0, e = 1e-9;
        const double p0 = nominal + o;
        for (int j = 0; j < kLen; ++j)
        {
            const float c = readNearestL(d, r, p0 + j * kStride * inc);
            dot += static_cast<double>(ref[j]) * c;
            e += static_cast<double>(c) * c;
        }
        const double score = dot / std::sqrt(e);
        if (score > best) { best = score; bestO = o; }
    }
    return bestO;
}

int GrainEngine::activeGrains() const noexcept
{
    int n = 0;
    for (const auto& g : grains_) n += g.active ? 1 : 0;
    return n;
}

void GrainEngine::process(const SampleData& d, const Region& r, double anchor, double anchorInc, const Params& p,
                          float* outL, float* outR, int n, bool overwrite) noexcept
{
    if (overwrite)
    {
        std::memset(outL, 0, sizeof(float) * static_cast<size_t>(n));
        std::memset(outR, 0, sizeof(float) * static_cast<size_t>(n));
    }
    if (d.empty()) return;
    const int len = std::max(64, static_cast<int>(p.sizeSamples));
    const double overlap = std::max(1.0, static_cast<double>(p.overlap));
    const double hop = std::max(1.0, len / overlap);
    const float gain = overlap >= 2.0 ? static_cast<float>(2.0 / overlap) : 1.f;
    const float* w = window_.data();

    for (int i = 0; i < n; ++i)
    {
        countdown_ -= 1.0;
        if (countdown_ <= 0.0)
        {
            countdown_ += hop;
            for (auto& g : grains_)
            {
                if (g.active) continue;
                g.active = true;
                g.age = 0;
                g.len = len;
                double pos = anchor + i * anchorInc;
                if (p.scatterFrames > 0.0) pos += static_cast<double>(rng_.bi()) * p.scatterFrames;
                double ratio = p.pitchRatio;
                if (p.pitchRandomSemis > 0.f) ratio *= std::pow(2.0, static_cast<double>(rng_.bi()) * p.pitchRandomSemis / 12.0);
                if (p.align) pos += bestOffset(d, r, pos, ratio, std::max(32, static_cast<int>(d.sampleRate * 0.0125)));
                g.pos = pos;
                g.inc = ratio;
                lastIdx_ = static_cast<int>(&g - grains_.data());
                const float pan = p.spread > 0.f ? rng_.bi() * p.spread : 0.f;
                g.gl = std::cos((pan * 0.5f + 0.5f) * 0.5f * kPi) * 1.41421356f;
                g.gr = std::sin((pan * 0.5f + 0.5f) * 0.5f * kPi) * 1.41421356f;
                break;
            }
        }
        float accL = 0.f, accR = 0.f;
        for (auto& g : grains_)
        {
            if (!g.active) continue;
            const float ph = static_cast<float>(g.age) / static_cast<float>(g.len) * 2048.f;
            const int wi = std::min(2047, static_cast<int>(ph));
            const float wv = w[wi] + (w[wi + 1] - w[wi]) * (ph - static_cast<float>(wi));
            float sl, sr;
            readWrapped(d, r, g.pos + g.age * g.inc, sl, sr);
            accL += sl * wv * g.gl;
            accR += sr * wv * g.gr;
            if (++g.age >= g.len) g.active = false;
        }
        outL[i] += accL * gain;
        outR[i] += accR * gain;
    }
}

// ---- BeatRepeat ------------------------------------------------------------------------------------
void BeatRepeat::prepare(double sampleRate)
{
    sr_ = sampleRate;
    size_ = static_cast<int>(sampleRate * 4.0) + 8;
    ringL_.assign(static_cast<size_t>(size_), 0.f);
    ringR_.assign(static_cast<size_t>(size_), 0.f);
    reset();
}

void BeatRepeat::reset() noexcept
{
    std::fill(ringL_.begin(), ringL_.end(), 0.f);
    std::fill(ringR_.begin(), ringR_.end(), 0.f);
    w_ = 0; pos_ = 0; active_ = false; activeDiv_ = 0.0; blend_ = 0.f;
}

void BeatRepeat::process(float& l, float& r, double divBeats, double bpm, float gate, float mix) noexcept
{
    const bool want = divBeats > 0.0 && bpm > 1.0;
    if (want && (!active_ || std::abs(divBeats - activeDiv_) > 1e-9))
    {
        // freeze the slice that has just passed
        sliceLen_ = std::clamp(static_cast<int>(divBeats * 60.0 / bpm * sr_), 64, size_ - 2);
        startIdx_ = ((w_ - sliceLen_) % size_ + size_) % size_;
        pos_ = 0;
        active_ = true;
        activeDiv_ = divBeats;
    }
    if (!want) active_ = false;

    const float target = active_ ? 1.f : 0.f;
    const float slew = active_ ? 0.02f : 0.01f;
    blend_ += (target - blend_) * slew;
    if (!active_ && blend_ < 1e-4f) blend_ = 0.f;

    if (blend_ > 0.f)
    {
        const int idx = (startIdx_ + pos_) % size_;
        float rl = ringL_[static_cast<size_t>(idx)], rr = ringR_[static_cast<size_t>(idx)];
        // gate: only the first `gate` part of every repeat sounds (short fades at the edges)
        const float ph = static_cast<float>(pos_) / static_cast<float>(sliceLen_);
        const float g = std::clamp(gate, 0.02f, 1.f);
        float env = ph < g ? 1.f : 0.f;
        const float fade = 48.f / static_cast<float>(sliceLen_);
        if (ph < fade) env *= ph / fade;
        else if (ph > g - fade && ph < g) env *= std::max(0.f, (g - ph) / fade);
        rl *= env; rr *= env;
        if (++pos_ >= sliceLen_) pos_ = 0;
        const float m = std::clamp(mix, 0.f, 1.f) * blend_;
        // the live input keeps being recorded only while not repeating
        l = l * (1.f - m) + rl * m;
        r = r * (1.f - m) + rr * m;
        if (active_) return;
    }
    ringL_[static_cast<size_t>(w_)] = l;
    ringR_[static_cast<size_t>(w_)] = r;
    if (++w_ >= size_) w_ = 0;
}

// ---- tempo detection -------------------------------------------------------------------------------
TempoEstimate detectTempo(const float* mono, size_t n, double sr)
{
    TempoEstimate out;
    const double dur = static_cast<double>(n) / sr;
    if (n < 2 || dur < 1.5 || dur > 600.0) return out;

    const double envRate = 200.0;
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(sr / envRate));
    const double frameSec = static_cast<double>(hop) / sr;
    const size_t frames = n / hop;
    if (frames < 64) return out;

    // onset strength: log RMS of the first difference (emphasises transients), positive rise only
    std::vector<float> env(frames), onset(frames, 0.f);
    for (size_t k = 0; k < frames; ++k)
    {
        double acc = 0.0;
        const size_t s0 = k * hop;
        for (size_t i = 1; i < hop; ++i)
        {
            const double dlt = static_cast<double>(mono[s0 + i]) - static_cast<double>(mono[s0 + i - 1]);
            acc += dlt * dlt;
        }
        env[k] = std::log1p(200.f * static_cast<float>(std::sqrt(acc / static_cast<double>(hop))));
    }
    double mean = 0.0;
    for (size_t k = 1; k < frames; ++k)
    {
        onset[k] = std::max(0.f, env[k] - env[k - 1]);
        mean += onset[k];
    }
    mean /= static_cast<double>(frames);
    double energy = 0.0;
    for (auto& v : onset) { v = std::max(0.f, v - static_cast<float>(mean)); energy += static_cast<double>(v) * v; }
    if (energy < 1e-9) return out;

    auto ac = [&](double lag) {   // normalised autocorrelation at a fractional lag (in frames)
        const size_t l0 = static_cast<size_t>(lag);
        const double f = lag - static_cast<double>(l0);
        if (l0 + 2 >= frames) return 0.0;
        double a0 = 0.0, a1 = 0.0;
        for (size_t k = 0; k + l0 + 1 < frames; ++k) { a0 += static_cast<double>(onset[k]) * onset[k + l0]; a1 += static_cast<double>(onset[k]) * onset[k + l0 + 1]; }
        return (a0 * (1.0 - f) + a1 * f) / energy;
    };
    auto score = [&](double bpm) {
        const double beatFrames = 60.0 / bpm / frameSec;
        double s = 0.0, wsum = 0.0;
        for (int m = 1; m <= 8; ++m)
        {
            const double lag = beatFrames * m;
            if (lag > static_cast<double>(frames) * 0.6) break;
            // small search around the lag absorbs tiny tempo drift
            double best = 0.0;
            for (int dlt = -1; dlt <= 1; ++dlt) best = std::max(best, ac(lag + dlt * 0.5));
            const double w = 1.0 / m;
            s += w * best;
            wsum += w;
        }
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 0.7, 2.0));
        return wsum > 0.0 ? s / wsum * (0.6 + 0.4 * prior) : 0.0;
    };

    struct Cand { double bpm, score; bool loop; };
    std::vector<Cand> cands;
    // (a) whole-beat loops: the sample lasts exactly N beats
    for (int beats = static_cast<int>(std::ceil(dur * 70.0 / 60.0)); beats <= static_cast<int>(std::floor(dur * 180.0 / 60.0)); ++beats)
    {
        if (beats < 1) continue;
        const double bpm = beats * 60.0 / dur;
        cands.push_back({ bpm, score(bpm) * 1.12, true });
    }
    // (b) free tempo grid (not a loop): 0.5 bpm steps
    for (double bpm = 70.0; bpm <= 180.0; bpm += 0.5) cands.push_back({ bpm, score(bpm), false });
    if (cands.empty()) return out;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    const Cand best = cands.front();
    double runnerUp = 0.0;   // best score at a clearly different tempo
    for (const auto& c : cands)
        if (std::abs(std::log2(c.bpm / best.bpm)) > 0.08) { runnerUp = c.score; break; }
    out.bpm = best.bpm;
    out.beats = std::round(dur * best.bpm / 60.0);
    out.bars = out.beats / 4.0;
    out.confidence = static_cast<float>(std::clamp(best.score > 1e-9 ? (best.score - runnerUp) / best.score * 2.0 : 0.0, 0.0, 1.0));
    return out;
}

} // namespace mangle
