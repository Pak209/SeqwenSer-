// Tests of the Seqwenser sampler engine (slicing, step sequencing, locks, conditions, play modes, effects, C API).
#include <cmath>
#include <vector>

#include "TestFramework.h"
#include "TestSignals.h"
#include "seqw/Sampler.h"
#include "seqw_engine.h"

using namespace seqw;
using mangle::SampleData;

namespace {

struct Rendered { std::vector<float> l, r; };

Rendered renderSeconds(Sampler& s, double seconds, int block = 256)
{
    Rendered out;
    const long total = static_cast<long>(seconds * s.sampleRate());
    out.l.resize(static_cast<size_t>(total));
    out.r.resize(static_cast<size_t>(total));
    for (long pos = 0; pos < total; pos += block)
    {
        const int n = static_cast<int>(std::min<long>(block, total - pos));
        s.render(out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

double rms(const std::vector<float>& v, size_t a, size_t b)
{
    double acc = 0.0;
    b = std::min(b, v.size());
    for (size_t i = a; i < b; ++i) acc += static_cast<double>(v[i]) * v[i];
    return b > a ? std::sqrt(acc / static_cast<double>(b - a)) : 0.0;
}

float peakOf(const std::vector<float>& v)
{
    float p = 0.f;
    for (float x : v) { CHECK(std::isfinite(x)); p = std::max(p, std::abs(x)); }
    return p;
}

std::shared_ptr<SampleData> shared(SampleData d) { return std::make_shared<SampleData>(std::move(d)); }

/** A sample made of `count` different sine tones, 0.25 s each: slice i has frequency 200 * (i + 1) Hz. */
std::shared_ptr<SampleData> toneStrip(double sr, int count)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = sr;
    const size_t per = static_cast<size_t>(0.25 * sr);
    for (int k = 0; k < count; ++k)
        for (size_t i = 0; i < per; ++i)
            d->left.push_back(0.5f * static_cast<float>(std::sin(2.0 * M_PI * 200.0 * (k + 1) * static_cast<double>(i) / sr)));
    d->right = d->left;
    return d;
}

/** Frequency estimate by zero crossings in a window. */
double freqOf(const std::vector<float>& v, size_t a, size_t b, double sr)
{
    int crossings = 0;
    for (size_t i = a + 1; i < b && i < v.size(); ++i) if ((v[i - 1] < 0.f) != (v[i] < 0.f)) ++crossings;
    return crossings * 0.5 * sr / static_cast<double>(b - a);
}

SqState quietState()
{
    SqState s = defaultState();
    s.volume = 1.f;
    return s;
}

} // namespace

TEST_CASE("Sampler: silent without a sample or pattern, transport start/stop")
{
    Sampler s; s.prepare(44100.0, 256);
    s.setPlaying(true);
    auto o = renderSeconds(s, 0.5);
    CHECK(peakOf(o.l) < 1e-6f);
    s.setSample(toneStrip(44100.0, 4));
    SqState st = quietState();
    s.setState(st);          // no step is active
    o = renderSeconds(s, 0.5);
    CHECK(peakOf(o.l) < 1e-6f);
    CHECK_EQ(s.status().playing, 1);
    s.setPlaying(false);
    renderSeconds(s, 0.1);
    CHECK_EQ(s.status().playing, 0);
}

TEST_CASE("Autoslice: finds drum hits; sensitivity changes the count; equal-slice fallback")
{
    const auto loop = testsig::drumLoop(44100.0, 120.0, 8);
    float e[SQ_MAX_SLICES + 1];
    const int hi = autoSlice(loop, 0.9f, 0.f, 1.f, e, 16);
    const int lo = autoSlice(loop, 0.1f, 0.f, 1.f, e, 16);
    CHECK(hi >= 6 && hi <= 16);
    CHECK(lo >= 1);
    CHECK(hi >= lo);
    const int n = autoSlice(loop, 0.5f, 0.f, 1.f, e, 16);
    CHECK(n >= 4);
    CHECK_NEAR(e[0], 0.f, 1e-6);
    CHECK_NEAR(e[n], 1.f, 1e-3);
    for (int i = 0; i < n; ++i) CHECK(e[i + 1] > e[i]);
    // on-beat: the second edge is near beat 1 (0.5 s of 4 s = 0.125), give or take
    bool nearBeat = false;
    for (int i = 1; i < n; ++i) { const double beats = e[i] * 8.0; if (std::abs(beats - std::round(beats)) < 0.08) nearBeat = true; }
    CHECK(nearBeat);
    // silence -> equal slices
    SampleData sil; sil.sampleRate = 44100.0; sil.left.assign(44100, 0.f); sil.right = sil.left;
    const int m = autoSlice(sil, 0.5f, 0.f, 1.f, e, 16);
    CHECK(m >= 1 && m <= 16);
    // trim region respected
    const int t = autoSlice(loop, 0.7f, 0.25f, 0.75f, e, 16);
    CHECK_NEAR(e[0], 0.25f, 1e-3);
    CHECK_NEAR(e[t], 0.75f, 1e-3);
    // demo break has clear hits
    auto demo = makeDemoBreak(44100.0);
    const int dn = autoSlice(*demo, 0.6f, 0.f, 1.f, e, 16);
    CHECK(dn >= 6);
}

TEST_CASE("Sampler: steps play their slices at the right time; inactive steps are silent")
{
    const double sr = 44100.0;
    Sampler s; s.prepare(sr, 256);
    s.setSample(toneStrip(sr, 4));
    SqState st = quietState();
    equalSlices(0.f, 1.f, 4, st.sliceEdge); st.numSlices = 4;
    st.bpm = 120.f; st.rate = SQ_RATE_8;      // 0.25 s per step
    st.fx[SQ_FX_FILTER][0] = 1.f;
    st.length = 4;
    for (int i = 0; i < 4; ++i) { st.steps[0][i].slice = i; st.steps[0][i].active = (i != 2); }
    s.setState(st);
    s.setPlaying(true);
    auto o = renderSeconds(s, 1.0);
    const size_t q = static_cast<size_t>(0.25 * sr);
    CHECK(rms(o.l, q * 0 + 2000, q - 2000) > 0.05);
    CHECK(rms(o.l, q * 1 + 2000, q * 2 - 2000) > 0.05);
    CHECK(rms(o.l, q * 2 + 2000, q * 3 - 2000) < 0.005);    // step 3 inactive
    CHECK(rms(o.l, q * 3 + 2000, q * 4 - 2000) > 0.05);
    CHECK_NEAR(freqOf(o.l, q * 0 + 3000, q - 3000, sr), 200.0, 12.0);
    CHECK_NEAR(freqOf(o.l, q * 1 + 3000, q * 2 - 3000, sr), 400.0, 20.0);
    CHECK_NEAR(freqOf(o.l, q * 3 + 3000, q * 4 - 3000, sr), 800.0, 40.0);
}

TEST_CASE("Sampler: pitch semitones, per-step parameter lock overrides the global value")
{
    const double sr = 44100.0;
    Sampler s; s.prepare(sr, 256);
    s.setSample(shared(testsig::sine(sr, 2.0, 400.0)));
    SqState st = quietState();
    st.bpm = 120.f; st.rate = SQ_RATE_4;         // 0.5 s per step
    st.length = 2;
    st.steps[0][0].active = 1; st.steps[0][1].active = 1;
    st.fx[SQ_FX_PITCH][0] = 0.5f + 12.f / 24.f;   // +12 semitones globally
    st.steps[0][1].hasLock[SQ_FX_PITCH] = 1;
    st.steps[0][1].lock[SQ_FX_PITCH][0] = 0.5f;   // step 2: locked to 0
    s.setState(st); s.setPlaying(true);
    auto o = renderSeconds(s, 1.0);
    const size_t h = static_cast<size_t>(0.5 * sr);
    CHECK_NEAR(freqOf(o.l, 4000, h - 4000, sr), 800.0, 30.0);
    CHECK_NEAR(freqOf(o.l, h + 4000, 2 * h - 4000, sr), 400.0, 20.0);
    // -12 st
    st.steps[0][1].hasLock[SQ_FX_PITCH] = 0;
    st.fx[SQ_FX_PITCH][0] = 0.f;
    s.reset(); s.setState(st); s.setPlaying(true);
    o = renderSeconds(s, 0.5);
    CHECK_NEAR(freqOf(o.l, 4000, h - 4000, sr), 200.0, 15.0);
}

TEST_CASE("Sampler: reverse, lo-fi, stretch flags change the sound; stretch keeps pitch and fills the step")
{
    const double sr = 44100.0;
    auto drum = shared(testsig::drumLoop(sr, 120.0, 4));
    auto run = [&](auto&& tweak) {
        Sampler s; s.prepare(sr, 256); s.setSample(drum); s.setSeed(3);
        SqState st = quietState();
        st.rate = SQ_RATE_4; st.length = 1; st.loop = 1;
        st.steps[0][0].active = 1;
        st.trimEnd = 0.25f;                      // one beat of the loop
        tweak(st);
        s.setState(st); s.setPlaying(true);
        return renderSeconds(s, 0.48);
    };
    auto plain = run([](SqState&) {});
    auto rev = run([](SqState& s) { s.steps[0][0].reverse = 1; });
    auto lofi = run([](SqState& s) { s.steps[0][0].lofi = 1; });
    double dRev = 0, dLofi = 0;
    for (size_t i = 0; i < plain.l.size(); ++i) { dRev += std::abs(plain.l[i] - rev.l[i]); dLofi += std::abs(plain.l[i] - lofi.l[i]); }
    CHECK(dRev > 10.0);
    CHECK(dLofi > 1.0);
    CHECK(peakOf(rev.l) > 0.1f);

    // stretch: a 0.25 s sine slice stretched into a 0.5 s step: still 500 Hz and sounding across the whole step
    Sampler s; s.prepare(sr, 256);
    auto d = std::make_shared<SampleData>(testsig::sine(sr, 0.25, 500.0));
    s.setSample(d);
    SqState st = quietState();
    st.rate = SQ_RATE_4; st.length = 1;
    st.steps[0][0].active = 1; st.steps[0][0].stretch = 1;
    s.setState(st); s.setPlaying(true);
    auto o = renderSeconds(s, 0.45);
    CHECK(rms(o.l, 0.3 * sr, 0.42 * sr) > 0.05);
    CHECK_NEAR(freqOf(o.l, 0.1 * sr, 0.4 * sr, sr), 500.0, 30.0);
}

TEST_CASE("Sampler: play modes order the steps")
{
    const double sr = 44100.0;
    auto order = [&](int mode, int seconds4) {
        Sampler s; s.prepare(sr, 256); s.setSeed(11);
        s.setSample(toneStrip(sr, 4));
        SqState st = quietState();
        equalSlices(0.f, 1.f, 4, st.sliceEdge); st.numSlices = 4;
        st.rate = SQ_RATE_8; st.bpm = 120.f; st.length = 4; st.playMode = mode;
        for (int i = 0; i < 4; ++i) { st.steps[0][i].active = 1; st.steps[0][i].slice = i; }
        s.setState(st); s.setPlaying(true);
        auto o = renderSeconds(s, 0.25 * seconds4);
        std::vector<int> seq;
        for (int k = 0; k < seconds4; ++k)
        {
            const double f = freqOf(o.l, static_cast<size_t>((k * 0.25 + 0.03) * sr), static_cast<size_t>((k * 0.25 + 0.22) * sr), sr);
            seq.push_back(static_cast<int>(std::lround(f / 200.0)));
        }
        return seq;
    };
    CHECK(order(SQ_MODE_FORWARD, 8) == (std::vector<int> { 1, 2, 3, 4, 1, 2, 3, 4 }));
    CHECK(order(SQ_MODE_REVERSE, 8) == (std::vector<int> { 4, 3, 2, 1, 4, 3, 2, 1 }));
    CHECK(order(SQ_MODE_PINGPONG, 8) == (std::vector<int> { 1, 2, 3, 4, 3, 2, 1, 2 }));
    const auto rnd = order(SQ_MODE_RANDOM, 24);
    int counts[5] = {};
    for (int v : rnd) { CHECK(v >= 1 && v <= 4); ++counts[std::clamp(v, 0, 4)]; }
    for (int k = 1; k <= 4; ++k) CHECK(counts[k] >= 1);
    CHECK(rnd != order(SQ_MODE_FORWARD, 24));
}

TEST_CASE("Sampler: probability and conditions")
{
    const double sr = 44100.0;
    auto hits = [&](auto&& tweak, int steps) {
        Sampler s; s.prepare(sr, 256); s.setSeed(99);
        s.setSample(shared(testsig::sine(sr, 4.0, 300.0)));
        SqState st = quietState();
        st.rate = SQ_RATE_16; st.bpm = 240.f; st.length = 1; st.loop = 1;      // 1/16 at 240 bpm = 62.5 ms per step
        st.steps[0][0].active = 1;
        tweak(st);
        s.setState(st); s.setPlaying(true);
        auto o = renderSeconds(s, 0.0625 * steps);
        int n = 0;
        for (int k = 0; k < steps; ++k)
            if (rms(o.l, static_cast<size_t>((k * 0.0625 + 0.02) * sr), static_cast<size_t>((k * 0.0625 + 0.05) * sr)) > 0.03) ++n;
        return n;
    };
    CHECK_EQ(hits([](SqState&) {}, 32), 32);
    const int half = hits([](SqState& s) { s.probability = 0.5f; }, 200);
    CHECK(half > 70 && half < 130);
    CHECK_EQ(hits([](SqState& s) { s.probability = 0.f; }, 20), 0);
    const int p80 = hits([](SqState& s) { s.probability = 0.8f; }, 200);
    CHECK(p80 > 140 && p80 < 180);
    // step probability multiplies
    const int mul = hits([](SqState& s) { s.probability = 0.5f; s.steps[0][0].prob = 0.5f; }, 400);
    CHECK(mul > 60 && mul < 140);
    // every 2nd pass (pass counter = step counter here since length == 1)
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].cond = SQ_COND_EVERY2; }, 20), 10);
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].cond = SQ_COND_FIRST; }, 20), 1);
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].cond = SQ_COND_NOT_FIRST; }, 20), 19);
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].cond = SQ_COND_EVERY4; }, 20), 5);
    // velocity conditions
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].velCond = SQ_VEL_GT50; s.steps[0][0].velocity = 0.9f; }, 10), 10);
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].velCond = SQ_VEL_GT50; s.steps[0][0].velocity = 0.3f; }, 10), 0);
    CHECK_EQ(hits([](SqState& s) { s.steps[0][0].velCond = SQ_VEL_LT50; s.steps[0][0].velocity = 0.3f; }, 10), 10);
}

