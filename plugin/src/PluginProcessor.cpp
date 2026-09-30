#include "PluginProcessor.h"

#include "Log.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "mangle/Dsp.h"

namespace mangle {

using namespace juce;

MangleProcessor::MangleProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Params", params::createLayout()),
      refs_(apvts_)
{
    loader_.onFinished = [this](LoadResult r) { onLoadFinished(std::move(r)); };
    log::write("plugin", "Mangle " + log::versionString() + " created");
}

MangleProcessor::~MangleProcessor()
{
    loader_.onFinished = nullptr;
    active_.store(nullptr);
    log::write("plugin", "Mangle destroyed");
}

bool MangleProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void MangleProcessor::prepareToPlay(double sr, int block)
{
    sampleRate_ = sr > 0 ? sr : 44100.0;
    engine_.prepare(sampleRate_, jmax(1, block));
    engine_.reset();
}

void MangleProcessor::releaseResources() {}

// ---- audio ----------------------------------------------------------------------------------------------
void MangleProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0 || buffer.getNumChannels() < 2) return;

    refs_.read(settings_, pattern_);

    HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            double bpm = 0.0;
            if (auto b = pos->getBpm()) bpm = *b;
            host.playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { host.ppq = *p; host.valid = bpm > 0.0; }
            else if (auto t = pos->getTimeInSeconds()) { host.ppq = *t * bpm / 60.0; host.valid = bpm > 0.0; }
            host.bpm = bpm;
        }

    int numEvents = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        auto& e = events_[static_cast<size_t>(jmin(numEvents, 511))];
        if (numEvents >= 512) break;
        e.offset = jlimit(0, n - 1, meta.samplePosition);
        if (m.isNoteOn()) { e.type = MidiEvent::NoteOn; e.note = m.getNoteNumber(); e.velocity = m.getFloatVelocity(); }
        else if (m.isNoteOff()) { e.type = MidiEvent::NoteOff; e.note = m.getNoteNumber(); e.velocity = 0.f; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = MidiEvent::AllNotesOff; }
        else continue;
        ++numEvents;
    }
    midi.clear();

    // announce which sample we read (hazard pointer), then re-check it is still the published one
    const SampleData* s = active_.load();
    inUse_.store(s);
    if (active_.load() != s) { s = active_.load(); inUse_.store(s); }

    engine_.process(s, settings_, pattern_, host, events_.data(), numEvents, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    inUse_.store(nullptr);

    const auto& st = engine_.status();
    live_.phase.store(static_cast<float>(st.loopPhase));
    live_.step.store(st.step);
    live_.playing.store(st.playing);
    live_.repeating.store(st.repeating);
    live_.level.store(st.level);
    live_.hostBpm.store(static_cast<float>(host.valid ? host.bpm : 0.0));
    for (size_t i = 0; i < static_cast<size_t>(kNumLfos); ++i) live_.lfo[i].store(st.lfo[i]);
}

// ---- sample management (message thread) ----------------------------------------------------------------
ValueTree MangleProcessor::sampleNode() { return apvts_.state.getOrCreateChildWithName("Sample", nullptr); }

void MangleProcessor::setMessage(const String& m)
{
    info_.message = m;
    if (m.isNotEmpty()) log::write("sample", m);
    sendChangeMessage();
}

void MangleProcessor::purgeRetired()
{
    const SampleData* busy = inUse_.load();
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [busy](const std::shared_ptr<const SampleData>& p) { return p.get() != busy; }),
                   retired_.end());
}

void MangleProcessor::publish(std::shared_ptr<const SampleData> d)
{
    auto old = std::move(current_);
    current_ = std::move(d);
    active_.store(current_.get());
    if (old) retired_.push_back(std::move(old));
    purgeRetired();
    if (!retired_.empty())   // the audio thread may still be reading one: release it shortly
        Timer::callAfterDelay(300, [weak = WeakReference<MangleProcessor>(this)] { if (weak != nullptr) weak->purgeRetired(); });
}

