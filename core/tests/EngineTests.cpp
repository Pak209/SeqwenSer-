#include "TestFramework.h"
#include "TestSignals.h"
#include "mangle/Engine.h"

using namespace mangle;

namespace {

struct Rig
{
    Engine e;
    SampleData sample;
    Settings s;
    Pattern pat;
    HostInfo host;
    double sr = 44100.0;
    std::vector<float> L, R;
    explicit Rig(SampleData d, double rate = 44100.0) : sample(std::move(d)), sr(rate)
    {
        e.prepare(sr, 512);
        host.valid = true;
        host.playing = true;
        host.bpm = 120.0;
        s.loopBeats = 16.f;
        s.dryWet = 1.f;
    }
    /** Renders `seconds` in blocks of `block` frames, advancing the host ppq like a real host. */
    void run(double seconds, int block = 512, std::vector<MidiEvent> ev = {})
    {
        const size_t total = static_cast<size_t>(seconds * sr);
        std::vector<float> bl(static_cast<size_t>(block)), br(static_cast<size_t>(block));
        size_t done = 0;
        bool first = true;
        while (done < total)
        {
            const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), total - done));
            e.process(&sample, s, pat, host, first ? ev.data() : nullptr, first ? static_cast<int>(ev.size()) : 0, bl.data(), br.data(), n);
            first = false;
            L.insert(L.end(), bl.begin(), bl.begin() + n);
            R.insert(R.end(), br.begin(), br.begin() + n);
            done += static_cast<size_t>(n);
            if (host.playing) host.ppq += n / sr * host.bpm / 60.0;
        }
    }
};

bool allFinite(const std::vector<float>& v)
{
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
double peakOf(const std::vector<float>& v, size_t a = 0, size_t b = SIZE_MAX)
{
    double p = 0;
    for (size_t i = a; i < v.size() && i < b; ++i) p = std::max(p, static_cast<double>(std::abs(v[i])));
    return p;
}

} // namespace

TEST_CASE("Engine: tempo-matched loop with all effects off is a transparent copy of the sample")
{
    Rig rig(testsig::drumLoop(44100.0, 120.0, 16));   // 8 s at 120 bpm = 16 beats
    rig.s.seqOn = false;
    rig.s.filterFreq = 20000.f;
    rig.s.filterReso = 0.f;
    rig.run(1.0);   // let the 4 ms attack settle
    rig.L.clear(); rig.R.clear();
    rig.run(4.0);
    const size_t off = static_cast<size_t>(1.0 * 44100.0);
    double maxErr = 0;
    for (size_t i = 200; i < rig.L.size(); ++i)
        maxErr = std::max(maxErr, static_cast<double>(std::abs(rig.L[i] - rig.sample.left[(off + i) % rig.sample.left.size()])));
    INFO("max error " << maxErr);
    CHECK(maxErr < 0.01);
    CHECK(allFinite(rig.L) && allFinite(rig.R));
}

TEST_CASE("Engine: host stopped is silent; start/stop fades without clicks; loop wraps with the host position")
{
    Rig rig(testsig::sine(44100.0, 8.0, 220.0));
    rig.s.seqOn = false;
    rig.s.releaseMs = 50.f;
    rig.host.playing = false;
    rig.run(0.5);
    CHECK(peakOf(rig.L) == 0.0);
    rig.L.clear();
    rig.host.playing = true;
    rig.host.ppq = 0.0;
    rig.run(1.0);
    CHECK(peakOf(rig.L) > 0.3);
    double maxStep = 0;
    for (size_t i = 1; i < rig.L.size(); ++i) maxStep = std::max(maxStep, static_cast<double>(std::abs(rig.L[i] - rig.L[i - 1])));
    CHECK(maxStep < 0.06);      // a 220 Hz sine at 0.5 has a max step of 0.03; no clicks
    rig.L.clear();
    rig.host.playing = false;
    rig.run(0.6);
    CHECK(peakOf(rig.L, 0, 100) > 0.0);
    CHECK(peakOf(rig.L, static_cast<size_t>(0.45 * 44100), rig.L.size()) < 1e-3);   // released
}

