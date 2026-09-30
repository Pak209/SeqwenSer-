#include "TestFramework.h"
#include "TestSignals.h"
#include "mangle/Dsp.h"

using namespace mangle;

namespace {
std::vector<float> sineBuf(double sr, double hz, size_t n, float amp = 0.5f)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr));
    return v;
}
double gainAt(double sr, double hz, bool hp, float fc, float reso)
{
    SvFilter f;
    f.prepare(sr);
    f.set(fc, reso, hp);
    const auto x = sineBuf(sr, hz, 44100);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) { float l = x[i], r = x[i]; f.process(l, r); y[i] = l; }
    return testsig::rms(y, 20000, 44100) / testsig::rms(x, 20000, 44100);
}
} // namespace

TEST_CASE("Filter: low-pass and high-pass attenuate by the expected amounts")
{
    CHECK(gainAt(44100, 100, false, 1000.f, 0.f) > 0.95);          // passband
    CHECK(gainAt(44100, 8000, false, 1000.f, 0.f) < 0.02);         // ~ -12 dB/oct x 3 oct
    CHECK(gainAt(44100, 5000, true, 500.f, 0.f) > 0.95);
    CHECK(gainAt(44100, 50, true, 1000.f, 0.f) < 0.02);
    CHECK_NEAR(gainAt(44100, 1000, false, 1000.f, 0.f), 0.55, 0.08);   // Q ~ 0.55 at the corner: about -5 dB
    CHECK(gainAt(44100, 1000, false, 1000.f, 0.9f) > 2.0);         // resonance peak
}

TEST_CASE("Filter: stable at extreme settings")
{
    SvFilter f;
    f.prepare(48000);
    Rng rng;
    for (float fc : { 16.f, 20.f, 200.f, 20000.f, 23000.f })
        for (float reso : { 0.f, 1.f })
            for (bool hp : { false, true })
            {
                f.reset();
                f.set(fc, reso, hp);
                float peak = 0.f;
                for (int i = 0; i < 48000; ++i) { float l = rng.bi(), r = l; f.process(l, r); peak = std::max(peak, std::abs(l)); }
                CHECK(std::isfinite(peak));
                CHECK(peak < 200.f);
            }
}

TEST_CASE("Grit: 0 is an exact bypass; more drive adds harmonics, stays bounded and DC free")
{
    Grit g;
    g.prepare(44100);
    auto x = sineBuf(44100, 220, 44100, 0.6f);
    bool same = true;
    for (float s : x) { float l = s, r = s; g.process(l, r, 0.f); same = same && l == s && r == s; }
    CHECK(same);
    g.reset();
    double dc = 0, peak = 0;
    for (float s : x) { float l = s, r = s; g.process(l, r, 1.f); dc += l; peak = std::max(peak, static_cast<double>(std::abs(l))); }
    CHECK(std::abs(dc / static_cast<double>(x.size())) < 0.02);
    CHECK(peak < 1.2);
    // harmonic content: energy at 3rd harmonic (660 Hz) via Goertzel
    auto goertzel = [](const std::vector<float>& v, double hz, double sr) {
        double re = 0, im = 0;
        for (size_t i = 0; i < v.size(); ++i) { const double ph = 2 * M_PI * hz * static_cast<double>(i) / sr; re += v[i] * std::cos(ph); im += v[i] * std::sin(ph); }
        return std::sqrt(re * re + im * im) / static_cast<double>(v.size());
    };
    std::vector<float> y(x.size());
    g.reset();
    for (size_t i = 0; i < x.size(); ++i) { float l = x[i], r = x[i]; g.process(l, r, 0.8f); y[i] = l; }
    CHECK(goertzel(y, 660, 44100) > 10.0 * goertzel(x, 660, 44100) + 0.01);
}

