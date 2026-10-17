#pragma once
// Small DSP building blocks. Everything allocates in prepare() only; process functions are
// real-time safe (no locks, no allocation, no exceptions).

#include <array>
#include <cstdint>
#include <vector>

#include "mangle/Types.h"

namespace mangle {

/** xorshift32 noise source. */
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return static_cast<float>(next() >> 8) * (1.f / 16777216.f); }       // 0..1
    float bi() noexcept { return uni() * 2.f - 1.f; }                                              // -1..1
};

/** One-pole parameter smoother (per sub-block). */
struct Smoother
{
    float value = 0.f, target = 0.f;
    void set(float v) noexcept { value = target = v; }
    float step(float coeff) noexcept { value += (target - value) * coeff; return value; }
};
inline float smoothCoeff(double seconds, double updateRate) noexcept
{
    return seconds <= 0.0 ? 1.f : static_cast<float>(1.0 - std::exp(-1.0 / (seconds * updateRate)));
}

// ---- filter --------------------------------------------------------------------------------------
/** Stereo zero-delay-feedback state-variable filter (Zavalishin TPT). */
class SvFilter
{
public:
    void prepare(double sampleRate) noexcept { sr_ = sampleRate; reset(); }
    void reset() noexcept { for (auto& c : ic1_) c = 0.f; for (auto& c : ic2_) c = 0.f; }
    /** cutoff in Hz, reso 0..1 (Q about 0.55 .. 12). */
    void set(float cutoffHz, float reso, bool highPass) noexcept;
    void process(float& l, float& r) noexcept;
    void processSample(int ch, float& x) noexcept;
private:
    double sr_ = 44100.0;
    float g_ = 0.1f, k_ = 1.f, a1_ = 0, a2_ = 0, a3_ = 0;
    bool hp_ = false;
    float ic1_[2] = { 0, 0 }, ic2_[2] = { 0, 0 };
};

// ---- distortion ----------------------------------------------------------------------------------
class Grit
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept { for (auto& z : dcX_) z = 0.f; for (auto& z : dcY_) z = 0.f; }
    /** amount 0..1; 0 is an exact bypass. */
    void process(float& l, float& r, float amount) noexcept;
private:
    float dcR_ = 0.999f;
    float dcX_[2] = { 0, 0 }, dcY_[2] = { 0, 0 };
};

// ---- reverb (Freeverb topology, public domain tunings) ---------------------------------------------
class Reverb
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    /** size, damp 0..1. Output is the wet signal only. */
    void process(float inL, float inR, float& outL, float& outR, float size, float damp) noexcept;
private:
    struct Comb
    {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.f;
        float process(float in, float feedback, float damp) noexcept
        {
            const float out = buf[static_cast<size_t>(idx)];
            store = out * (1.f - damp) + store * damp;
            buf[static_cast<size_t>(idx)] = in + store * feedback;
            if (++idx >= static_cast<int>(buf.size())) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0;
        float process(float in) noexcept
        {
            const float b = buf[static_cast<size_t>(idx)];
            const float out = -in + b;
            buf[static_cast<size_t>(idx)] = in + b * 0.5f;
            if (++idx >= static_cast<int>(buf.size())) idx = 0;
            return out;
        }
    };
    static constexpr int kCombs = 8, kAllpass = 4;
    Comb combL_[kCombs], combR_[kCombs];
    Allpass apL_[kAllpass], apR_[kAllpass];
};

// ---- LFO -------------------------------------------------------------------------------------------
/** Bipolar (-1..1) LFO evaluated from the song position, so it stays locked to the host. */
float lfoValue(int shape, double ppq, double periodBeats) noexcept;

// ---- step position ---------------------------------------------------------------------------------
struct StepPos
{
    int index = 0;          // 0 .. length-1
    double phase = 0.0;     // 0..1 inside the step
    long long number = 0;   // running step number (before wrapping)
};
/** Which step is playing at song position `ppq`. Swing 0..1 delays every second step (the pair
    boundary moves from 50 % to 75 % of the pair). */
StepPos stepAt(double ppq, double stepBeats, double swing, int length) noexcept;

// ---- sample access ---------------------------------------------------------------------------------
struct Region
{
    double start = 0.0, length = 64.0;   // frames of the file
};
Region regionFor(const SampleData& d, float startFrac, float endFrac) noexcept;
/** Linear-interpolated read at (possibly out of range) `pos`, wrapping inside the region. */
void readWrapped(const SampleData& d, const Region& r, double pos, float& l, float& rr) noexcept;

// ---- grains ----------------------------------------------------------------------------------------
/** Grain player over a SampleData region. Used twice: as the time-locked pitch shifter (2x overlap,
    no scatter) and as the creative granular effect. */
class GrainEngine
{
public:
    static constexpr int kMaxGrains = 64;
    void prepare(double sampleRate, uint32_t seed);
    void reset() noexcept;
    struct Params
    {
        double sizeSamples = 2048;
        float overlap = 2.f;          // grains overlapping at any time (>= 1)
        double pitchRatio = 1.0;      // file frames read per output sample (includes sample-rate ratio)
        double scatterFrames = 0.0;   // +- random start offset
        float pitchRandomSemis = 0.f; // +- random detune per grain
        float spread = 0.f;           // random pan per grain 0..1
        bool align = false;           // WSOLA-style: nudge each grain start so it lines up in phase with the previous one
    };
    /** Adds n samples of grains into outL/outR (overwrites when `overwrite`). The grain start
        position follows `anchor + i * anchorInc` (file frames). */
    void process(const SampleData& d, const Region& r, double anchor, double anchorInc, const Params& p,
                 float* outL, float* outR, int n, bool overwrite) noexcept;
    int activeGrains() const noexcept;
private:
    struct Grain { bool active = false; double pos = 0, inc = 1; int age = 0, len = 1; float gl = 1, gr = 1; };
    std::array<Grain, kMaxGrains> grains_;
    std::vector<float> window_;
    double countdown_ = 0.0;
    int lastIdx_ = -1;
    Rng rng_;
    double bestOffset(const SampleData& d, const Region& r, double nominal, double inc, int range) const noexcept;
};

// ---- beat repeat -----------------------------------------------------------------------------------
class BeatRepeat
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    /** divBeats <= 0: off. Freezes the slice that just passed when a division starts (or changes)
        and loops it; `gate` 0..1 mutes the rest of each slice; `mix` blends with the live signal. */
    void process(float& l, float& r, double divBeats, double bpm, float gate, float mix) noexcept;
    bool active() const noexcept { return active_; }
private:
    double sr_ = 44100.0;
    std::vector<float> ringL_, ringR_;
    int size_ = 1, w_ = 0, startIdx_ = 0, sliceLen_ = 1, pos_ = 0;
    bool active_ = false;
    double activeDiv_ = 0.0;
    float blend_ = 0.f;
};

// ---- tempo detection -------------------------------------------------------------------------------
struct TempoEstimate
{
    double bpm = 0.0;        // when the sample looks like a whole-beat loop, snapped so that it is exactly that long
    double beats = 0.0;      // whole beats in the sample at `bpm`
    double bars = 0.0;       // beats / 4
    float confidence = 0.f;  // 0..1
};
/** Rough tempo of a rhythmic loop (mono mix). Intended for 70..180 bpm loops of 1..64 bars. */
TempoEstimate detectTempo(const float* mono, size_t n, double sampleRate);

} // namespace mangle
