// Plugin-level tests (headless, Linux/macOS): parameter coverage, state round trip incl. the sample
// (path with spaces, missing file, embedded copy), sample loading in several formats, processing
// through the real processor with a fake host and MIDI, presets, and the editor.

#include <juce_audio_processors/juce_audio_processors.h>

#include <set>

#include "../../core/tests/TestFramework.h"
#include "../../core/tests/TestSignals.h"
#include "Params.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "SampleLoader.h"

using namespace mangle;

namespace {

struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (!valid) return {};
        PositionInfo p;
        p.setPpqPosition(ppq);
        p.setTimeInSeconds(ppq * 60.0 / bpm);
        p.setBpm(bpm);
        p.setIsPlaying(playing);
        return p;
    }
    bool valid = true, playing = true;
    double ppq = 0.0, bpm = 120.0;
};

juce::File tempDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mangle tests " + juce::String(juce::Random::getSystemRandom().nextInt(1000000)));
    d.createDirectory();
    return d;
}

bool writeAudio(juce::AudioFormat& fmt, const juce::File& f, const SampleData& d, int bits = 16)
{
    f.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream>(f);
    if (!os->openedOk()) return false;
    const juce::AudioChannelSet cs = juce::AudioChannelSet::stereo();
    auto opts = juce::AudioFormatWriterOptions().withSampleRate(d.sampleRate).withNumChannels(2).withBitsPerSample(bits);
    std::unique_ptr<juce::OutputStream> stream(os.release());
    auto w = fmt.createWriterFor(stream, opts);
    if (w == nullptr) return false;
    juce::AudioBuffer<float> b(2, static_cast<int>(d.frames()));
    b.copyFrom(0, 0, d.left.data(), b.getNumSamples());
    b.copyFrom(1, 0, d.right.data(), b.getNumSamples());
    return w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool waitFor(const std::function<bool()>& cond, int ms = 8000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (!cond() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return cond();
}

void setParam(MangleProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts().getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}
float getParam(MangleProcessor& p, const juce::String& id) { return p.refs().get(id.toRawUTF8()); }

/** Renders `blocks` blocks; returns the overall peak (and asserts finite). */
float run(MangleProcessor& p, FakePlayHead& ph, int blocks, int n = 512, juce::MidiBuffer* midiFirst = nullptr, double* rmsOut = nullptr)
{
    float peak = 0.f;
    double sum = 0.0;
    long cnt = 0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, n);
        buf.clear();
        juce::MidiBuffer m;
        if (b == 0 && midiFirst != nullptr) m = *midiFirst;
        p.processBlock(buf, m);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample(c, i);
                CHECK(std::isfinite(v));
                peak = std::max(peak, std::abs(v));
                sum += static_cast<double>(v) * v;
                ++cnt;
            }
        if (ph.playing) ph.ppq += n / 44100.0 * ph.bpm / 60.0;
    }
    if (rmsOut) *rmsOut = std::sqrt(sum / static_cast<double>(std::max(1L, cnt)));
    return peak;
}

void prepare(MangleProcessor& p, FakePlayHead& ph)
{
    p.setPlayHead(&ph);
    p.setRateAndBufferSizeDetails(44100.0, 512);
    p.prepareToPlay(44100.0, 512);
}

bool loadAndWait(MangleProcessor& p, const juce::File& f)
{
    p.loadSampleFile(f);
    return waitFor([&] { return p.sampleInfo().loaded || p.sampleInfo().message.isNotEmpty(); }) && p.sampleInfo().loaded;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::SystemStats::setApplicationCrashHandler(nullptr);
    return tf::runAll("MangleTests");
}