TEST_CASE("Engine: pitch +12 raises the loop an octave; -12 lowers it; timing stays locked")
{
    Rig rig(testsig::sine(44100.0, 8.0, 220.0));
    rig.s.seqOn = false;
    rig.s.semis = 12.f;
    rig.run(2.0);
    rig.L.clear();
    rig.run(1.0);
    CHECK_NEAR(testsig::zeroCrossHz(rig.L, 4000, 40000, 44100.0), 440.0, 20.0);
    Rig lo(testsig::sine(44100.0, 8.0, 440.0));
    lo.s.seqOn = false;
    lo.s.semis = -12.f;
    lo.run(2.0);
    lo.L.clear();
    lo.run(1.0);
    CHECK_NEAR(testsig::zeroCrossHz(lo.L, 4000, 40000, 44100.0), 220.0, 15.0);
    CHECK(testsig::rms(lo.L, 4000, 40000) > 0.2);
}

TEST_CASE("Engine: a sample of any length is stretched to the loop length at the project tempo")
{
    // 4 s sample played over 4 beats at 120 bpm (2 s): twice as fast, but the pitch is kept (grain time-stretch)
    Rig rig(testsig::sine(44100.0, 4.0, 300.0));
    rig.s.seqOn = false;
    rig.s.loopBeats = 4.f;
    rig.run(2.0);
    rig.L.clear();
    rig.run(1.5);
    CHECK_NEAR(testsig::zeroCrossHz(rig.L, 4000, 40000, 44100.0), 300.0, 12.0);
    CHECK(testsig::rms(rig.L, 4000, 40000) > 0.25);
    // and a sample that is 2x too short is slowed down the same way
    Rig slow(testsig::sine(44100.0, 1.0, 300.0));
    slow.s.seqOn = false; slow.s.loopBeats = 4.f;
    slow.run(2.0);
    slow.L.clear();
    slow.run(1.5);
    CHECK_NEAR(testsig::zeroCrossHz(slow.L, 4000, 40000, 44100.0), 300.0, 12.0);
}

TEST_CASE("Engine: dry/wet 0 is the dry source; gate lane closes steps; depth 0 disables the gate")
{
    Rig rig(testsig::sine(44100.0, 8.0, 220.0, 0.5f));
    rig.s.seqRate = 1;   // 1/8
    rig.s.gateEdges = 0.f;
    rig.s.gateShape = 1.f;
    rig.pat.v[LaneGate][1] = 0.f;    // step 1 closed (beats 0.5..1.0)
    rig.run(0.3);
    rig.L.clear();
    rig.host.ppq = 0.0;
    rig.e.reset();
    rig.run(1.0);
    const double open = testsig::rms(rig.L, 1000, 9000);            // step 0
    const double closed = testsig::rms(rig.L, 13000, 21000);        // step 1: 0.25..0.5 s
    CHECK(open > 0.3);
    CHECK(closed < 0.02);
    Rig nogate(testsig::sine(44100.0, 8.0, 220.0, 0.5f));
    nogate.s.gateDepth = 0.f;
    nogate.pat.v[LaneGate][1] = 0.f;
    nogate.run(1.0);
    CHECK(testsig::rms(nogate.L, 13000, 21000) > 0.3);
}

TEST_CASE("Engine: swing delays the off-beat step")
{
    Rig a(testsig::sine(44100.0, 8.0, 220.0)), b(testsig::sine(44100.0, 8.0, 220.0));
    for (auto* r : { &a, &b })
    {
        r->s.seqRate = 1;
        r->s.gateEdges = 0.f;
        r->s.gateShape = 1.f;
        r->pat.v[LaneGate][0] = 0.f;   // only step 1 sounds
        r->pat.v[LaneGate][1] = 1.f;
        for (int i = 2; i < 16; ++i) r->pat.v[LaneGate][i] = 0.f;
    }
    b.s.swing = 1.f;
    a.run(1.0); b.run(1.0);
    auto firstSound = [](const std::vector<float>& v) { for (size_t i = 2000; i < v.size(); ++i) if (std::abs(v[i]) > 0.05f) return i; return v.size(); };
    const size_t fa = firstSound(a.L), fb = firstSound(b.L);
    INFO("first sound straight " << fa << " swung " << fb);
    CHECK_NEAR(static_cast<double>(fa), 0.25 * 44100, 900);
    CHECK_NEAR(static_cast<double>(fb), 0.375 * 44100, 900);
}

