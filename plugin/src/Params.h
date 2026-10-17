#pragma once
// All automatable parameters and the read-out of them into the DSP core's Settings / Pattern.
// IDs are stable (they are stored in Logic projects): never rename, only add.

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "mangle/Types.h"

namespace mangle::params {

// ---- ids -------------------------------------------------------------------------------------------
inline constexpr const char* mode = "mode";
inline constexpr const char* loopBars = "loop_bars";
inline constexpr const char* rootNote = "root_note";
inline constexpr const char* oneShot = "one_shot";
inline constexpr const char* sampleStart = "sample_start";
inline constexpr const char* sampleEnd = "sample_end";
inline constexpr const char* dryWet = "dry_wet";
inline constexpr const char* outDb = "out_db";
inline constexpr const char* pan = "pan";
inline constexpr const char* swing = "swing";
inline constexpr const char* pitch = "pitch";
inline constexpr const char* fine = "fine";
inline constexpr const char* release = "release";
inline constexpr const char* seqOn = "seq_on";
inline constexpr const char* seqRate = "seq_rate";
inline constexpr const char* seqLength = "seq_length";
inline constexpr const char* gateShape = "gate_shape";
inline constexpr const char* gateEdges = "gate_edges";
inline constexpr const char* gateDepth = "gate_depth";
inline constexpr const char* filterType = "filter_type";
inline constexpr const char* filterFreq = "filter_freq";
inline constexpr const char* filterReso = "filter_reso";
inline constexpr const char* grit = "grit";
inline constexpr const char* grainMix = "grain_mix";
inline constexpr const char* grainPitch = "grain_pitch";
inline constexpr const char* grainScatter = "grain_scatter";
inline constexpr const char* grainSize = "grain_size";
inline constexpr const char* grainDensity = "grain_density";
inline constexpr const char* repeatDiv = "repeat_div";
inline constexpr const char* repeatMix = "repeat_mix";
inline constexpr const char* repeatGate = "repeat_gate";
inline constexpr const char* revSize = "rev_size";
inline constexpr const char* revDamp = "rev_damp";
inline constexpr const char* revMix = "rev_mix";
inline constexpr const char* fallbackBpm = "fallback_bpm";

/** "lfo1_shape", "lfo1_rate", "lfo1_depth", "lfo1_target" (slot 0..5). */
juce::String lfoId(int slot, const char* what);
/** "seq_gate_01" ... lane 0..3, step 0..15. */
juce::String stepId(int lane, int step);

extern const juce::StringArray kModeNames, kRateNames, kFilterNames, kRepeatNames, kLfoShapeNames, kLfoRateNames, kTargetNames;

/** Text of a sequencer step value for its lane ("+5 st", "1/16", "70 %"). */
juce::String stepText(int lane, float v);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Cached raw-value pointers (built once; reading is lock-free and allocation-free). */
class Refs
{
public:
    explicit Refs(juce::AudioProcessorValueTreeState& apvts);
    /** Audio-thread safe. */
    void read(Settings& s, Pattern& p) const noexcept;
    float get(const char* id) const;   // message thread convenience

private:
    struct Lfo { std::atomic<float>* shape, *rate, *depth, *target; };
    std::atomic<float> *mode_, *bars_, *root_, *oneShot_, *sStart_, *sEnd_, *dryWet_, *out_, *pan_, *swing_, *pitch_, *fine_,
        *release_, *seqOn_, *seqRate_, *seqLen_, *gShape_, *gEdges_, *gDepth_, *fType_, *fFreq_, *fReso_, *grit_, *grMix_,
        *grPitch_, *grScatter_, *grSize_, *grDens_, *repDiv_, *repMix_, *repGate_, *rvSize_, *rvDamp_, *rvMix_, *fbBpm_;
    std::array<Lfo, kNumLfos> lfo_ {};
    std::array<std::array<std::atomic<float>*, kMaxSteps>, kNumLanes> steps_ {};
    juce::AudioProcessorValueTreeState& apvts_;
};

} // namespace mangle::params