void MangleProcessor::buildWaveform(const SampleData& d)
{
    constexpr int kCols = 1400;
    waveform_.mins.assign(kCols, 0.f);
    waveform_.maxs.assign(kCols, 0.f);
    const auto total = static_cast<size_t>(d.frames());
    for (int c = 0; c < kCols; ++c)
    {
        const size_t a = total * static_cast<size_t>(c) / kCols, b = jmax(a + 1, total * static_cast<size_t>(c + 1) / kCols);
        float lo = 0.f, hi = 0.f;
        for (size_t i = a; i < b && i < total; ++i)
        {
            const float v = 0.5f * (d.left[i] + d.right[i]);
            lo = jmin(lo, v);
            hi = jmax(hi, v);
        }
        waveform_.mins[static_cast<size_t>(c)] = lo;
        waveform_.maxs[static_cast<size_t>(c)] = hi;
    }
}

double MangleProcessor::regionSeconds() const
{
    if (!info_.loaded) return 0.0;
    const double a = refs_.get(params::sampleStart), b = refs_.get(params::sampleEnd);
    return info_.seconds * jmax(0.0, b - a);
}

void MangleProcessor::loadSampleFile(const File& f, bool adoptTempo)
{
    restoring_ = false;
    triedEmbedded_ = true;
    adoptTempoOnLoad_ = adoptTempo;
    info_.loading = true;
    info_.message = {};
    log::write("sample", "loading " + f.getFullPathName());
    sendChangeMessage();
    loader_.loadFile(f);
}

void MangleProcessor::clearSample()
{
    publish(nullptr);
    info_ = {};
    waveform_ = {};
    sampleNode().removeAllProperties(nullptr);
    sendChangeMessage();
}

void MangleProcessor::installSample(LoadResult r, bool adoptTempo, bool restoring)
{
    info_.loading = false;
    info_.loaded = true;
    info_.name = r.name;
    info_.path = r.file.getFullPathName();
    info_.seconds = r.seconds;
    info_.detectedBpm = r.data ? r.data->detectedBpm : 0.0;
    info_.confidence = r.confidence;
    info_.usingEmbedded = r.fromEmbedded;
    buildWaveform(*r.data);
    publish(r.data);

    auto node = sampleNode();
    if (!restoring)
    {
        node.setProperty("path", info_.path, nullptr);
        node.setProperty("name", r.name, nullptr);
        if (r.embeddedBytes.getSize() > 0) node.setProperty("embedded", r.embeddedBytes, nullptr);
        else node.removeProperty("embedded", nullptr);
    }

    if (adoptTempo)
    {
        // Loop length: from the detected tempo if it looked like a loop, else the whole-bar count closest to the host tempo.
        double bars = 0.0;
        if (r.confidence >= 0.2f && r.beats > 0) bars = r.beats / 4.0;
        else
        {
            const double bpm = live_.hostBpm.load() > 1.f ? static_cast<double>(live_.hostBpm.load()) : refs_.get(params::fallbackBpm);
            const double beats = r.seconds * bpm / 60.0;
            bars = jmax(0.25, std::round(beats / 4.0 * 4.0) / 4.0);   // nearest quarter bar
            if (beats >= 4.0) bars = jmax(1.0, std::round(beats / 4.0));
        }
        bars = jlimit(0.25, 64.0, bars);
        if (auto* p = apvts_.getParameter(params::loopBars)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(bars))); p->endChangeGesture(); }
        for (auto id : { params::sampleStart, params::sampleEnd })
            if (auto* p = apvts_.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture(); }
    }
    info_.message = {};
    if (restoring && r.fromEmbedded && pendingPath_.isNotEmpty())
        info_.message = "Original file not found (" + pendingPath_ + "). Using the copy saved in the project.";
    else if (r.confidence < 0.2f && adoptTempo && !restoring)
        info_.message = "Tempo not detected: loop length set from your project tempo. Adjust bars or enter the sample's BPM.";
    log::write("sample", "installed " + r.name + (info_.message.isNotEmpty() ? " | " + info_.message : String()));
    sendChangeMessage();
}

void MangleProcessor::onLoadFinished(LoadResult r)
{
    if (!r.ok)
    {
        info_.loading = false;
        if (restoring_ && !triedEmbedded_ && pendingEmbedded_.getSize() > 0)
        {
            triedEmbedded_ = true;
            loader_.loadEmbedded(pendingEmbedded_, pendingName_);
            return;
        }
        if (restoring_)
        {
            info_.loaded = false;
            info_.name = pendingName_;
            info_.path = pendingPath_;
            setMessage("Sample file is missing: " + pendingPath_ + ". Drop it onto the window or press Load to relink.");
        }
        else
            setMessage(r.error);
        return;
    }
    installSample(std::move(r), adoptTempoOnLoad_ && !restoring_, restoring_);
}

