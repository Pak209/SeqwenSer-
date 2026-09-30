#include "Params.h"

namespace mangle::params {

using namespace juce;

const StringArray kModeNames { "Loop (host tempo)", "Loop on MIDI note", "Keys (sampler)" };
const StringArray kRateNames { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
const StringArray kFilterNames { "Low-pass", "High-pass" };
const StringArray kRepeatNames { "Off", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
const StringArray kLfoShapeNames { "Sine", "Triangle", "Saw", "Square", "Random steps", "Random smooth" };
const StringArray kLfoRateNames { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" };
const StringArray kTargetNames { "Off", "Pitch", "Filter freq", "Filter reso", "Grain scatter", "Grain pitch", "Grain mix", "Pan",
                                 "Gate depth", "Grit", "Reverb mix", "Repeat mix", "Out level", "Dry/wet" };

String lfoId(int slot, const char* what) { return "lfo" + String(slot + 1) + "_" + what; }
String stepId(int lane, int step)
{
    static const char* names[] = { "gate", "pitch", "filter", "repeat" };
    return String("seq_") + names[lane] + "_" + String(step + 1).paddedLeft('0', 2);
}

String stepText(int lane, float v)
{
    switch (lane)
    {
        case LaneGate:   return v <= 0.001f ? String("off") : String(roundToInt(v * 100.f)) + " %";
        case LanePitch:  { const int st = lanePitchSemis(v); return (st > 0 ? "+" : "") + String(st) + " st"; }
        case LaneFilter: return v <= 0.001f ? String("0") : "+" + String(v * 4.f, 1) + " oct";
        case LaneRepeat: return kRepeatNames[laneRepeatIndex(v)];
        default:         return {};
    }
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 1;
    auto pct = [](float x, int) { return String(roundToInt(x * 100.f)) + " %"; };
    auto pctIn = [](const String& t) { return t.getFloatValue() * 0.01f; };
    auto add = [&layout](auto p) { layout.add(std::move(p)); };
    auto fl = [&](const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a = {}) {
        add(std::make_unique<AudioParameterFloat>(ParameterID { id, v }, name, r, def, a));
    };
    auto pctParam = [&](const char* id, const char* name, float def) {
        fl(id, name, { 0.f, 1.f, 0.001f }, def, AudioParameterFloatAttributes().withStringFromValueFunction(pct).withValueFromStringFunction(pctIn));
    };

    add(std::make_unique<AudioParameterChoice>(ParameterID { mode, v }, "Play mode", kModeNames, 0));
    fl(loopBars, "Loop length", { 0.25f, 64.f, 0.25f }, 4.f, AudioParameterFloatAttributes().withLabel("bars"));
    add(std::make_unique<AudioParameterInt>(ParameterID { rootNote, v }, "Root note", 0, 127, 60, AudioParameterIntAttributes().withStringFromValueFunction([](int x, int) { return MidiMessage::getMidiNoteName(x, true, true, 3); })));
    add(std::make_unique<AudioParameterBool>(ParameterID { oneShot, v }, "One shot", false));
    pctParam(sampleStart, "Sample start", 0.f);
    pctParam(sampleEnd, "Sample end", 1.f);
    pctParam(dryWet, "Dry/Wet", 1.f);
    fl(outDb, "Out", { -48.f, 12.f, 0.1f, 2.2f }, 0.f, AudioParameterFloatAttributes().withLabel("dB"));
    fl(pan, "Pan", { -1.f, 1.f, 0.001f }, 0.f,
       AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) {
           return std::abs(x) < 0.005f ? String("C") : (x < 0 ? "L" : "R") + String(roundToInt(std::abs(x) * 100.f)); }));
    pctParam(swing, "Swing", 0.f);
    fl(pitch, "Pitch", { -24.f, 24.f, 0.01f }, 0.f, AudioParameterFloatAttributes().withLabel("st"));
    fl(fine, "Fine tune", { -100.f, 100.f, 0.1f }, 0.f, AudioParameterFloatAttributes().withLabel("ct"));
    fl(release, "Release", { 0.f, 3000.f, 1.f, 0.4f }, 120.f, AudioParameterFloatAttributes().withLabel("ms"));
    add(std::make_unique<AudioParameterBool>(ParameterID { seqOn, v }, "Sequencer on", true));
    add(std::make_unique<AudioParameterChoice>(ParameterID { seqRate, v }, "Sequencer rate", kRateNames, 1));
    add(std::make_unique<AudioParameterInt>(ParameterID { seqLength, v }, "Sequencer length", 1, kMaxSteps, kMaxSteps));
    pctParam(gateShape, "Gate shape", 0.5f);
    pctParam(gateEdges, "Gate edges", 0.3f);
    pctParam(gateDepth, "Gate depth", 1.f);
    add(std::make_unique<AudioParameterChoice>(ParameterID { filterType, v }, "Filter type", kFilterNames, 0));
    fl(filterFreq, "Filter freq", { 20.f, 20000.f, 1.f, 0.3f }, 18000.f, AudioParameterFloatAttributes().withLabel("Hz"));
    pctParam(filterReso, "Filter reso", 0.2f);
    pctParam(grit, "Grit", 0.f);
    pctParam(grainMix, "Grain mix", 0.f);
    fl(grainPitch, "Grain pitch", { -24.f, 24.f, 0.01f }, 0.f, AudioParameterFloatAttributes().withLabel("st"));
    pctParam(grainScatter, "Grain scatter", 0.3f);
    fl(grainSize, "Grain size", { 10.f, 400.f, 1.f, 0.5f }, 80.f, AudioParameterFloatAttributes().withLabel("ms"));
    fl(grainDensity, "Grain density", { 1.f, 12.f, 0.1f }, 4.f);
    add(std::make_unique<AudioParameterChoice>(ParameterID { repeatDiv, v }, "Beat repeat", kRepeatNames, 0));
    pctParam(repeatMix, "Repeat mix", 1.f);
    pctParam(repeatGate, "Repeat gate", 1.f);
    pctParam(revSize, "Reverb size", 0.5f);
    pctParam(revDamp, "Reverb damp", 0.5f);
    pctParam(revMix, "Reverb mix", 0.f);
    fl(fallbackBpm, "Standalone tempo", { 40.f, 240.f, 0.1f }, 120.f, AudioParameterFloatAttributes().withLabel("bpm"));

    for (int i = 0; i < kNumLfos; ++i)
    {
        const String n = "LFO " + String(i + 1) + " ";
        add(std::make_unique<AudioParameterChoice>(ParameterID { lfoId(i, "shape"), v }, n + "shape", kLfoShapeNames, i % 4));
        add(std::make_unique<AudioParameterChoice>(ParameterID { lfoId(i, "rate"), v }, n + "rate", kLfoRateNames, 3));
        add(std::make_unique<AudioParameterFloat>(ParameterID { lfoId(i, "depth"), v }, n + "depth", NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
        add(std::make_unique<AudioParameterChoice>(ParameterID { lfoId(i, "target"), v }, n + "target", kTargetNames, 0));
    }

    static const char* laneName[] = { "Gate", "Pitch", "Filter", "Repeat" };
    for (int l = 0; l < kNumLanes; ++l)
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const String name = String("Seq ") + laneName[l] + " " + String(s + 1);
            const float def = l == LaneGate ? 1.f : 0.f;
            NormalisableRange<float> r(l == LanePitch ? -1.f : 0.f, 1.f, l == LanePitch ? 1.f / 12.f : l == LaneRepeat ? 1.f / 6.f : 0.001f);
            add(std::make_unique<AudioParameterFloat>(ParameterID { stepId(l, s), v }, name, r, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction([l](float x, int) { return stepText(l, x); })));
        }
    return layout;
}

Refs::Refs(AudioProcessorValueTreeState& a) : apvts_(a)
{
    auto g = [&a](const String& id) { auto* p = a.getRawParameterValue(id); jassert(p != nullptr); return p; };
    mode_ = g(mode); bars_ = g(loopBars); root_ = g(rootNote); oneShot_ = g(oneShot); sStart_ = g(sampleStart); sEnd_ = g(sampleEnd);
    dryWet_ = g(dryWet); out_ = g(outDb); pan_ = g(pan); swing_ = g(swing); pitch_ = g(pitch); fine_ = g(fine); release_ = g(release);
    seqOn_ = g(seqOn); seqRate_ = g(seqRate); seqLen_ = g(seqLength); gShape_ = g(gateShape); gEdges_ = g(gateEdges); gDepth_ = g(gateDepth);
    fType_ = g(filterType); fFreq_ = g(filterFreq); fReso_ = g(filterReso); grit_ = g(grit); grMix_ = g(grainMix); grPitch_ = g(grainPitch);
    grScatter_ = g(grainScatter); grSize_ = g(grainSize); grDens_ = g(grainDensity); repDiv_ = g(repeatDiv); repMix_ = g(repeatMix);
    repGate_ = g(repeatGate); rvSize_ = g(revSize); rvDamp_ = g(revDamp); rvMix_ = g(revMix); fbBpm_ = g(fallbackBpm);
    for (int i = 0; i < kNumLfos; ++i)
        lfo_[static_cast<size_t>(i)] = { g(lfoId(i, "shape")), g(lfoId(i, "rate")), g(lfoId(i, "depth")), g(lfoId(i, "target")) };
    for (int l = 0; l < kNumLanes; ++l)
        for (int s = 0; s < kMaxSteps; ++s) steps_[static_cast<size_t>(l)][static_cast<size_t>(s)] = g(stepId(l, s));
}

float Refs::get(const char* id) const { return apvts_.getRawParameterValue(id)->load(); }

void Refs::read(Settings& s, Pattern& p) const noexcept
{
    auto ri = [](std::atomic<float>* a) { return static_cast<int>(std::lround(a->load())); };
    s.mode = ri(mode_);
    s.loopBeats = bars_->load() * 4.f;
    s.rootNote = ri(root_);
    s.oneShot = oneShot_->load() >= 0.5f;
    s.sampleStart = sStart_->load();
    s.sampleEnd = sEnd_->load();
    s.dryWet = dryWet_->load();
    s.outDb = out_->load();
    s.pan = pan_->load();
    s.swing = swing_->load();
    s.semis = pitch_->load();
    s.fineCents = fine_->load();
    s.releaseMs = release_->load();
    s.seqOn = seqOn_->load() >= 0.5f;
    s.seqRate = ri(seqRate_);
    s.gateShape = gShape_->load();
    s.gateEdges = gEdges_->load();
    s.gateDepth = gDepth_->load();
    s.filterType = ri(fType_);
    s.filterFreq = fFreq_->load();
    s.filterReso = fReso_->load();
    s.grit = grit_->load();
    s.grainMix = grMix_->load();
    s.grainPitch = grPitch_->load();
    s.grainScatter = grScatter_->load();
    s.grainSizeMs = grSize_->load();
    s.grainDensity = grDens_->load();
    s.repeatDiv = ri(repDiv_);
    s.repeatMix = repMix_->load();
    s.repeatGate = repGate_->load();
    s.revSize = rvSize_->load();
    s.revDamp = rvDamp_->load();
    s.revMix = rvMix_->load();
    s.fallbackBpm = static_cast<double>(fbBpm_->load());
    for (size_t i = 0; i < static_cast<size_t>(kNumLfos); ++i)
    {
        s.lfo[i].shape = ri(lfo_[i].shape);
        s.lfo[i].rate = ri(lfo_[i].rate);
        s.lfo[i].depth = lfo_[i].depth->load();
        s.lfo[i].target = ri(lfo_[i].target);
    }
    p.length = ri(seqLen_);
    for (size_t l = 0; l < static_cast<size_t>(kNumLanes); ++l)
        for (size_t st = 0; st < static_cast<size_t>(kMaxSteps); ++st) p.v[l][st] = steps_[l][st]->load();
}

} // namespace mangle::params