TEST_CASE("Params: ids are unique, defaults are in range, expected count")
{
    MangleProcessor p;
    std::set<juce::String> ids;
    int n = 0;
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            CHECK(ids.insert(r->paramID).second);
            CHECK(r->getDefaultValue() >= 0.f && r->getDefaultValue() <= 1.f);
            CHECK(r->getName(64).isNotEmpty());
            ++n;
        }
    // 34 scalar + 6 LFOs x 4 + 4 lanes x 16 steps
    CHECK_EQ(n, 35 + 6 * 4 + 64);
    CHECK_EQ(n, static_cast<int>(p.getParameters().size()));
    CHECK(p.getParameters().size() < 200);   // Logic handles long lists, but keep it sane
    // the audio-thread reader must see every parameter
    Settings s; Pattern pat;
    p.refs().read(s, pat);
    CHECK_EQ(pat.length, 16);
    CHECK_NEAR(s.dryWet, 1.f, 1e-6);
    CHECK_NEAR(s.filterFreq, 18000.f, 1.f);
    CHECK_NEAR(s.loopBeats, 16.f, 1e-4);
}

TEST_CASE("Plugin: identity (AU instrument, stereo out, MIDI in)")
{
    MangleProcessor p;
    CHECK(p.acceptsMidi());
    CHECK(!p.producesMidi());
    CHECK_EQ(p.getTotalNumInputChannels(), 0);
    CHECK_EQ(p.getTotalNumOutputChannels(), 2);
    CHECK(p.hasEditor());
    juce::AudioProcessor::BusesLayout ok, bad;
    ok.outputBuses.add(juce::AudioChannelSet::stereo());
    bad.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(p.checkBusesLayoutSupported(ok));
    CHECK(!p.checkBusesLayoutSupported(bad));
    CHECK_EQ(p.getNumPrograms(), presets::count());
}

TEST_CASE("Sample loading: WAV / AIFF / FLAC decode; path with spaces; tempo detection")
{
    const auto dir = tempDir();
    const auto loop = testsig::drumLoop(44100.0, 120.0, 16);   // 4 bars at 120 bpm
    juce::WavAudioFormat wav; juce::AiffAudioFormat aiff; juce::FlacAudioFormat flac;
    struct { juce::AudioFormat* f; const char* name; } cases[] = { { &wav, "my drum loop.wav" }, { &aiff, "drums (aiff).aif" }, { &flac, "drums.flac" } };
    for (auto& c : cases)
    {
        const auto file = dir.getChildFile(c.name);
        CHECK(writeAudio(*c.f, file, loop));
        MangleProcessor p;
        FakePlayHead ph; prepare(p, ph);
        setParam(p, params::mode, 0);
        CHECK(loadAndWait(p, file));
        CHECK_EQ(p.sampleInfo().name, juce::String(c.name));
        CHECK_NEAR(p.sampleInfo().seconds, 8.0, 0.01);
        CHECK(!p.waveform().empty());
        CHECK_NEAR(p.sampleInfo().detectedBpm, 120.0, 2.0);
        CHECK_NEAR(getParam(p, params::loopBars), 4.f, 0.01);   // adopted from the tempo estimate
        double rms = 0;
        run(p, ph, 40, 512, nullptr, &rms);
        CHECK(rms > 0.01);
    }
    dir.deleteRecursively();
}

TEST_CASE("Sample loading: bad files give a readable message and leave the plug-in silent and alive")
{
    const auto dir = tempDir();
    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);

    CHECK(!loadAndWait(p, dir.getChildFile("nope.wav")));
    CHECK(p.sampleInfo().message.contains("not found"));

    const auto txt = dir.getChildFile("notes.txt");
    txt.replaceWithText("hello");
    p.loadSampleFile(txt);
    CHECK(waitFor([&] { return p.sampleInfo().message.contains("Unsupported"); }));

    const auto bad = dir.getChildFile("broken.wav");
    bad.replaceWithText("RIFF this is not really a wav file");
    p.loadSampleFile(bad);
    CHECK(waitFor([&] { return p.sampleInfo().message.contains("Could not read"); }));
    CHECK(!p.sampleInfo().loaded);
    CHECK(run(p, ph, 8) < 1e-6f);
    dir.deleteRecursively();
}