void MangleProcessor::setLoopFromSampleBpm(double sampleBpm)
{
    if (!info_.loaded || sampleBpm < 20.0 || sampleBpm > 400.0) return;
    const double region = jmax(0.01, static_cast<double>(refs_.get(params::sampleEnd) - refs_.get(params::sampleStart)));
    const double beats = info_.seconds * region * sampleBpm / 60.0;
    const double bars = jlimit(0.25, 64.0, std::round(beats / 4.0 * 4.0) / 4.0);
    if (auto* p = apvts_.getParameter(params::loopBars)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(bars))); p->endChangeGesture(); }
    sendChangeMessage();
}

// ---- programs ----------------------------------------------------------------------------------------
int MangleProcessor::getNumPrograms() { return presets::count(); }
const String MangleProcessor::getProgramName(int i) { return i >= 0 && i < presets::count() ? presets::name(i) : String(); }

void MangleProcessor::setCurrentProgram(int i)
{
    if (i < 0 || i >= presets::count()) return;
    program_ = i;
    presetName_ = presets::name(i);
    presets::apply(apvts_, i);
    apvts_.state.setProperty("preset", presetName_, nullptr);
    sendChangeMessage();
}

void MangleProcessor::nextPreset(int delta)
{
    const int n = presets::count();
    setCurrentProgram(((program_ + delta) % n + n) % n);
}

void MangleProcessor::randomise()
{
    presets::randomize(apvts_, rng_);
    presetName_ = "(random)";
    apvts_.state.setProperty("preset", presetName_, nullptr);
    sendChangeMessage();
}

// ---- state ------------------------------------------------------------------------------------------------
void MangleProcessor::getStateInformation(MemoryBlock& dest)
{
    apvts_.state.setProperty("preset", presetName_, nullptr);
    apvts_.state.setProperty("version", String(MANGLE_VERSION), nullptr);
    MemoryOutputStream out(dest, false);
    out.writeInt(0x4d4e474c);   // 'MNGL'
    apvts_.copyState().writeToStream(out);
}

void MangleProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size < 8) return;
    MemoryInputStream in(data, static_cast<size_t>(size), false);
    if (in.readInt() != 0x4d4e474c) { log::write("state", "ignored state with a wrong header"); return; }
    auto tree = ValueTree::readFromStream(in);
    if (!tree.isValid() || !tree.hasType(apvts_.state.getType())) { log::write("state", "ignored unreadable state"); return; }
    apvts_.replaceState(tree);
    presetName_ = apvts_.state.getProperty("preset", "(none)").toString();
    restoreSampleFromState();
}

void MangleProcessor::restoreSampleFromState()
{
    auto node = apvts_.state.getChildWithName("Sample");
    if (!node.isValid() || node["path"].toString().isEmpty())
    {
        if (!info_.loaded) { publish(nullptr); info_ = {}; waveform_ = {}; }
        sendChangeMessage();
        return;
    }
    pendingPath_ = node["path"].toString();
    pendingName_ = node["name"].toString();
    pendingEmbedded_ = {};
    if (auto* mb = node["embedded"].getBinaryData()) pendingEmbedded_ = *mb;
    if (info_.loaded && info_.path == pendingPath_ && current_) { sendChangeMessage(); return; }   // same sample already here

    restoring_ = true;
    triedEmbedded_ = false;
    adoptTempoOnLoad_ = false;
    info_.loading = true;
    info_.name = pendingName_;
    info_.message = {};
    const File f(pendingPath_);
    if (f.existsAsFile()) loader_.loadFile(f);
    else if (pendingEmbedded_.getSize() > 0) { triedEmbedded_ = true; loader_.loadEmbedded(pendingEmbedded_, pendingName_); }
    else
    {
        info_.loading = false;
        info_.loaded = false;
        setMessage("Sample file is missing: " + pendingPath_ + ". Drop it onto the window or press Load to relink.");
    }
    sendChangeMessage();
}

AudioProcessorEditor* MangleProcessor::createEditor() { return new MangleEditor(*this); }

} // namespace mangle

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mangle::MangleProcessor(); }