TEST_CASE("Sampler: bank B, pattern length, swing, tempo and rate")
{
    const double sr = 44100.0;
    Sampler s; s.prepare(sr, 256);
    s.setSample(toneStrip(sr, 4));
    SqState st = quietState();
    equalSlices(0.f, 1.f, 4, st.sliceEdge); st.numSlices = 4;
    st.rate = SQ_RATE_8; st.bpm = 120.f; st.length = 2; st.bank = 1;
    st.steps[0][0].active = 1; st.steps[0][0].slice = 0;                    // bank A would play slice 0
    st.steps[1][0].active = 1; st.steps[1][0].slice = 2;
    st.steps[1][1].active = 1; st.steps[1][1].slice = 3;
    s.setState(st); s.setPlaying(true);
    auto o = renderSeconds(s, 1.0);
    CHECK_NEAR(freqOf(o.l, 3000, 9000, sr), 600.0, 30.0);
    CHECK_NEAR(freqOf(o.l, static_cast<size_t>(0.25 * sr) + 3000, static_cast<size_t>(0.25 * sr) + 9000, sr), 800.0, 40.0);
    CHECK_NEAR(freqOf(o.l, static_cast<size_t>(0.5 * sr) + 3000, static_cast<size_t>(0.5 * sr) + 9000, sr), 600.0, 30.0);   // length 2 -> wraps
    // tempo change is picked up: 240 bpm halves the step
    st.bpm = 240.f; s.setState(st); s.reset(); s.setPlaying(true); s.setState(st);
    o = renderSeconds(s, 0.5);
    CHECK_NEAR(freqOf(o.l, static_cast<size_t>(0.125 * sr) + 2000, static_cast<size_t>(0.125 * sr) + 5000, sr), 800.0, 60.0);
    // swing: with full swing the first step of a pair lasts 1.5x (0.375 s at 1/8 and 120 bpm)
    {
        Sampler w; w.prepare(sr, 256);
        w.setSample(shared(testsig::sine(sr, 3.0, 600.0)));
        SqState sw = quietState();
        sw.rate = SQ_RATE_8; sw.bpm = 120.f; sw.length = 2; sw.swing = 1.f;
        sw.steps[0][0].active = 1; sw.steps[0][1].active = 1;
        sw.steps[0][1].hasLock[SQ_FX_PITCH] = 1; sw.steps[0][1].lock[SQ_FX_PITCH][0] = 1.f;   // +12 st
        w.setState(sw); w.setPlaying(true);
        o = renderSeconds(w, 0.6);
        CHECK_NEAR(freqOf(o.l, static_cast<size_t>(0.28 * sr), static_cast<size_t>(0.35 * sr), sr), 600.0, 40.0);
        CHECK_NEAR(freqOf(o.l, static_cast<size_t>(0.40 * sr), static_cast<size_t>(0.46 * sr), sr), 1200.0, 150.0);
    }
    // one-shot pattern ends by itself
    st.swing = 0.f; st.loop = 0; st.length = 2;
    s.reset(); s.setState(st); s.setPlaying(true);
    o = renderSeconds(s, 1.5);
    CHECK(rms(o.l, static_cast<size_t>(1.1 * sr), static_cast<size_t>(1.5 * sr)) < 0.005);
    CHECK_EQ(s.status().playing, 0);
}

