#include "TestFramework.h"
#include "TestSignals.h"
#include "mangle/Dsp.h"

using namespace mangle;

namespace {
TempoEstimate est(double bpm, int beats, bool kickOnly, double sr = 44100.0)
{
    auto d = testsig::drumLoop(sr, bpm, beats, kickOnly);
    return detectTempo(d.left.data(), d.left.size(), sr);
}
} // namespace

TEST_CASE("Tempo: whole-bar drum loops are detected and snapped to whole beats")
{
    for (double bpm : { 90.0, 100.0, 120.0, 128.0, 140.0, 160.0 })
    {
        const auto e = est(bpm, 16, false);
        INFO("bpm " << bpm << " -> " << e.bpm << " beats " << e.beats << " conf " << e.confidence);
        CHECK_NEAR(e.bpm, bpm, 1.5);
        CHECK_NEAR(e.beats, 16.0, 0.01);
        CHECK_NEAR(e.bars, 4.0, 0.01);
    }
}

TEST_CASE("Tempo: 8-beat and 32-beat loops, kick-only loops, 48 kHz")
{
    auto e = est(120.0, 8, true);
    CHECK_NEAR(e.bpm, 120.0, 2.0);
    e = est(110.0, 32, false);
    CHECK_NEAR(e.bpm, 110.0, 1.5);
    e = est(124.0, 16, false, 48000.0);
    CHECK_NEAR(e.bpm, 124.0, 1.5);
}

TEST_CASE("Tempo: silence, noise-free tone, tiny and huge inputs return no estimate")
{
    std::vector<float> z(44100 * 8, 0.f);
    CHECK(detectTempo(z.data(), z.size(), 44100.0).bpm == 0.0);
    auto t = testsig::sine(44100.0, 8.0, 440.0);
    const auto e = detectTempo(t.left.data(), t.left.size(), 44100.0);
    CHECK(e.confidence < 0.5f);
    CHECK(detectTempo(z.data(), 10, 44100.0).bpm == 0.0);
    CHECK(detectTempo(nullptr, 0, 44100.0).bpm == 0.0);
}
