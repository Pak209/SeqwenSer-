#include "Presets.h"

#include "Params.h"
#include "mangle/Types.h"

namespace mangle::presets {

using namespace juce;
namespace P = mangle::params;

namespace {

struct Kv { String id; float value; };   // value in the parameter's own units (Hz, st, 0..1 ...)

void setParam(AudioProcessorValueTreeState& a, const String& id, float value)
{
    if (auto* p = a.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

bool keepOnReset(const String& id)
{
    return id == P::loopBars || id == P::mode || id == P::rootNote || id == P::sampleStart || id == P::sampleEnd || id == P::fallbackBpm;
}

void setPattern(AudioProcessorValueTreeState& a, int lane, std::initializer_list<float> v)
{
    int i = 0;
    for (float x : v)
    {
        if (i >= kMaxSteps) break;
        setParam(a, P::stepId(lane, i++), x);
    }
}

struct Preset { const char* name; std::function<void(AudioProcessorValueTreeState&)> fn; };

const std::vector<Preset>& all()
{
    static const std::vector<Preset> p = {
        { "Init", [](auto&) {} },
        { "Stutter Gate", [](auto& a) {
            setPattern(a, LaneGate, { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 });
            setParam(a, P::seqRate, 2); setParam(a, P::gateEdges, 0.1f); setParam(a, P::gateShape, 0.35f); } },
        { "Half-Time Crush", [](auto& a) {
            setParam(a, P::grit, 0.7f); setParam(a, P::filterFreq, 3200); setParam(a, P::filterReso, 0.35f);
            setPattern(a, LaneGate, { 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0 });
            setParam(a, P::seqRate, 1); setParam(a, P::gateEdges, 0.5f); } },
        { "Glitch Repeater", [](auto& a) {
            setParam(a, P::repeatDiv, 3); setParam(a, P::repeatGate, 0.8f);
            setPattern(a, LaneRepeat, { 0, 0, 0, 0, 0, 0, 1.f/6*2, 0, 0, 0, 0, 0, 1.f/6*3, 1.f/6*3, 1.f/6*4, 1.f/6*4 });
            setParam(a, P::seqRate, 2); } },
        { "Granular Cloud", [](auto& a) {
            setParam(a, P::grainMix, 0.85f); setParam(a, P::grainScatter, 0.75f); setParam(a, P::grainSize, 140);
            setParam(a, P::grainPitch, 7); setParam(a, P::revMix, 0.35f); setParam(a, P::revSize, 0.8f);
            setParam(a, P::lfoId(0,"target"), 4); } },
        { "Dub Wash", [](auto& a) {
            setParam(a, P::revMix, 0.55f); setParam(a, P::revSize, 0.9f); setParam(a, P::revDamp, 0.65f);
            setParam(a, P::filterFreq, 1400); setParam(a, P::filterReso, 0.45f); setParam(a, P::dryWet, 0.8f);
            setPattern(a, LaneFilter, { 0, 0, .25f, .5f, 0, 0, .5f, .75f, 0, 0, .25f, .5f, .75f, 1, 0, 0 }); } },
        { "Telephone", [](auto& a) {
            setParam(a, P::filterType, 1); setParam(a, P::filterFreq, 900); setParam(a, P::filterReso, 0.5f);
            setParam(a, P::grit, 0.4f); setParam(a, P::outDb, 2); } },
        { "Pitch Ladder", [](auto& a) {
            setPattern(a, LanePitch, { 0, 0, 3.f/12, 3.f/12, 5.f/12, 5.f/12, 7.f/12, 7.f/12, 0, 0, -2.f/12, -2.f/12, 5.f/12, 5.f/12, 12.f/12, 7.f/12 });
            setParam(a, P::seqRate, 1); setParam(a, P::gateEdges, 0.2f); } },
        { "Wobble", [](auto& a) {
            setParam(a, P::filterFreq, 900); setParam(a, P::filterReso, 0.55f);
            setParam(a, P::lfoId(0,"shape"), 0); setParam(a, P::lfoId(0,"rate"), 5); setParam(a, P::lfoId(0,"depth"), 0.8f); setParam(a, P::lfoId(0,"target"), 2);
            setParam(a, P::lfoId(1,"shape"), 1); setParam(a, P::lfoId(1,"rate"), 4); setParam(a, P::lfoId(1,"depth"), 0.5f); setParam(a, P::lfoId(1,"target"), 7); } },
        { "Swung Chops", [](auto& a) {
            setParam(a, P::swing, 0.6f); setParam(a, P::seqRate, 2);
            setPattern(a, LaneGate, { 1, 0.6f, 1, 0.3f, 0, 1, 0.6f, 1, 1, 0.6f, 0, 0.3f, 1, 0, 0.6f, 1 });
            setParam(a, P::gateEdges, 0.15f); } },
    };
    return p;
}

} // namespace

int count() { return static_cast<int>(all().size()); }
String name(int i) { return all()[static_cast<size_t>(jlimit(0, count() - 1, i))].name; }

void resetAll(AudioProcessorValueTreeState& a)
{
    for (auto* p : a.processor.getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*>(p))
        {
            if (keepOnReset(r->paramID)) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost(r->getDefaultValue());
            p->endChangeGesture();
        }
}

void apply(AudioProcessorValueTreeState& a, int index)
{
    resetAll(a);
    all()[static_cast<size_t>(jlimit(0, count() - 1, index))].fn(a);
}

void randomize(AudioProcessorValueTreeState& a, Random& rng)
{
    auto u = [&]() { return rng.nextFloat(); };
    // Steps
    for (int s = 0; s < kMaxSteps; ++s)
    {
        setParam(a, P::stepId(LaneGate, s), u() < 0.72f ? (u() < 0.75f ? 1.f : 0.5f) : 0.f);
        setParam(a, P::stepId(LanePitch, s), u() < 0.3f ? static_cast<float>(rng.nextInt(9) - 3) / 12.f : 0.f);
        setParam(a, P::stepId(LaneFilter, s), u() < 0.25f ? u() : 0.f);
        setParam(a, P::stepId(LaneRepeat, s), u() < 0.15f ? static_cast<float>(1 + rng.nextInt(6)) / 6.f : 0.f);
    }
    setParam(a, P::seqOn, 1);
    setParam(a, P::seqRate, static_cast<float>(1 + rng.nextInt(2)));
    setParam(a, P::gateShape, u());
    setParam(a, P::gateEdges, 0.05f + 0.5f * u());
    setParam(a, P::gateDepth, 0.6f + 0.4f * u());
    setParam(a, P::swing, u() < 0.5f ? 0.f : 0.5f * u());
    setParam(a, P::filterType, u() < 0.75f ? 0.f : 1.f);
    setParam(a, P::filterFreq, 400.f * std::pow(45.f, u()));
    setParam(a, P::filterReso, 0.5f * u());
    setParam(a, P::grit, u() < 0.5f ? 0.f : 0.6f * u());
    setParam(a, P::grainMix, u() < 0.5f ? 0.f : 0.2f + 0.6f * u());
    setParam(a, P::grainScatter, u());
    setParam(a, P::grainPitch, static_cast<float>(rng.nextInt(5) * 2 - 4) + (u() < 0.5f ? 0.f : 7.f));
    setParam(a, P::grainSize, 40.f + 160.f * u());
    setParam(a, P::repeatDiv, u() < 0.5f ? 0.f : static_cast<float>(1 + rng.nextInt(6)));
    setParam(a, P::revMix, u() < 0.5f ? 0.f : 0.4f * u());
    setParam(a, P::revSize, 0.3f + 0.6f * u());
    setParam(a, P::pitch, u() < 0.6f ? 0.f : static_cast<float>(rng.nextInt(9) - 4));
    for (int i = 0; i < kNumLfos; ++i)
    {
        const bool on = i < 3 && u() < 0.6f;
        setParam(a, P::lfoId(i, "shape"), static_cast<float>(rng.nextInt(6)));
        setParam(a, P::lfoId(i, "rate"), static_cast<float>(2 + rng.nextInt(5)));
        setParam(a, P::lfoId(i, "depth"), on ? (u() - 0.5f) * 1.4f : 0.f);
        setParam(a, P::lfoId(i, "target"), on ? static_cast<float>(1 + rng.nextInt(static_cast<int>(ModTarget::Count) - 1)) : 0.f);
    }
}

} // namespace mangle::presets