TEST_CASE("Sampler: effects - filter, drive, space, repeat, grain each change the output and stay bounded")
{
    const double sr = 44100.0;
    auto drum = shared(testsig::drumLoop(sr, 120.0, 8));
    auto run = [&](auto&& tweak) {
        Sampler s; s.prepare(sr, 256); s.setSample(drum); s.setSeed(5);
        SqState st = quietState();
        st.rate = SQ_RATE_8; st.length = 16;
        for (int i = 0; i < 16; ++i) { st.steps[0][i].active = 1; st.steps[0][i].slice = 0; }
        st.numSlices = 0; st.trimStart = 0.f; st.trimEnd = 1.f;
        tweak(st);
        s.setState(st); s.setPlaying(true);
        return renderSeconds(s, 3.0);
    };
    auto base = run([](SqState&) {});
    auto diff = [&](const Rendered& a) { double d = 0; for (size_t i = 0; i < a.l.size(); ++i) d += std::abs(a.l[i] - base.l[i]); return d; };
    const auto lp = run([](SqState& s) { s.fx[SQ_FX_FILTER][0] = 0.25f; s.fx[SQ_FX_FILTER][1] = 0.05f; });
    CHECK(diff(lp) > 50.0);
    CHECK(rms(lp.l, 0, lp.l.size()) < rms(base.l, 0, base.l.size()));   // low-pass removes energy
    const auto dr = run([](SqState& s) { s.fx[SQ_FX_DRIVE][0] = 0.9f; });
    CHECK(diff(dr) > 50.0);
    const auto sp = run([](SqState& s) { s.fx[SQ_FX_SPACE][0] = 0.7f; s.fx[SQ_FX_SPACE][1] = 0.9f; });
    CHECK(diff(sp) > 50.0);
    const auto rp = run([](SqState& s) { s.fx[SQ_FX_REPEAT][0] = 0.9f; s.fx[SQ_FX_REPEAT][1] = 0.66f; });
    CHECK(diff(rp) > 50.0);
    const auto gr = run([](SqState& s) { s.fx[SQ_FX_GRAIN][0] = 0.9f; s.fx[SQ_FX_GRAIN][1] = 0.7f; });
    CHECK(diff(gr) > 50.0);
    const auto fm = run([](SqState& s) { s.fx[SQ_FX_PITCH][1] = 1.f; });
    CHECK(diff(fm) > 10.0);
    for (const auto* r : { &lp, &dr, &sp, &rp, &gr, &fm }) CHECK(peakOf(r->l) < 1.01f);
    // everything at once, extreme
    const auto all = run([](SqState& s) {
        for (int f = 0; f < SQ_FX; ++f) { s.fx[f][0] = 1.f; s.fx[f][1] = 1.f; }
        s.fx[SQ_FX_FILTER][0] = 0.3f;
        s.volume = 1.5f;
    });
    CHECK(peakOf(all.l) <= 1.0001f);
    CHECK(peakOf(all.r) <= 1.0001f);
}