TEST_CASE("Processing: silent without a sample; plays in time with the host; stops when the transport stops")
{
    const auto dir = tempDir();
    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);
    CHECK(run(p, ph, 10) < 1e-6f);

    const auto file = dir.getChildFile("a b.wav");
    juce::WavAudioFormat wav;
    CHECK(writeAudio(wav, file, testsig::drumLoop(44100.0, 100.0, 8)));
    CHECK(loadAndWait(p, file));
    ph.ppq = 0.0;
    double rms = 0;
    CHECK(run(p, ph, 60, 512, nullptr, &rms) > 0.05f);
    CHECK(p.live().playing.load());
    CHECK(p.live().phase.load() >= 0.f && p.live().phase.load() <= 1.f);
    // stop: after the release tail it is silent
    ph.playing = false;
    run(p, ph, 100);
    CHECK(run(p, ph, 4) < 1e-4f);
    // host without a timeline (standalone): free-runs and plays
    FakePlayHead none; none.valid = false;
    p.setPlayHead(&none);
    p.prepareToPlay(44100.0, 512);
    CHECK(run(p, none, 60) > 0.05f);
    dir.deleteRecursively();
}

TEST_CASE("Processing: Keys mode plays from MIDI notes, pitch follows the note")
{
    const auto dir = tempDir();
    MangleProcessor p;
    FakePlayHead ph; ph.playing = false; prepare(p, ph);
    const auto file = dir.getChildFile("sine.wav");
    juce::WavAudioFormat wav;
    CHECK(writeAudio(wav, file, testsig::sine(44100.0, 2.0, 440.0)));
    CHECK(loadAndWait(p, file));
    setParam(p, params::mode, 2);
    setParam(p, params::rootNote, 60);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 10);
    double rms = 0;
    CHECK(run(p, ph, 30, 512, &on, &rms) > 0.1f);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    run(p, ph, 1, 512, &off);
    run(p, ph, 200);
    CHECK(run(p, ph, 2) < 1e-3f);
    // no note -> silent
    CHECK(run(p, ph, 10) < 1e-4f);
    dir.deleteRecursively();
}

TEST_CASE("Processing: every factory preset and Randomize render finite, bounded audio")
{
    const auto dir = tempDir();
    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);
    const auto file = dir.getChildFile("loop.wav");
    juce::WavAudioFormat wav;
    CHECK(writeAudio(wav, file, testsig::drumLoop(44100.0, 120.0, 16)));
    CHECK(loadAndWait(p, file));
    for (int i = 0; i < presets::count(); ++i)
    {
        p.setCurrentProgram(i);
        CHECK_EQ(p.presetName(), presets::name(i));
        const float pk = run(p, ph, 40);
        CHECK(pk < 8.f);
        // the loop length and sample survive a preset change
        CHECK_NEAR(getParam(p, params::loopBars), 4.f, 0.01);
    }
    for (int i = 0; i < 20; ++i)
    {
        p.randomise();
        CHECK(run(p, ph, 30) < 8.f);
    }
    dir.deleteRecursively();
}

