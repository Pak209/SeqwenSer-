#pragma once
// Decodes audio files (WAV/AIFF/FLAC/MP3, M4A/AAC via CoreAudio on macOS) on a background thread into
// the core's SampleData, runs the rough tempo detection, and hands the result back on the message thread.

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>

#include "mangle/Types.h"

namespace mangle {

struct LoadResult
{
    bool ok = false;
    juce::String error;            // user-readable
    juce::File file;
    juce::String name;             // display name (file name)
    std::shared_ptr<const SampleData> data;
    juce::String formatName;
    // tempo guess (bars = 0: none)
    double bpm = 0.0, beats = 0.0;
    float confidence = 0.f;
    double seconds = 0.0;
    int generation = 0;
    bool fromEmbedded = false;
    juce::MemoryBlock embeddedBytes;   // raw file bytes when the file was small (kept for the project)
};

class SampleLoader : private juce::Thread
{
public:
    static constexpr double kMaxSeconds = 600.0;          // 10 minutes
    static constexpr size_t kEmbedLimitBytes = 6u << 20;  // files up to 6 MB travel inside the project

    SampleLoader();
    ~SampleLoader() override;

    /** Called on the message thread when a load finished (ok or not). */
    std::function<void(LoadResult)> onFinished;

    void loadFile(const juce::File& file);
    void loadEmbedded(const juce::MemoryBlock& bytes, const juce::String& fileName);
    bool busy() const noexcept { return busy_.load(); }

    /** Synchronous decode (used by the thread and by tests). */
    static LoadResult decode(juce::AudioFormatManager& fm, const juce::File& file, const juce::MemoryBlock* embedded,
                             const juce::String& displayName);
    static juce::String supportedExtensions();  // "*.wav;*.aiff;..."
    static bool isSupportedFile(const juce::File& f);

private:
    void run() override;
    struct Request { juce::File file; juce::MemoryBlock bytes; juce::String name; bool embedded = false; int gen = 0; };
    juce::CriticalSection lock_;
    Request pending_;
    bool havePending_ = false;
    std::atomic<bool> busy_ { false };
    int generation_ = 0;
    juce::AudioFormatManager formats_;
    JUCE_DECLARE_WEAK_REFERENCEABLE(SampleLoader)
};

} // namespace mangle
