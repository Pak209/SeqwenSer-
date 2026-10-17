#pragma once
// Shared plain-data types of the Mangle DSP core (no JUCE, no allocation after prepare()).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mangle {

constexpr int kNumLanes = 4;    // sequencer lanes
constexpr int kMaxSteps = 16;
constexpr int kNumLfos = 6;

enum Lane { LaneGate = 0, LanePitch = 1, LaneFilter = 2, LaneRepeat = 3 };

/** 16 x 4 step pattern. Lane values:
    gate   0..1   (0 = closed step, else the open level)
    pitch  -1..1  (x 12 semitones, rounded)
    filter 0..1   (opens the cutoff by up to 4 octaves)
    repeat 0..1   (0 = off, else beat-repeat division index round(v*6): 1/4 1/8 1/16 1/32 1/8T 1/16T) */
struct Pattern
{
    float v[kNumLanes][kMaxSteps];
    int length = 16;
    Pattern() { clear(); }
    void clear()
    {
        for (int l = 0; l < kNumLanes; ++l)
            for (int s = 0; s < kMaxSteps; ++s) v[l][s] = l == LaneGate ? 1.f : 0.f;
        length = 16;
    }
};

inline int laneRepeatIndex(float v) { return std::clamp(static_cast<int>(std::lround(v * 6.f)), 0, 6); }
inline int lanePitchSemis(float v) { return static_cast<int>(std::lround(std::clamp(v, -1.f, 1.f) * 12.f)); }

/** Beats (quarter notes) per sequencer step for the rate choice: 1/4 1/8 1/16 1/32 1/8T 1/16T. */
inline double stepBeatsForRate(int idx)
{
    static const double t[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    return t[std::clamp(idx, 0, 5)];
}
/** Repeat division index (0 = off, 1..6) -> beats. */
inline double repeatBeatsForDiv(int idx) { return stepBeatsForRate(idx - 1); }
/** LFO period choices, in beats: 8 bar .. 1/32. */
inline double lfoBeatsForRate(int idx)
{
    static const double t[] = { 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125 };
    return t[std::clamp(idx, 0, 8)];
}

enum class LfoShape { Sine = 0, Triangle, Saw, Square, SampleHold, Smooth, Count };

enum class ModTarget
{
    None = 0, Pitch, FilterFreq, FilterReso, GrainScatter, GrainPitch, GrainMix, Pan, GateDepth, Grit, ReverbMix,
    RepeatMix, Out, DryWet, Count
};

struct LfoSettings
{
    int shape = 0;
    int rate = 3;        // index into lfoBeatsForRate
    float depth = 0.f;   // -1..1
    int target = 0;      // ModTarget
};

enum class PlayMode { LoopTransport = 0, LoopMidi = 1, Keys = 2 };

/** Every sound parameter, read once per block by the plug-in from its automatable parameters. */
struct Settings
{
    int mode = 0;
    float loopBeats = 16.f;           // the sample region lasts this many beats (quarter notes) at the project tempo
    float sampleStart = 0.f, sampleEnd = 1.f;   // region (0..1 of the file)
    int rootNote = 60;
    bool oneShot = false;             // Keys mode: play the region once instead of looping while held
    float dryWet = 1.f, outDb = 0.f, pan = 0.f, swing = 0.f;
    float semis = 0.f, fineCents = 0.f, releaseMs = 120.f;
    bool seqOn = true;
    int seqRate = 1;
    float gateShape = 0.5f, gateEdges = 0.3f, gateDepth = 1.f;
    int filterType = 0;               // 0 low-pass, 1 high-pass
    float filterFreq = 18000.f, filterReso = 0.2f, grit = 0.f;
    float grainMix = 0.f, grainPitch = 0.f, grainScatter = 0.3f, grainSizeMs = 80.f, grainDensity = 4.f;
    int repeatDiv = 0;                // 0 off, 1..6
    float repeatMix = 1.f, repeatGate = 1.f;
    float revSize = 0.5f, revDamp = 0.5f, revMix = 0.f;
    LfoSettings lfo[kNumLfos];
    double fallbackBpm = 120.0;       // used when the host gives no tempo
};

/** Host transport at the start of the block. */
struct HostInfo
{
    bool valid = false;      // false: no host timeline (standalone / unknown): free-running clock
    bool playing = false;
    double ppq = 0.0;        // quarter notes at the first sample of the block
    double bpm = 0.0;        // 0 = unknown
};

struct MidiEvent
{
    enum Type { NoteOn, NoteOff, AllNotesOff };
    int offset = 0;          // sample offset inside the block
    int type = NoteOn;
    int note = 60;
    float velocity = 1.f;
};

/** Decoded audio, shared read-only with the audio thread. */
struct SampleData
{
    std::vector<float> left, right;   // right is a copy of left for mono files
    double sampleRate = 44100.0;
    double detectedBpm = 0.0;
    int64_t frames() const noexcept { return static_cast<int64_t>(left.size()); }
    bool empty() const noexcept { return left.size() < 64; }
};

inline float dbToGain(float db) { return std::pow(10.f, db * 0.05f); }

} // namespace mangle
