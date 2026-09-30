#include "SampleLoader.h"

#include <juce_events/juce_events.h>

#include "Log.h"
#include "mangle/Dsp.h"

namespace mangle {

using namespace juce;

String SampleLoader::supportedExtensions()
{
    String e = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
   #if JUCE_MAC || JUCE_IOS
    e += ";*.m4a;*.aac;*.caf;*.mp4";
   #endif
    return e;
}

bool SampleLoader::isSupportedFile(const File& f)
{
    const auto ext = "*" + f.getFileExtension().toLowerCase();
    return f.existsAsFile() && StringArray::fromTokens(supportedExtensions(), ";", "").contains(ext);
}

SampleLoader::SampleLoader() : Thread("Mangle sample loader")
{
    formats_.registerBasicFormats();   // WAV, AIFF, FLAC, OGG, MP3 (JUCE_USE_MP3AUDIOFORMAT), CoreAudio on macOS (M4A/AAC/CAF)
}

SampleLoader::~SampleLoader()
{
    onFinished = nullptr;
    stopThread(5000);
}

void SampleLoader::loadFile(const File& file)
{
    {
        const ScopedLock sl(lock_);
        pending_ = {};
        pending_.file = file;
        pending_.name = file.getFileName();
        pending_.gen = ++generation_;
        havePending_ = true;
    }
    startThread(Priority::normal);
    notify();
}

void SampleLoader::loadEmbedded(const MemoryBlock& bytes, const String& fileName)
{
    {
        const ScopedLock sl(lock_);
        pending_ = {};
        pending_.bytes = bytes;
        pending_.name = fileName;
        pending_.embedded = true;
        pending_.gen = ++generation_;
        havePending_ = true;
    }
    startThread(Priority::normal);
    notify();
}

void SampleLoader::run()
{
    while (!threadShouldExit())
    {
        Request r;
        {
            const ScopedLock sl(lock_);
            if (!havePending_) { busy_ = false; }
            else { r = std::move(pending_); havePending_ = false; busy_ = true; }
        }
        if (!busy_)
        {
            wait(500);
            if (!havePending_) return;   // idle: let the thread end; loadFile() starts it again
            continue;
        }
        LoadResult res = r.embedded ? decode(formats_, {}, &r.bytes, r.name) : decode(formats_, r.file, nullptr, r.name);
        res.generation = r.gen;
        res.fromEmbedded = r.embedded;
        {
            // A newer request replaced this one: drop the stale result.
            const ScopedLock sl(lock_);
            if (r.gen != generation_) continue;
        }
        WeakReference<SampleLoader> weak(this);
        MessageManager::callAsync([weak, res = std::move(res)]() mutable {
            if (weak != nullptr && weak->onFinished) weak->onFinished(std::move(res));
        });
    }
}

LoadResult SampleLoader::decode(AudioFormatManager& fm, const File& file, const MemoryBlock* embedded, const String& displayName)
{
    LoadResult out;
    out.file = file;
    out.name = displayName.isNotEmpty() ? displayName : file.getFileName();
    const auto t0 = Time::getMillisecondCounterHiRes();

    std::unique_ptr<AudioFormatReader> reader;
    if (embedded != nullptr)
    {
        auto ext = File(displayName).getFileExtension();
        auto* format = fm.findFormatForFileExtension(ext);
        auto stream = std::make_unique<MemoryInputStream>(embedded->getData(), embedded->getSize(), true);
        if (format != nullptr) reader.reset(format->createReaderFor(stream.release(), true));
        if (reader == nullptr)
        {
            // unknown extension: try every format
            for (int i = 0; i < fm.getNumKnownFormats() && reader == nullptr; ++i)
                reader.reset(fm.getKnownFormat(i)->createReaderFor(new MemoryInputStream(embedded->getData(), embedded->getSize(), true), true));
        }
        if (reader == nullptr) { out.error = "The audio saved in this project could not be read."; return out; }
    }
    else
    {
        if (!file.existsAsFile()) { out.error = "File not found: " + file.getFileName(); return out; }
        auto* format = fm.findFormatForFileExtension(file.getFileExtension());
        if (format == nullptr)
        {
            StringArray names;
            for (int i = 0; i < fm.getNumKnownFormats(); ++i)
                names.addIfNotAlreadyThere(fm.getKnownFormat(i)->getFormatName().upToFirstOccurrenceOf(" file", false, true).trim());
            out.error = "Unsupported file type \"" + file.getFileExtension() + "\". Supported here: " + names.joinIntoString(", ") + ".";
            return out;
        }
        out.formatName = format->getFormatName();
        reader.reset(fm.createReaderFor(file));
        if (reader == nullptr)
        {
            out.error = "Could not read " + file.getFileName() + ": it looks damaged or is not a valid " + out.formatName + " file.";
            return out;
        }
        if (file.getSize() <= static_cast<int64>(kEmbedLimitBytes)) file.loadFileAsData(out.embeddedBytes);
    }

    const double sr = reader->sampleRate;
    const int64 total = reader->lengthInSamples;
    if (sr <= 0 || total <= 0 || reader->numChannels == 0) { out.error = out.name + " contains no audio."; return out; }
    if (static_cast<double>(total) / sr > kMaxSeconds)
    {
        out.error = out.name + " is longer than 10 minutes.";
        return out;
    }
    out.seconds = static_cast<double>(total) / sr;

    auto data = std::make_shared<SampleData>();
    data->sampleRate = sr;
    data->left.assign(static_cast<size_t>(total), 0.f);
    data->right.assign(static_cast<size_t>(total), 0.f);
    const int nch = static_cast<int>(jmin<unsigned>(reader->numChannels, 2u));
    constexpr int kChunk = 1 << 16;
    AudioBuffer<float> buf(nch, kChunk);
    for (int64 pos = 0; pos < total; pos += kChunk)
    {
        const int n = static_cast<int>(jmin<int64>(kChunk, total - pos));
        buf.clear();
        if (!reader->read(&buf, 0, n, pos, true, nch > 1))
        {
            out.error = "Reading " + out.name + " failed part-way.";
            return out;
        }
        for (int i = 0; i < n; ++i)
        {
            float l = buf.getSample(0, i), r = nch > 1 ? buf.getSample(1, i) : l;
            if (!std::isfinite(l)) l = 0.f;
            if (!std::isfinite(r)) r = 0.f;
            data->left[static_cast<size_t>(pos + i)] = l;
            data->right[static_cast<size_t>(pos + i)] = r;
        }
    }

    // Rough tempo of loops (mono mix). Only worth it for material of a sensible length.
    if (out.seconds >= 1.0 && out.seconds <= 300.0)
    {
        std::vector<float> mono(static_cast<size_t>(total));
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (data->left[i] + data->right[i]);
        const auto est = detectTempo(mono.data(), mono.size(), sr);
        out.bpm = est.bpm;
        out.beats = est.beats;
        out.confidence = est.confidence;
        data->detectedBpm = est.confidence >= 0.2f ? est.bpm : 0.0;
    }
    out.data = std::move(data);
    out.ok = true;
    log::write("sample", "decoded " + out.name + " (" + String(out.seconds, 2) + " s, " + String(sr, 0) + " Hz, "
                   + String(reader->numChannels) + " ch) in " + String(Time::getMillisecondCounterHiRes() - t0, 0)
                   + " ms; tempo " + String(out.bpm, 1) + " bpm, confidence " + String(out.confidence, 2));
    return out;
}

} // namespace mangle