TEST_CASE("Sampler: pads play slices immediately (monophonic-free); block size independence of the pattern")
{
    const double sr = 44100.0;
    Sampler s; s.prepare(sr, 512);
    s.setSample(toneStrip(sr, 4));
    SqState st = quietState();
    equalSlices(0.f, 1.f, 4, st.sliceEdge); st.numSlices = 4;
    s.setState(st);
    renderSeconds(s, 0.01);
    s.triggerPad(2, 1.f);
    auto o = renderSeconds(s, 0.2);
    CHECK_NEAR(freqOf(o.l, 3000, 8000, sr), 600.0, 40.0);
    CHECK(s.status().voices >= 1);
    // same result with different block sizes (step timing is sample accurate)
    auto pattern = [&](int block) {
        Sampler q; q.prepare(sr, block); q.setSample(toneStrip(sr, 4)); q.setSeed(2);
        SqState t = quietState();
        equalSlices(0.f, 1.f, 4, t.sliceEdge); t.numSlices = 4;
        t.rate = SQ_RATE_16; t.length = 8;
        for (int i = 0; i < 8; ++i) { t.steps[0][i].active = (i % 3 != 1); t.steps[0][i].slice = i % 4; }
        q.setState(t); q.setPlaying(true);
        return renderSeconds(q, 1.0, block);
    };
    auto a = pattern(64), b = pattern(509);
    double dd = 0;
    for (size_t i = 0; i < a.l.size(); ++i) dd = std::max(dd, static_cast<double>(std::abs(a.l[i] - b.l[i])));
    CHECK(dd < 0.02);
}

