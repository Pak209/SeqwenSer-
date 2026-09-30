#pragma once
// The Mangle sound engine: plays a decoded sample (tempo-locked loop or keyboard sampler) through
// gate / step sequencer, pitch, granular, grit, filter, beat repeat, reverb, pan and output stages.
// No JUCE, no locks, no allocation after prepare(). The plug-in feeds it once per host block.

#include <array>
#include <memory>

#include "mangle/Dsp.h"
#include "mangle/Types.h"

namespace mangle {

/** What the engine did in the last block; read by the editor (via atomics in the plug-in). */
struct EngineStatus
{
    double ppq = 0.0;            // song position at the end of the block
    double loopPhase = 0.0;      // 0..1 position inside the sample region (drives the playhead)
    int step = 0;
    bool playing = false;
    float lfo[kNumLfos] = {};    // last LFO values (-1..1)
    float level = 0.f;           // block peak of the output
    bool repeating = false;
};

class Engine
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kSubBlock = 16;

    void prepare(double sampleRate, int maxBlock);
    void reset() noexcept;

    /** Renders `n` frames into outL/outR (overwritten). `sample` may be null / empty (silence). */
    void process(const SampleData* sample, const Settings& s, const Pattern& pat, const HostInfo& host,
                 const MidiEvent* events, int numEvents, float* outL, float* outR, int n) noexcept;

    const EngineStatus& status() const noexcept { return status_; }
    double sampleRate() const noexcept { return sr_; }

    /** Sum of depth * lfo value over every LFO aimed at `t` (unitless, about -1..1 per LFO). */
    static float modSum(const Settings& s, const float* lfoValues, ModTarget t) noexcept;

private:
    struct Voice
    {
        bool active = false, held = false;
        int note = 60;
        float vel = 1.f, env = 0.f;
        double pos = 0.0;
        bool ended = false;
    };
    void handleEvent(const Settings& s, const MidiEvent& e) noexcept;
    void renderSub(const SampleData* d, const Settings& s, const Pattern& pat, double bpm, bool hostPlaying,
                   float* outL, float* outR, int n) noexcept;

    double sr_ = 44100.0;
    int maxBlock_ = 0;
    EngineStatus status_;

    // sources
    std::array<Voice, kVoices> voices_;
    int heldCount_ = 0, lastNote_ = 60;
    float loopGate_ = 0.f;          // transport / midi-loop amplitude (attack / release)
    double ppqNow_ = 0.0;           // running song position (host while playing, own clock otherwise)
    int newestVoice_ = -1;

    // effect blocks
    GrainEngine shifter_, granular_;
    Grit grit_;
    SvFilter filter_;
    BeatRepeat repeat_;
    Reverb reverb_;

    // smoothed values
    Smoother sGrainMix_, sPan_, sOut_, sDryWet_, sRevMix_, sGrit_, sCut_, sReso_, sRepMix_;
    float directBlend_ = 1.f;       // 1 = direct read, 0 = grain shifter

    std::vector<float> tmp_[8];     // scratch (sub-block sized)
    float envState_ = 1.f;          // gate envelope slew
    bool prepared_ = false;
};

} // namespace mangle
