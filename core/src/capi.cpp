#include "seqw_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mangle/Dsp.h"
#include "seqw/Sampler.h"

struct SqEngine
{
    seqw::Sampler sampler;
    double sr = 44100.0;
    int maxBlock = 512;
};

extern "C" {

SqEngine* sq_create(double sampleRate, int maxBlock)
{
    auto* e = new SqEngine();
    e->sr = sampleRate > 0 ? sampleRate : 44100.0;
    e->maxBlock = maxBlock > 0 ? maxBlock : 512;
    e->sampler.prepare(e->sr, e->maxBlock);
    return e;
}

void sq_destroy(SqEngine* e) { delete e; }
SqState sq_default_state(void) { return seqw::defaultState(); }

int sq_load_pcm(SqEngine* e, const float* l, const float* r, long frames, double sr)
{
    if (e == nullptr || l == nullptr || frames < 64 || sr < 1000.0) return 0;
    auto d = std::make_shared<mangle::SampleData>();
    d->sampleRate = sr;
    d->left.assign(l, l + frames);
    if (r != nullptr) d->right.assign(r, r + frames); else d->right = d->left;
    for (long i = 0; i < frames; ++i)
    {
        if (!std::isfinite(d->left[static_cast<size_t>(i)])) d->left[static_cast<size_t>(i)] = 0.f;
        if (!std::isfinite(d->right[static_cast<size_t>(i)])) d->right[static_cast<size_t>(i)] = 0.f;
    }
    e->sampler.setSample(std::move(d));
    return 1;
}

int sq_load_demo(SqEngine* e)
{
    if (e == nullptr) return 0;
    e->sampler.setSample(seqw::makeDemoBreak(44100.0));
    return 1;
}

void sq_clear_sample(SqEngine* e) { if (e) e->sampler.setSample(nullptr); }
long sq_sample_frames(const SqEngine* e) { auto s = e ? e->sampler.sample() : nullptr; return s ? static_cast<long>(s->frames()) : 0; }
long sq_copy_pcm(const SqEngine* e, float* l, float* r)
{
    auto s = e ? e->sampler.sample() : nullptr;
    if (!s || !l || !r) return 0;
    std::memcpy(l, s->left.data(), sizeof(float) * s->left.size());
    std::memcpy(r, s->right.data(), sizeof(float) * s->right.size());
    return static_cast<long>(s->left.size());
}
double sq_sample_rate(const SqEngine* e) { auto s = e ? e->sampler.sample() : nullptr; return s ? s->sampleRate : 0.0; }

void sq_peaks(const SqEngine* e, float from, float to, int columns, float* mins, float* maxs)
{
    for (int c = 0; c < columns; ++c) { mins[c] = 0.f; maxs[c] = 0.f; }
    auto s = e ? e->sampler.sample() : nullptr;
    if (!s || s->empty() || columns <= 0) return;
    const double n = static_cast<double>(s->frames());
    const double a = std::clamp(static_cast<double>(from), 0.0, 1.0) * n, b = std::clamp(static_cast<double>(to), 0.0, 1.0) * n;
    if (b - a < 1.0) return;
    for (int c = 0; c < columns; ++c)
    {
        const size_t i0 = static_cast<size_t>(a + (b - a) * c / columns);
        const size_t i1 = std::max(i0 + 1, static_cast<size_t>(a + (b - a) * (c + 1) / columns));
        float lo = 0.f, hi = 0.f;
        for (size_t i = i0; i < i1 && i < s->left.size(); ++i)
        {
            const float v = 0.5f * (s->left[i] + s->right[i]);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        mins[c] = lo; maxs[c] = hi;
    }
}

double sq_detect_bpm(const SqEngine* e, float* confidence)
{
    if (confidence) *confidence = 0.f;
    auto s = e ? e->sampler.sample() : nullptr;
    if (!s || s->empty()) return 0.0;
    std::vector<float> mono(s->left.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (s->left[i] + s->right[i]);
    const auto t = mangle::detectTempo(mono.data(), mono.size(), s->sampleRate);
    if (confidence) *confidence = t.confidence;
    return t.confidence >= 0.2f ? t.bpm : 0.0;
}

void sq_auto_slice(const SqEngine* e, SqState* st, float sens)
{
    auto s = e ? e->sampler.sample() : nullptr;
    if (!st) return;
    if (!s || s->empty()) { st->numSlices = 0; return; }
    st->numSlices = seqw::autoSlice(*s, sens, st->trimStart, st->trimEnd, st->sliceEdge, SQ_MAX_SLICES);
}

void sq_equal_slices(SqState* st, int count)
{
    if (st) st->numSlices = seqw::equalSlices(st->trimStart, st->trimEnd, count, st->sliceEdge);
}

SqStep* sq_state_step(SqState* s, int bank, int i)
{
    if (!s || bank < 0 || bank >= SQ_BANKS || i < 0 || i >= SQ_STEPS) return nullptr;
    return &s->steps[bank][i];
}
float* sq_state_fx(SqState* s, int fx) { return (s && fx >= 0 && fx < SQ_FX) ? s->fx[fx] : nullptr; }
float* sq_step_lock(SqStep* st, int fx) { return (st && fx >= 0 && fx < SQ_FX) ? st->lock[fx] : nullptr; }
int* sq_step_has_lock(SqStep* st, int fx) { return (st && fx >= 0 && fx < SQ_FX) ? &st->hasLock[fx] : nullptr; }
float* sq_state_slice_edges(SqState* s) { return s ? s->sliceEdge : nullptr; }

void sq_set_state(SqEngine* e, const SqState* s) { if (e && s) e->sampler.setState(*s); }
void sq_set_playing(SqEngine* e, int on) { if (e) e->sampler.setPlaying(on != 0); }
void sq_trigger_pad(SqEngine* e, int slice, float vel) { if (e) e->sampler.triggerPad(slice, vel); }
void sq_render(SqEngine* e, float* l, float* r, int n) { if (e) e->sampler.render(l, r, n); }
SqStatus sq_status(const SqEngine* e) { return e ? e->sampler.status() : SqStatus {}; }
void sq_collect_garbage(SqEngine* e) { if (e) e->sampler.collectGarbage(); }

long sq_bounce(const SqEngine* e, const SqState* st, int passes, double tail, float** out)
{
    auto s = e ? e->sampler.sample() : nullptr;
    if (!s || !st || !out || passes < 1) return 0;
    seqw::Sampler smp;
    smp.prepare(e->sr, 512);
    smp.setSample(s);
    SqState ss = *st;
    ss.loop = 0;
    smp.setState(ss);
    smp.setSeed(7);
    const int len = std::clamp(ss.length, 1, SQ_STEPS);
    double period = len;
    if (ss.playMode == SQ_MODE_PINGPONG) period = std::max(1, 2 * (len - 1));
    const double seconds = period * passes * seqw::stepBeats(ss.rate) * 60.0 / std::clamp<double>(ss.bpm, 30.0, 300.0) + std::max(0.0, tail);
    const long frames = static_cast<long>(seconds * e->sr);
    if (frames < 1 || frames > static_cast<long>(e->sr) * 60 * 10) return 0;
    // "loop off" plays one pass; for several passes keep looping and just render long enough
    if (passes > 1) { ss.loop = 1; smp.setState(ss); }
    auto* buf = static_cast<float*>(std::malloc(sizeof(float) * 2 * static_cast<size_t>(frames)));
    if (!buf) return 0;
    smp.setPlaying(true);
    for (long pos = 0; pos < frames; pos += 512)
    {
        const int n = static_cast<int>(std::min<long>(512, frames - pos));
        smp.render(buf + pos, buf + frames + pos, n);
    }
    *out = buf;
    return frames;
}

void sq_free(void* p) { std::free(p); }

} // extern "C"