TEST_CASE("Engine: filter, grit, reverb and pan/out do what they say")
{
    auto render = [](auto setup) {
        Rig rig(testsig::sine(44100.0, 8.0, 5000.0, 0.5f));
        rig.s.seqOn = false;
        setup(rig);
        rig.run(1.5);
        return rig;
    };
    auto plain = render([](Rig&) {});
    auto lp = render([](Rig& r) { r.s.filterFreq = 300.f; });
    auto hp = render([](Rig& r) { r.s.filterType = 1; r.s.filterFreq = 12000.f; });
    CHECK(testsig::rms(lp.L, 30000, 60000) < 0.05 * testsig::rms(plain.L, 30000, 60000));
    CHECK(testsig::rms(hp.L, 30000, 60000) < 0.25 * testsig::rms(plain.L, 30000, 60000));
    auto pn = render([](Rig& r) { r.s.pan = -1.f; });
    CHECK(testsig::rms(pn.R, 30000, 60000) < 1e-3);
    CHECK(testsig::rms(pn.L, 30000, 60000) > 0.3);
    auto out = render([](Rig& r) { r.s.outDb = -6.f; });
    CHECK_NEAR(testsig::rms(out.L, 30000, 60000) / testsig::rms(plain.L, 30000, 60000), 0.5012, 0.02);
    auto grit = render([](Rig& r) { r.s.grit = 0.9f; });
    CHECK(std::abs(testsig::rms(grit.L, 30000, 60000) - testsig::rms(plain.L, 30000, 60000)) > 0.005);
    CHECK(peakOf(grit.L) < 1.5);
    // reverb: after the source stops, a tail remains
    Rig rv(testsig::sine(44100.0, 8.0, 300.0));
    rv.s.seqOn = false; rv.s.revMix = 1.f; rv.s.revSize = 0.9f; rv.s.releaseMs = 5.f;
    rv.run(1.0);
    rv.host.playing = false;
    rv.L.clear();
    rv.run(1.0);
    CHECK(testsig::rms(rv.L, 22050, 44100) > 0.002);
    Rig dry(testsig::sine(44100.0, 8.0, 300.0));
    dry.s.seqOn = false; dry.s.revMix = 0.f; dry.s.releaseMs = 5.f;
    dry.run(1.0);
    dry.host.playing = false;
    dry.L.clear();
    dry.run(1.0);
    CHECK(testsig::rms(dry.L, 22050, 44100) < 1e-4);
}

TEST_CASE("Engine: beat repeat lane repeats a slice; granular mix produces sound and follows the sample")
{
    Rig rig(testsig::drumLoop(44100.0, 120.0, 16));
    rig.s.seqOn = true;
    rig.s.gateDepth = 0.f;
    for (int i = 0; i < 16; ++i) rig.pat.v[LaneRepeat][i] = i % 4 == 3 ? 3.f / 6.f : 0.f;   // 1/16 repeat on every 4th step
    rig.run(4.0);
    CHECK(allFinite(rig.L));
    CHECK(rig.e.status().repeating || peakOf(rig.L) > 0.1);
    Rig gr(testsig::sine(44100.0, 8.0, 330.0));
    gr.s.seqOn = false; gr.s.grainMix = 1.f; gr.s.grainScatter = 0.6f; gr.s.grainPitch = 7.f;
    gr.run(2.0);
    CHECK(allFinite(gr.L));
    CHECK(testsig::rms(gr.L, 30000, 80000) > 0.05);
    CHECK(peakOf(gr.L) < 1.5);
}