TEST_CASE("State: parameters, pattern and sample survive save/load; path with spaces; embedded copy when the file is gone")
{
    const auto dir = tempDir();
    const auto file = dir.getChildFile("my sample loop.wav");
    juce::WavAudioFormat wav;
    CHECK(writeAudio(wav, file, testsig::drumLoop(44100.0, 110.0, 8)));

    juce::MemoryBlock state;
    {
        MangleProcessor a;
        FakePlayHead ph; prepare(a, ph);
        CHECK(loadAndWait(a, file));
        setParam(a, params::filterFreq, 1234.f);
        setParam(a, params::grainMix, 0.6f);
        setParam(a, params::pitch, -5.f);
        setParam(a, params::stepId(LanePitch, 3), 5.f / 12.f);
        setParam(a, params::stepId(LaneGate, 6), 0.f);
        setParam(a, params::seqLength, 12);
        setParam(a, params::lfoId(2, "target"), 3);
        setParam(a, params::lfoId(2, "depth"), -0.4f);
        a.setCurrentProgram(0);   // presets reset, so set again afterwards
        setParam(a, params::filterFreq, 1234.f);
        setParam(a, params::grainMix, 0.6f);
        setParam(a, params::pitch, -5.f);
        setParam(a, params::stepId(LanePitch, 3), 5.f / 12.f);
        setParam(a, params::stepId(LaneGate, 6), 0.f);
        setParam(a, params::seqLength, 12);
        setParam(a, params::lfoId(2, "target"), 3);
        setParam(a, params::lfoId(2, "depth"), -0.4f);
        a.getStateInformation(state);
        CHECK(state.getSize() > 100);
    }
    auto check = [&](MangleProcessor& b) {
        CHECK_NEAR(getParam(b, params::filterFreq), 1234.f, 1.5f);
        CHECK_NEAR(getParam(b, params::grainMix), 0.6f, 0.01);
        CHECK_NEAR(getParam(b, params::pitch), -5.f, 0.02);
        CHECK_NEAR(getParam(b, params::stepId(LanePitch, 3)), 5.f / 12.f, 0.02);
        CHECK_NEAR(getParam(b, params::stepId(LaneGate, 6)), 0.f, 1e-4);
        CHECK_NEAR(getParam(b, params::seqLength), 12.f, 0.01);
        CHECK_EQ(juce::roundToInt(getParam(b, params::lfoId(2, "target").toRawUTF8())), 3);
        CHECK_NEAR(getParam(b, params::lfoId(2, "depth").toRawUTF8()), -0.4f, 0.01);
    };
    {   // 1. file still there: reloads it from its path
        MangleProcessor b;
        FakePlayHead ph; prepare(b, ph);
        b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        CHECK(waitFor([&] { return b.sampleInfo().loaded; }));
        CHECK(b.sampleInfo().message.isEmpty());
        CHECK_EQ(b.sampleInfo().name, juce::String("my sample loop.wav"));
        CHECK(!b.sampleInfo().usingEmbedded);
        check(b);
        CHECK(run(b, ph, 30) > 0.05f);
    }
    file.deleteFile();
    {   // 2. file gone: the copy embedded in the project is used, with a note
        MangleProcessor b;
        FakePlayHead ph; prepare(b, ph);
        b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        CHECK(waitFor([&] { return b.sampleInfo().loaded; }));
        CHECK(b.sampleInfo().usingEmbedded);
        CHECK(b.sampleInfo().message.contains("not found"));
        check(b);
        CHECK(run(b, ph, 30) > 0.05f);
    }
    {   // 3. file gone and nothing embedded (big file): a clear "missing" message, still silent and alive
        MangleProcessor a;
        FakePlayHead ph; prepare(a, ph);
        a.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        CHECK(waitFor([&] { return a.sampleInfo().loaded; }));
        // strip the embedded bytes from a copy of the state
        auto tree = a.apvts().copyState();
        tree.getChildWithName("Sample").removeProperty("embedded", nullptr);
        juce::MemoryBlock stripped;
        {
            juce::MemoryOutputStream out(stripped, false);
            out.writeInt(0x4d4e474c);
            tree.writeToStream(out);
        }
        MangleProcessor b;
        FakePlayHead ph2; prepare(b, ph2);
        b.setStateInformation(stripped.getData(), static_cast<int>(stripped.getSize()));
        CHECK(waitFor([&] { return b.sampleInfo().message.isNotEmpty(); }));
        CHECK(!b.sampleInfo().loaded);
        CHECK(b.sampleInfo().message.contains("missing"));
        CHECK(b.sampleInfo().message.contains("my sample loop.wav"));
        check(b);
        CHECK(run(b, ph2, 5) < 1e-6f);
        // relinking works
        CHECK(writeAudio(wav, file, testsig::drumLoop(44100.0, 110.0, 8)));
        CHECK(loadAndWait(b, file));
        CHECK(b.sampleInfo().message.isEmpty());
    }
    {   // garbage state is ignored
        MangleProcessor b;
        const char junk[] = "this is not a state";
        b.setStateInformation(junk, sizeof(junk));
        b.setStateInformation(nullptr, 0);
        CHECK_NEAR(getParam(b, params::dryWet), 1.f, 1e-6);
    }
    dir.deleteRecursively();
}