TEST_CASE("Sampler: sample swap while playing is safe; garbage collection")
{
    const double sr = 44100.0;
    Sampler s; s.prepare(sr, 256);
    SqState st = quietState();
    st.rate = SQ_RATE_16; st.length = 16;
    for (int i = 0; i < 16; ++i) st.steps[0][i].active = 1;
    s.setState(st); s.setPlaying(true);
    for (int k = 0; k < 20; ++k)
    {
        s.setSample(k % 3 == 2 ? nullptr : toneStrip(sr, 2 + k % 4));
        auto o = renderSeconds(s, 0.05);
        CHECK(peakOf(o.l) < 1.01f);
        s.collectGarbage();
    }
}

TEST_CASE("C API: load, peaks, detect tempo, auto slice, render, bounce")
{
    SqEngine* e = sq_create(44100.0, 256);
    CHECK(e != nullptr);
    CHECK_EQ(sq_load_demo(e), 1);
    CHECK_EQ(sq_sample_frames(e), 176400);         // 32 sixteenths at 120 bpm
    float mn[64], mx[64];
    sq_peaks(e, 0.f, 1.f, 64, mn, mx);
    float top = 0.f;
    for (int i = 0; i < 64; ++i) { top = std::max(top, mx[i]); CHECK(mn[i] <= 0.f); }
    CHECK(top > 0.3f);
    float conf = 0;
    const double bpm = sq_detect_bpm(e, &conf);
    CHECK(bpm == 0.0 || (bpm > 100.0 && bpm < 140.0));
    SqState st = sq_default_state();
    sq_auto_slice(e, &st, 0.6f);
    CHECK(st.numSlices >= 4);
    sq_equal_slices(&st, 8);
    CHECK_EQ(st.numSlices, 8);
    CHECK_NEAR(st.sliceEdge[8], 1.0, 1e-5);
    for (int i = 0; i < 8; ++i) { st.steps[0][i * 2].active = 1; st.steps[0][i * 2].slice = i; }
    st.length = 16; st.rate = SQ_RATE_16;
    sq_set_state(e, &st);
    sq_set_playing(e, 1);
    std::vector<float> l(512), r(512);
    float peak = 0;
    for (int i = 0; i < 400; ++i) { sq_render(e, l.data(), r.data(), 512); for (float v : l) peak = std::max(peak, std::abs(v)); }
    CHECK(peak > 0.2f);
    SqStatus ss = sq_status(e);
    CHECK_EQ(ss.playing, 1);
    // bounce
    float* pcm = nullptr;
    const long frames = sq_bounce(e, &st, 2, 1.0, &pcm);
    CHECK(pcm != nullptr);
    CHECK_NEAR(static_cast<double>(frames), 2.0 * 16 * 0.125 * 44100.0 + 44100.0, 2.0);
    float bp = 0;
    for (long i = 0; i < frames * 2; ++i) { CHECK(std::isfinite(pcm[i])); bp = std::max(bp, std::abs(pcm[i])); }
    CHECK(bp > 0.2f);
    sq_free(pcm);
    // bad input
    CHECK_EQ(sq_load_pcm(e, nullptr, nullptr, 0, 44100.0), 0);
    sq_destroy(e);
}