TEST_CASE("Reverb: silent in gives silent out; impulse decays; bigger size rings longer")
{
    auto tail = [](float size) {
        Reverb rv;
        rv.prepare(44100);
        float l, r;
        rv.process(1.f, 1.f, l, r, size, 0.5f);
        double late = 0;
        for (int i = 1; i < 44100 * 3; ++i)
        {
            rv.process(0.f, 0.f, l, r, size, 0.5f);
            if (i > 44100 * 2) late += static_cast<double>(l) * l;
        }
        return late;
    };
    Reverb rv;
    rv.prepare(48000);
    float l = 1, r = 1;
    for (int i = 0; i < 1000; ++i) rv.process(0, 0, l, r, 0.5f, 0.5f);
    CHECK(l == 0.f && r == 0.f);
    const double small = tail(0.1f), big = tail(0.95f);
    CHECK(big > small * 20.0);
    CHECK(std::isfinite(big));
}

TEST_CASE("LFO: shapes stay in range, sine hits the right phases, S&H is stepwise and deterministic")
{
    for (int sh = 0; sh < static_cast<int>(LfoShape::Count); ++sh)
        for (int i = 0; i < 2000; ++i)
        {
            const float v = lfoValue(sh, i * 0.0137, 4.0);
            CHECK(v >= -1.0001f && v <= 1.0001f);
        }
    CHECK_NEAR(lfoValue(0, 0.0, 4.0), 0.0, 1e-6);
    CHECK_NEAR(lfoValue(0, 1.0, 4.0), 1.0, 1e-5);
    CHECK_NEAR(lfoValue(0, 3.0, 4.0), -1.0, 1e-5);
    CHECK_NEAR(lfoValue(1, 0.0, 4.0), -1.0, 1e-6);
    CHECK_NEAR(lfoValue(1, 2.0, 4.0), 1.0, 1e-6);
    CHECK_NEAR(lfoValue(3, 0.5, 4.0), 1.0, 1e-6);
    CHECK_NEAR(lfoValue(3, 3.0, 4.0), -1.0, 1e-6);
    CHECK_EQ(lfoValue(4, 0.10, 4.0), lfoValue(4, 0.20, 4.0));       // same held value
    CHECK(lfoValue(4, 0.10, 4.0) != lfoValue(4, 1.10, 4.0));
    CHECK_EQ(lfoValue(4, 7.3, 4.0), lfoValue(4, 7.3, 4.0));
}

TEST_CASE("Steps: index, phase, wrap and swing")
{
    auto s = stepAt(0.0, 0.5, 0.0, 16);
    CHECK_EQ(s.index, 0);
    s = stepAt(0.5, 0.5, 0.0, 16);
    CHECK_EQ(s.index, 1);
    s = stepAt(7.99, 0.5, 0.0, 16);
    CHECK_EQ(s.index, 15);
    s = stepAt(8.0, 0.5, 0.0, 16);
    CHECK_EQ(s.index, 0);
    CHECK_EQ(s.number, 16LL);
    s = stepAt(0.25, 0.5, 0.0, 16);
    CHECK_NEAR(s.phase, 0.5, 1e-9);
    // swing 1: the off-beat step starts at 75 % of the pair (1.5 steps)
    s = stepAt(0.74, 0.5, 1.0, 16);   // 1.48 steps: still step 0
    CHECK_EQ(s.index, 0);
    s = stepAt(0.76, 0.5, 1.0, 16);   // 1.52 steps: now step 1
    CHECK_EQ(s.index, 1);
    s = stepAt(2.0, 0.5, 0.7, 6);     // pattern length 6
    CHECK_EQ(s.index, 4);
    s = stepAt(-1.0, 0.5, 0.0, 16);
    CHECK_EQ(s.index, 0);
}