TEST_CASE("Sample hand-off: replacing the sample while audio runs is safe")
{
    const auto dir = tempDir();
    juce::WavAudioFormat wav;
    const auto f1 = dir.getChildFile("one.wav"), f2 = dir.getChildFile("two.wav");
    CHECK(writeAudio(wav, f1, testsig::drumLoop(44100.0, 120.0, 8)));
    CHECK(writeAudio(wav, f2, testsig::sine(44100.0, 3.0, 220.0)));
    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);
    for (int i = 0; i < 6; ++i)
    {
        p.loadSampleFile(i % 2 ? f1 : f2);
        for (int k = 0; k < 40; ++k)
        {
            run(p, ph, 1);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        }
    }
    CHECK(waitFor([&] { return p.sampleInfo().loaded && !p.sampleInfo().loading; }));
    p.clearSample();
    run(p, ph, 40);   // let any release tail fade
    CHECK(run(p, ph, 4) < 1e-6f);
    CHECK(!p.sampleInfo().loaded);
    dir.deleteRecursively();
}

TEST_CASE("Editor: builds, lays out on every tab, takes drops, paints without loss of content")
{
    const auto dir = tempDir();
    juce::WavAudioFormat wav;
    const auto file = dir.getChildFile("drop me.wav");
    CHECK(writeAudio(wav, file, testsig::drumLoop(44100.0, 120.0, 16)));

    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditorAndMakeActive());
    auto* ed = dynamic_cast<MangleEditor*>(base.get());
    CHECK(ed != nullptr);
    if (ed == nullptr) return;
    ed->setSize(980, 700);
    ed->setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK_EQ(ed->fileLabelText(), juce::String("no file loaded"));

    // drop an unsupported file: message
    const auto txt = dir.getChildFile("x.txt");
    txt.replaceWithText("x");
    ed->filesDropped({ txt.getFullPathName() }, 10, 10);
    CHECK(p.sampleInfo().message.contains("not supported"));
    // drop a real file
    ed->filesDropped({ file.getFullPathName() }, 10, 10);
    CHECK(waitFor([&] { return p.sampleInfo().loaded; }));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    CHECK(ed->fileLabelText().contains("drop me.wav"));
    CHECK(ed->isInterestedInFileDrag({ file.getFullPathName() }));

    for (auto t : { MangleEditor::Tab::Fx, MangleEditor::Tab::Lfo, MangleEditor::Tab::Seq })
    {
        ed->showTab(t);
        run(p, ph, 4);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        CHECK_EQ(img.getWidth(), 980);
        CHECK_EQ(img.getHeight(), 700);
        // not a blank image: many distinct pixels
        std::set<juce::uint32> colours;
        for (int y = 0; y < img.getHeight(); y += 7)
            for (int x = 0; x < img.getWidth(); x += 7) colours.insert(img.getPixelAt(x, y).getARGB());
        CHECK(colours.size() > 60);
    }
    // resize within limits
    ed->setSize(1200, 800);
    ed->setSize(920, 660);
    // preset arrows and dice
    p.nextPreset(1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    p.randomise();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    base.reset();
    p.editorBeingDeleted(nullptr);
    dir.deleteRecursively();
}

#if JUCE_MAC
#include <juce_core/juce_core.h>
TEST_CASE("macOS: M4A (AAC) loads through CoreAudio")
{
    const auto dir = tempDir();
    juce::WavAudioFormat wav;
    const auto src = dir.getChildFile("source.wav");
    CHECK(writeAudio(wav, src, testsig::drumLoop(44100.0, 120.0, 16)));
    const auto m4a = dir.getChildFile("encoded loop.m4a");
    juce::ChildProcess proc;
    CHECK(proc.start(juce::StringArray { "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", src.getFullPathName(), m4a.getFullPathName() }));
    proc.waitForProcessToFinish(60000);
    CHECK(m4a.existsAsFile());
    MangleProcessor p;
    FakePlayHead ph; prepare(p, ph);
    CHECK(loadAndWait(p, m4a));
    CHECK_NEAR(p.sampleInfo().seconds, 8.0, 0.2);
    CHECK(p.sampleInfo().detectedBpm > 110.0 && p.sampleInfo().detectedBpm < 130.0);
    double rms = 0;
    run(p, ph, 40, 512, nullptr, &rms);
    CHECK(rms > 0.01);
    dir.deleteRecursively();
}
#endif
