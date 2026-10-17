// Dev tool (not part of the test suite): renders the editor to a PNG on a headless box
// (run under xvfb-run). Everything shown is TEST DATA: a synthetic drum + synth loop generated here,
// and a made-up pattern / LFO setup.
//   MangleSnapshot out.png [seq|fx|lfo] [width height]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "Params.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"

using namespace mangle;

namespace {

struct PlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition(ppq);
        p.setBpm(120.0);
        p.setIsPlaying(true);
        return p;
    }
    double ppq = 0.0;
};

void setParam(MangleProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts().getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

/** 4 bars at 120 bpm: kick, snare, hats and a simple bass/chord line so the waveform looks like music. */
juce::File makeLoop(const juce::File& where)
{
    const double sr = 44100.0, bpm = 120.0;
    const int beats = 16;
    const size_t n = static_cast<size_t>(beats * 60.0 / bpm * sr);
    std::vector<float> l(n, 0.f), r(n, 0.f);
    unsigned seed = 99;
    auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 8388608.f - 1.f; };
    const double beat = 60.0 / bpm * sr;
    for (int b = 0; b < beats; ++b)
    {
        const size_t k0 = static_cast<size_t>(b * beat);
        for (size_t i = 0; i < static_cast<size_t>(0.25 * sr) && k0 + i < n; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const float v = 0.95f * static_cast<float>(std::sin(2.0 * M_PI * (48.0 * t + 100.0 * (1.0 - std::exp(-t * 28.0)) / 28.0)) * std::exp(-t * 16.0));
            l[k0 + i] += v; r[k0 + i] += v;
        }
        if (b % 2 == 1)
            for (size_t i = 0; i < static_cast<size_t>(0.2 * sr) && k0 + i < n; ++i)
            {
                const double t = static_cast<double>(i) / sr;
                const float v = 0.6f * (noise() * static_cast<float>(std::exp(-t * 20.0)) + 0.4f * static_cast<float>(std::sin(2.0 * M_PI * 190.0 * t) * std::exp(-t * 30.0)));
                l[k0 + i] += v; r[k0 + i] += v;
            }
        for (int h = 0; h < 2; ++h)
        {
            const size_t h0 = static_cast<size_t>((b + 0.5 * h) * beat);
            for (size_t i = 0; i < static_cast<size_t>(0.05 * sr) && h0 + i < n; ++i)
            {
                const float v = 0.22f * noise() * static_cast<float>(std::exp(-static_cast<double>(i) / sr * 90.0));
                l[h0 + i] += v; r[h0 + i] += v * 0.8f;
            }
        }
    }
    const double roots[] = { 55.0, 55.0, 65.41, 49.0 };   // A1 A1 C2 G1
    for (int bar = 0; bar < 4; ++bar)
        for (size_t i = 0; i < static_cast<size_t>(4 * beat) && static_cast<size_t>(bar * 4 * beat) + i < n; ++i)
        {
            const size_t k = static_cast<size_t>(bar * 4 * beat) + i;
            const double t = static_cast<double>(i) / sr;
            const float v = 0.30f * static_cast<float>(std::sin(2.0 * M_PI * roots[bar] * t) * (0.6 + 0.4 * std::sin(2.0 * M_PI * 0.5 * t))
                                                      + 0.25 * std::sin(2.0 * M_PI * roots[bar] * 4.0 * t * 1.003) * std::exp(-std::fmod(t, 0.5) * 6.0));
            l[k] += v; r[k] += v;
        }
    juce::WavAudioFormat wav;
    auto f = where.getChildFile("snapshot loop.wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os(new juce::FileOutputStream(f));
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions().withSampleRate(sr).withNumChannels(2).withBitsPerSample(16));
    juce::AudioBuffer<float> buf(2, static_cast<int>(n));
    buf.copyFrom(0, 0, l.data(), static_cast<int>(n));
    buf.copyFrom(1, 0, r.data(), static_cast<int>(n));
    w->writeFromAudioSampleBuffer(buf, 0, static_cast<int>(n));
    return f;
}