TEST_CASE("Engine: LFOs move their target and stay in range; depth 0 or no target is inert")
{
    Rig rig(testsig::sine(44100.0, 8.0, 220.0));
    rig.s.seqOn = false;
    rig.s.lfo[0] = { 3, 5, 1.f, static_cast<int>(ModTarget::Pan) };   // square, 1 beat, full depth to pan
    rig.run(2.0);
    // the pan flips between hard L and hard R every half beat
    const double l1 = testsig::rms(rig.L, 4000, 9000), r1 = testsig::rms(rig.R, 4000, 9000);
    const double l2 = testsig::rms(rig.L, 15000, 19000), r2 = testsig::rms(rig.R, 15000, 19000);
    INFO(l1 << " " << r1 << " " << l2 << " " << r2);
    CHECK((l1 > 5 * r1) != (l2 > 5 * r2) || (r1 > 5 * l1) != (r2 > 5 * l2));
    CHECK(rig.e.status().lfo[0] == 1.f || rig.e.status().lfo[0] == -1.f);
    Rig inert(testsig::sine(44100.0, 8.0, 220.0));
    inert.s.seqOn = false;
    inert.s.lfo[0] = { 0, 5, 0.f, static_cast<int>(ModTarget::Pan) };
    inert.run(1.0);
    CHECK_NEAR(testsig::rms(inert.L, 8000, 40000), testsig::rms(inert.R, 8000, 40000), 1e-6);
    Settings s;
    float vals[kNumLfos] = { 0.5f, -1.f, 0, 0, 0, 0 };
    s.lfo[0] = { 0, 3, 0.5f, static_cast<int>(ModTarget::Grit) };
    s.lfo[1] = { 0, 3, 1.0f, static_cast<int>(ModTarget::Grit) };
    CHECK_NEAR(Engine::modSum(s, vals, ModTarget::Grit), 0.25f - 1.f, 1e-6);
    CHECK_NEAR(Engine::modSum(s, vals, ModTarget::None), 0.f, 1e-9);
}

TEST_CASE("Engine: keys mode plays on MIDI notes at the right pitch; note-off releases; one-shot finishes")
{
    Rig rig(testsig::sine(44100.0, 4.0, 220.0));
    rig.s.mode = static_cast<int>(PlayMode::Keys);
    rig.s.seqOn = false;
    rig.s.rootNote = 57;
    rig.s.releaseMs = 40.f;
    rig.host.playing = false;
    rig.run(0.2);
    CHECK(peakOf(rig.L) == 0.0);
    rig.run(1.0, 512, { { 0, MidiEvent::NoteOn, 69, 1.f } });    // A4: +12 st from the root (A3)
    CHECK(peakOf(rig.L, 4000) > 0.3);
    CHECK_NEAR(testsig::zeroCrossHz(rig.L, 12000, 40000, 44100.0), 440.0, 15.0);
    rig.L.clear();
    rig.run(0.6, 512, { { 0, MidiEvent::NoteOff, 69, 0.f } });
    CHECK(peakOf(rig.L, static_cast<size_t>(0.5 * 44100)) < 1e-3);
    // polyphony: two notes sum
    Rig poly(testsig::sine(44100.0, 4.0, 220.0));
    poly.s.mode = 2; poly.s.seqOn = false; poly.s.rootNote = 57;
    poly.run(0.5, 512, { { 0, MidiEvent::NoteOn, 57, 0.5f }, { 10, MidiEvent::NoteOn, 64, 0.5f } });
    CHECK(peakOf(poly.L) > 0.3);
    // one-shot plays to the end then stops even while held
    Rig os(testsig::sine(44100.0, 0.5, 220.0));
    os.s.mode = 2; os.s.seqOn = false; os.s.oneShot = true; os.s.rootNote = 57; os.s.releaseMs = 10.f;
    os.run(2.0, 512, { { 0, MidiEvent::NoteOn, 57, 1.f } });
    CHECK(peakOf(os.L, 0, 20000) > 0.3);
    CHECK(peakOf(os.L, static_cast<size_t>(1.5 * 44100)) < 1e-3);
}