TEST_CASE("Beat repeat: off is a bit-exact bypass; on repeats the frozen slice")
{
    const double sr = 44100.0, bpm = 120.0;
    BeatRepeat br;
    br.prepare(sr);
    auto x = sineBuf(sr, 300, 44100 * 2);
    bool same = true;
    for (float s : x) { float l = s, r = s; br.process(l, r, 0.0, bpm, 1.f, 1.f); same = same && l == s; }
    CHECK(same);
    // ramp signal so the repeated slice is identifiable
    br.reset();
    const double slice = 0.25 * 60.0 / bpm * sr;   // 1/16 note
    std::vector<float> ramp(44100), out(44100);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i) / 44100.f;
    for (size_t i = 0; i < ramp.size(); ++i)
    {
        float l = ramp[i], r = ramp[i];
        br.process(l, r, i < 22050 ? 0.0 : 0.25, bpm, 1.f, 1.f);
        out[i] = l;
    }
    // well after the switch, the output is periodic with the slice length and no longer follows the ramp
    const size_t a = 30000, b = static_cast<size_t>(30000 + slice);
    CHECK_NEAR(out[a], out[b], 1e-3);
    CHECK(std::abs(out[40000] - ramp[40000]) > 0.05f);
    CHECK(br.active());
}

TEST_CASE("Beat repeat: gate shortens the audible part of each repeat")
{
    const double sr = 44100.0, bpm = 120.0;
    auto run = [&](float gate) {
        BeatRepeat br;
        br.prepare(sr);
        const auto x = sineBuf(sr, 300, 44100 * 2);
        std::vector<float> out(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            float l = x[i], r = x[i];
            br.process(l, r, i < 30000 ? 0.0 : 0.25, bpm, gate, 1.f);
            out[i] = l;
        }
        return testsig::rms(out, 44100, 88200);
    };
    CHECK(run(0.25f) < run(1.f) * 0.7);
}

TEST_CASE("Grains: pitch ratio 1 reproduces the source; 2 raises it an octave; scatter is bounded")
{
    const double sr = 44100.0;
    auto d = testsig::sine(sr, 3.0, 440.0);
    const auto reg = regionFor(d, 0.f, 1.f);
    GrainEngine g;
    g.prepare(sr, 1);
    std::vector<float> l(44100), r(44100);
    GrainEngine::Params p;
    p.sizeSamples = 2048; p.overlap = 3.f; p.pitchRatio = 1.0;
    g.process(d, reg, 1000.0, 1.0, p, l.data(), r.data(), 44100, true);
    CHECK_NEAR(testsig::zeroCrossHz(l, 8000, 40000, sr), 440.0, 15.0);
    CHECK(testsig::rms(l, 8000, 40000) > 0.25);
    g.reset();
    p.pitchRatio = 2.0;
    g.process(d, reg, 1000.0, 2.0, p, l.data(), r.data(), 44100, true);
    CHECK_NEAR(testsig::zeroCrossHz(l, 8000, 40000, sr), 880.0, 40.0);
    g.reset();
    p.pitchRatio = 1.0; p.scatterFrames = 20000; p.pitchRandomSemis = 3.f; p.spread = 1.f;
    g.process(d, reg, 30000.0, 1.0, p, l.data(), r.data(), 44100, true);
    double peak = 0;
    for (float v : l) peak = std::max(peak, static_cast<double>(std::abs(v)));
    CHECK(peak < 1.6);
    CHECK(g.activeGrains() <= GrainEngine::kMaxGrains);
}

TEST_CASE("Sample access: regions clamp, reads wrap inside the region and interpolate")
{
    SampleData d;
    d.left.resize(1000);
    for (size_t i = 0; i < 1000; ++i) d.left[i] = static_cast<float>(i);
    d.right = d.left;
    auto r = regionFor(d, 0.25f, 0.75f);
    CHECK_NEAR(r.start, 250.0, 1e-9);
    CHECK_NEAR(r.length, 500.0, 1e-9);
    float l, rr;
    readWrapped(d, r, 300.5, l, rr);
    CHECK_NEAR(l, 300.5, 1e-4);
    readWrapped(d, r, 750.0 + 10.0, l, rr);
    CHECK_NEAR(l, 260.0, 1e-4);
    readWrapped(d, r, 249.0, l, rr);
    CHECK_NEAR(l, 749.0, 1e-4);
    r = regionFor(d, 0.9f, 0.1f);   // inverted: still a valid region
    CHECK(r.length >= 64.0);
    r = regionFor(d, 0.f, 0.f);
    CHECK(r.length >= 64.0);
}