bool writePng(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os(f);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream(img, os);
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("snapshot.png"));
    const juce::String tab = argc > 2 ? argv[2] : "seq";
    const int width = argc > 4 ? std::atoi(argv[3]) : 980, height = argc > 4 ? std::atoi(argv[4]) : 700;

    MangleProcessor proc;
    PlayHead ph;
    proc.setPlayHead(&ph);
    proc.setRateAndBufferSizeDetails(44100.0, 512);
    proc.prepareToPlay(44100.0, 512);

    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    proc.loadSampleFile(makeLoop(tmp));
    for (int i = 0; i < 400 && !proc.sampleInfo().loaded; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    if (!proc.sampleInfo().loaded) { std::printf("load failed: %s\n", proc.sampleInfo().message.toRawUTF8()); return 1; }

    // a made-up pattern and settings (TEST DATA)
    const float gate[16] = { 1, 0.6f, 0, 1, 1, 0, 0.6f, 1, 1, 0, 1, 0.6f, 1, 0, 1, 1 };
    const float pitch[16] = { 0, 0, 0, 3, 0, 0, 5, 0, 0, 0, -2, 0, 0, 7, 0, 12 };
    const float filt[16] = { 0, 0, .3f, .6f, 0, 0, 0, 1, 0, .4f, 0, 0, .8f, 0, 0, 0 };
    const float rep[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 4, 5 };
    for (int s = 0; s < 16; ++s)
    {
        setParam(proc, params::stepId(LaneGate, s), gate[s]);
        setParam(proc, params::stepId(LanePitch, s), pitch[s] / 12.f);
        setParam(proc, params::stepId(LaneFilter, s), filt[s]);
        setParam(proc, params::stepId(LaneRepeat, s), rep[s] / 6.f);
    }
    setParam(proc, params::seqRate, 1);
    setParam(proc, params::filterFreq, 5243.f);
    setParam(proc, params::filterReso, 0.35f);
    setParam(proc, params::grit, 0.25f);
    setParam(proc, params::grainMix, 0.4f);
    setParam(proc, params::grainScatter, 0.55f);
    setParam(proc, params::grainPitch, 7.f);
    setParam(proc, params::gateShape, 0.6f);
    setParam(proc, params::gateEdges, 0.35f);
    setParam(proc, params::swing, 0.2f);
    setParam(proc, params::revMix, 0.3f);
    setParam(proc, params::repeatDiv, 3);
    setParam(proc, params::dryWet, 1.f);
    const int targets[] = { 2, 7, 3, 4, 9, 6 };
    const int shapes[] = { 0, 1, 2, 3, 5, 4 };
    const int rates[] = { 3, 4, 5, 6, 2, 5 };
    const float depths[] = { 0.7f, 0.5f, 0.6f, 0.4f, 0.5f, 0.6f };
    for (int i = 0; i < 6; ++i)
    {
        setParam(proc, params::lfoId(i, "shape"), static_cast<float>(shapes[i]));
        setParam(proc, params::lfoId(i, "rate"), static_cast<float>(rates[i]));
        setParam(proc, params::lfoId(i, "target"), static_cast<float>(targets[i]));
        setParam(proc, params::lfoId(i, "depth"), depths[i]);
    }

    // run the transport for a bit so the playhead / step / LFO values are live
    ph.ppq = 5.3;
    juce::MidiBuffer midi;
    for (int b = 0; b < 30; ++b)
    {
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        proc.processBlock(buf, midi);
        ph.ppq += 512.0 / 44100.0 * 2.0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditorAndMakeActive());
    editor->setSize(width, height);
    editor->setVisible(true);
    auto* ed = dynamic_cast<MangleEditor*>(editor.get());
    ed->showTab(tab == "fx" ? MangleEditor::Tab::Fx : tab == "lfo" ? MangleEditor::Tab::Lfo : MangleEditor::Tab::Seq);
    ed->setInfo("CUTOFF opens and closes the filter over the gated signal. tip: park it low and let a filter step open it on the accents.");
    for (int i = 0; i < 12; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    // one more block so the live values are fresh
    {
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        proc.processBlock(buf, midi);
    }
    ed->setInfo("CUTOFF opens and closes the filter over the gated signal. tip: park it low and let a filter step open it on the accents.");
    const bool ok = writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), out);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    editor.reset();
    proc.editorBeingDeleted(nullptr);
    return ok ? 0 : 1;
}