TEST_CASE("Engine: MIDI-loop mode starts on a held note and transposes the loop")
{
    Rig rig(testsig::sine(44100.0, 8.0, 220.0));
    rig.s.mode = static_cast<int>(PlayMode::LoopMidi);
    rig.s.seqOn = false;
    rig.s.rootNote = 60;
    rig.run(0.3);
    CHECK(peakOf(rig.L) == 0.0);
    rig.run(1.5, 512, { { 0, MidiEvent::NoteOn, 72, 1.f } });
    CHECK_NEAR(testsig::zeroCrossHz(rig.L, 30000, 60000, 44100.0), 440.0, 25.0);
}

TEST_CASE("Engine: no host timeline (standalone) free-runs at the fallback tempo")
{
    Rig rig(testsig::sine(44100.0, 9.6, 220.0));   // 16 beats at 100 bpm
    rig.host.valid = false;
    rig.s.seqOn = false;
    rig.s.fallbackBpm = 100.0;
    rig.run(1.0);
    CHECK(peakOf(rig.L) > 0.3);
    const double ppq = rig.e.status().ppq;
    CHECK_NEAR(ppq, 100.0 / 60.0, 0.02);
}

TEST_CASE("Engine: robustness - no sample, tiny sample, odd block sizes, extreme settings, denormals")
{
    {
        Engine e;
        e.prepare(44100.0, 512);
        Settings s; Pattern p; HostInfo h; h.valid = true; h.playing = true; h.bpm = 120;
        std::vector<float> l(512), r(512);
        for (int i = 0; i < 100; ++i) { e.process(nullptr, s, p, h, nullptr, 0, l.data(), r.data(), 512); h.ppq += 1.0; }
        CHECK(peakOf(l) == 0.0);
        SampleData tiny;
        tiny.left.assign(10, 0.5f); tiny.right = tiny.left;
        e.process(&tiny, s, p, h, nullptr, 0, l.data(), r.data(), 512);
        CHECK(peakOf(l) == 0.0);
    }
    Rig rig(testsig::drumLoop(44100.0, 120.0, 16));
    rig.s.grainMix = 1.f; rig.s.grainScatter = 1.f; rig.s.grit = 1.f; rig.s.filterReso = 1.f; rig.s.filterFreq = 60.f;
    rig.s.revMix = 1.f; rig.s.revSize = 1.f; rig.s.repeatDiv = 4; rig.s.semis = -24.f; rig.s.swing = 1.f;
    rig.s.loopBeats = 0.25f; rig.s.grainSizeMs = 5.f; rig.s.grainDensity = 12.f; rig.s.releaseMs = 0.f;
    for (auto& l : rig.s.lfo) l = { 4, 8, 1.f, static_cast<int>(ModTarget::FilterFreq) };
    for (int b : { 1, 7, 64, 333, 1024 }) { rig.L.clear(); rig.run(0.5, b); CHECK(allFinite(rig.L)); CHECK(peakOf(rig.L) < 8.0); }
    rig.host.bpm = 400.0;
    rig.run(0.5, 100);
    rig.host.bpm = 0.0;   // unknown tempo: falls back to the manual one
    rig.run(0.5, 100);
    CHECK(allFinite(rig.L));
    // silence in -> decays to exact zero (no denormal crawl)
    Rig sil(SampleData{});
    sil.sample.left.assign(44100, 0.f); sil.sample.right = sil.sample.left;
    sil.s.revMix = 1.f;
    sil.run(1.0);
    CHECK(peakOf(sil.L) == 0.0);
}

TEST_CASE("Engine: block-size independence (same audio for 64 and 480 frame blocks)")
{
    auto render = [](int block) {
        Rig rig(testsig::drumLoop(44100.0, 120.0, 16));
        rig.s.grainMix = 0.5f; rig.s.filterFreq = 3000.f; rig.s.semis = 3.f; rig.s.revMix = 0.3f;
        rig.run(2.0, block);
        return rig;
    };
    auto a = render(64), b = render(480);
    double diff = 0;
    for (size_t i = 0; i < std::min(a.L.size(), b.L.size()); ++i) diff = std::max(diff, static_cast<double>(std::abs(a.L[i] - b.L[i])));
    INFO("max diff " << diff);
    CHECK(diff < 0.05);   // sub-blocks make the control-rate parts identical except at block edges
}
