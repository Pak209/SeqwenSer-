#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "Params.h"
#include "SampleLoader.h"
#include "mangle/Engine.h"

namespace mangle {

/** Min/max columns of the loaded sample for drawing. Built on the message thread. */
struct Waveform
{
    std::vector<float> mins, maxs;   // one entry per column, -1..1
    bool empty() const noexcept { return mins.empty(); }
};

struct SampleInfo
{
    juce::String name;               // shown in the header
    juce::String path;
    juce::String message;            // problem / hint text ("" = fine)
    double seconds = 0.0;
    double detectedBpm = 0.0;
    float confidence = 0.f;
    bool loaded = false;
    bool loading = false;
    bool usingEmbedded = false;
};

/** Live values for the editor (written by the audio thread, read by the UI timer). */
struct LiveStatus
{
    std::atomic<float> phase { 0.f };
    std::atomic<int> step { 0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> repeating { false };
    std::atomic<float> level { 0.f };
    std::atomic<float> hostBpm { 0.f };
    std::array<std::atomic<float>, kNumLfos> lfo {};
};

class MangleProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    MangleProcessor();
    ~MangleProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mangle"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program_; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- model for the editor (message thread)
    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }
    const params::Refs& refs() const noexcept { return refs_; }
    const LiveStatus& live() const noexcept { return live_; }
    const SampleInfo& sampleInfo() const noexcept { return info_; }
    const Waveform& waveform() const noexcept { return waveform_; }
    juce::String presetName() const { return presetName_; }

    /** Load an audio file (async decode). `adoptTempo`: set the loop length from the detected tempo. */
    void loadSampleFile(const juce::File& f, bool adoptTempo = true);
    /** Sets the loop length so the region lasts as long as the sample would at `sampleBpm`. */
    void showMessage(const juce::String& m) { setMessage(m); }
    void setLoopFromSampleBpm(double sampleBpm);
    void clearSample();
    void nextPreset(int delta);
    void randomise();
    /** Test/UI helper: synchronously install a decoded sample (message thread). */
    void installSample(LoadResult r, bool adoptTempo, bool restoring);

    /** Whole notes of the loaded file the region covers: seconds of the selected region. */
    double regionSeconds() const;

private:
    void onLoadFinished(LoadResult r);
    void restoreSampleFromState();
    void setMessage(const juce::String& m);
    void publish(std::shared_ptr<const SampleData> d);
    void purgeRetired();
    void buildWaveform(const SampleData& d);
    juce::ValueTree sampleNode();

    juce::AudioProcessorValueTreeState apvts_;
    params::Refs refs_;
    SampleLoader loader_;
    Engine engine_;
    LiveStatus live_;

    // audio-thread-owned working copies (no allocation in processBlock)
    Settings settings_;
    Pattern pattern_;
    std::array<MidiEvent, 512> events_ {};

    // sample hand-off (hazard pointer): the audio thread announces what it reads in inUse_.
    std::shared_ptr<const SampleData> current_;
    std::vector<std::shared_ptr<const SampleData>> retired_;
    std::atomic<const SampleData*> active_ { nullptr };
    std::atomic<const SampleData*> inUse_ { nullptr };

    SampleInfo info_;
    Waveform waveform_;
    juce::String presetName_ = "(none)";
    int program_ = 0;
    bool restoring_ = false, adoptTempoOnLoad_ = false, triedEmbedded_ = false;
    juce::MemoryBlock pendingEmbedded_;
    juce::String pendingName_, pendingPath_;
    double sampleRate_ = 44100.0;
    juce::Random rng_ { 0x4d414e47 };

    JUCE_DECLARE_WEAK_REFERENCEABLE(MangleProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MangleProcessor)
};

} // namespace mangle
