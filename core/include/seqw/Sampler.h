#pragma once
// Seqwenser's sampler engine: slice playback driven by a 16-step pattern with per-step parameter locks,
// six global macro effects (pitch, grain, repeat, filter, space, drive) built from the shared DSP blocks.
// No JUCE, no locks on the audio thread, no allocation after prepare().

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "mangle/Dsp.h"
#include "mangle/Types.h"
#include "seqw_state.h"

namespace seqw {

using mangle::SampleData;

/** Default state: empty pattern, 120 bpm, neutral effects. */
SqState defaultState();

/** Transient-based slicing inside [trimStart, trimEnd] (normalised). Fills edges (count+1 values, normalised to the
    whole sample); returns the slice count (>= 1, <= maxSlices). sensitivity 0..1: higher finds more onsets.
    Falls back to equal slices when the audio has too few clear onsets. */
int autoSlice(const SampleData& d, float sensitivity, float trimStart, float trimEnd, float* edges, int maxSlices);
/** Equal slices in the trim region. */
int equalSlices(float trimStart, float trimEnd, int count, float* edges);

/** Built-in synthetic two-bar break at 120 bpm (kick, snare, hats, bass, stab): "KIT_01 Neon Break". */
std::shared_ptr<SampleData> makeDemoBreak(double sampleRate);

/** Beats per step for a rate index. */
double stepBeats(int rate);

class Sampler
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kSub = 16;

    void prepare(double sampleRate, int maxBlock);
    void reset() noexcept;                       // stop, clear voices and effect tails

    // ---- control (any thread) ----
    void setState(const SqState& s);             // brief spin lock; the audio thread never waits for it
    void setSample(std::shared_ptr<const SampleData> d);   // message thread; keeps the old one alive until it is safe
    std::shared_ptr<const SampleData> sample() const { return current_; }   // message thread only
    void setPlaying(bool on) noexcept { playReq_.store(on ? 1 : 0); }
    void triggerPad(int slice, float velocity) noexcept;   // performance pads
    void setSeed(uint32_t seed) noexcept;

    // ---- audio thread ----
    void render(float* outL, float* outR, int n) noexcept;

    SqStatus status() const noexcept;
    double sampleRate() const noexcept { return sr_; }

private:
    struct Voice
    {
        bool active = false, releasing = false, seq = false, reverse = false, lofi = false, stretch = false;
        double pos = 0, start = 0, end = 1, stretchInc = 1;
        float env = 0, vel = 1;
        int gateLeft = 0;                // frames until the release starts (<0: play to the end of the slice)
        int holdCount = 0;
        float holdL = 0, holdR = 0;
    };
    struct Pad { int slice; float vel; };

    void applyPending() noexcept;
    void startStep(const SampleData* d) noexcept;
    void trigger(const SampleData* d, int slice, float vel, const SqStep* step, bool fromSeq) noexcept;
    void renderSegment(const SampleData* d, float* outL, float* outR, int n) noexcept;
    bool sliceRange(const SampleData* d, int slice, double& a, double& b) const noexcept;
    float uni() noexcept { return rng_.uni(); }
    void unusedGuard() noexcept {}

    double sr_ = 44100.0;
    int maxBlock_ = 0;
    bool prepared_ = false;

    // state hand-off
    SqState staged_ {}, live_ {};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> dirty_ { false };
    std::atomic<int> playReq_ { -1 };
    std::array<Pad, 16> pads_ {};
    std::atomic<unsigned> padW_ { 0 }, padR_ { 0 };

    // sample hand-off (hazard pointer)
    std::shared_ptr<const SampleData> current_;
    std::vector<std::shared_ptr<const SampleData>> retired_;
    std::atomic<const SampleData*> active_ { nullptr };
    std::atomic<const SampleData*> inUse_ { nullptr };
    void purgeRetired();
public:
    /** Frees replaced samples once the audio thread no longer reads them (call from the message thread now and then). */
    void collectGarbage();
private:
    double stepFrames() const noexcept;
    float eff(int fx, int k) const noexcept;

    // sequencer
    bool playing_ = false;
    double countdown_ = 0.0;          // frames until the next step
    int stepIdx_ = 0, dir_ = 1, pass_ = 0, stepsThisPass_ = 0;
    long long stepCounter_ = 0;
    int lastStep_ = 0;
    SqStep curStep_ {};               // the step that sounded last (its parameter locks apply until the next step)
    double anchor_ = 0.0, anchorInc_ = 1.0;
    std::array<Voice, kVoices> voices_ {};
    int newest_ = -1;

    // effects
    mangle::GrainEngine granular_, stretcher_;
    mangle::Grit grit_;
    mangle::SvFilter filter_;
    mangle::BeatRepeat repeat_;
    mangle::Reverb reverb_;
    mangle::Rng rng_;
    mangle::Smoother sGrain_, sScatter_, sDrive_, sTone_, sSpace_, sSize_, sCut_, sReso_, sFormant_, sPitchBus_, sVol_, sPan_, sRepMix_;
    float formantLp_[2] = { 0, 0 }, toneLp_[2] = { 0, 0 };
    std::vector<float> tmp_[4];
    std::atomic<int> statStep_ { 0 }, statPlaying_ { 0 }, statPass_ { 0 }, statVoices_ { 0 };
    std::atomic<float> statPlayhead_ { -1.f }, statLevel_ { 0.f };
};

} // namespace seqw
