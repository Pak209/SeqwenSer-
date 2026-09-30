#include "PluginEditor.h"

#include "Log.h"
#include "Params.h"
#include "SampleLoader.h"

namespace mangle {

using namespace juce;
namespace th = mangle::theme;
namespace col = mangle::theme::col;

namespace {
constexpr int kW = 980, kH = 700;
constexpr int kPad = 16;

} // namespace

MangleEditor::MangleEditor(MangleProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), stepGrid_(p), waveform_(p), lfoStrip_(p)
{
    setLookAndFeel(&laf_);
    auto& a = proc_.apvts();

    // ---- header ---------------------------------------------------------------------------------------
    fileButton_.setButtonText("no file loaded");
    fileButton_.getProperties().set("colour", static_cast<int>(col::purple.getARGB()));
    fileButton_.setTooltip("The loaded audio file. Click to choose another (or drop a file on the window).");
    fileButton_.onClick = [this] { chooseFile(); };
    addAndMakeVisible(fileButton_);
    loadButton_.setTooltip("Load an audio file (WAV, AIFF, FLAC, MP3, M4A).");
    loadButton_.getProperties().set("accent", true);
    loadButton_.onClick = [this] { chooseFile(); };
    addAndMakeVisible(loadButton_);

    for (auto* b : { &seqTab_, &fxTab_, &lfoTab_ })
    {
        b->setClickingTogglesState(false);
        b->getProperties().set("colour", static_cast<int>(col::blue.getARGB()));
        addAndMakeVisible(*b);
    }
    seqTab_.setTooltip("Step sequencer: gate, pitch, filter and beat-repeat lanes, 16 steps, in time with Logic.");
    fxTab_.setTooltip("Pitch, release, beat repeat, reverb and grain settings.");
    lfoTab_.setTooltip("Six LFOs that move any control in time with the song.");
    seqTab_.onClick = [this] { showTab(Tab::Seq); };
    fxTab_.onClick = [this] { showTab(Tab::Fx); };
    lfoTab_.onClick = [this] { showTab(Tab::Lfo); };

    rateLabel_.setText("rate", dontSendNotification);
    rateLabel_.setFont(th::font(12.f, th::Weight::Medium));
    rateLabel_.setColour(Label::textColourId, col::textDim);
    rateLabel_.setJustificationType(Justification::centredRight);
    addAndMakeVisible(rateLabel_);
    rateBox_.addItemList(params::kRateNames, 1);
    rateBox_.setTooltip("Length of one sequencer step (1/8 = eighth notes).");
    addAndMakeVisible(rateBox_);
    rateAttach_ = std::make_unique<ComboBoxParameterAttachment>(*a.getParameter(params::seqRate), rateBox_);

    seqOnButton_.setClickingTogglesState(true);
    seqOnButton_.setTooltip("Turn the step sequencer on or off. Off = the sample plays straight, gate lane ignored.");
    seqOnButton_.getProperties().set("colour", static_cast<int>(col::green.getARGB()));
    addAndMakeVisible(seqOnButton_);
    seqOnAttach_ = std::make_unique<ButtonParameterAttachment>(*a.getParameter(params::seqOn), seqOnButton_);

    // ---- sample bar ---------------------------------------------------------------------------------------
    modeBox_.addItemList(params::kModeNames, 1);
    modeBox_.setTooltip("How the sample plays. Loop (host tempo): loops in time with Logic while it plays. Loop on MIDI note: loops while a key is held. Keys: plays like a sampler; the MIDI note sets the pitch.");
    addAndMakeVisible(modeBox_);
    modeAttach_ = std::make_unique<ComboBoxParameterAttachment>(*a.getParameter(params::mode), modeBox_);

    auto barStyle = [this](Slider& s, const String& tip) {
        s.setSliderStyle(Slider::LinearBar);
        s.setTextBoxIsEditable(true);
        s.setTooltip(tip);
        s.setColour(Slider::trackColourId, col::purple.withAlpha(0.35f));
        s.setColour(Slider::textBoxTextColourId, col::text);
        s.setColour(Slider::textBoxOutlineColourId, col::border);
        s.setColour(Slider::backgroundColourId, col::field);
        s.setMouseDragSensitivity(300);
        addAndMakeVisible(s);
    };
    barStyle(barsBar_, "Loop length in bars (4 beats each). The chosen part of the sample is stretched or squeezed to fit this many bars at the Logic tempo.");
    barsAttach_ = std::make_unique<SliderParameterAttachment>(*a.getParameter(params::loopBars), barsBar_, nullptr);
    barsBar_.setTextValueSuffix(" bars");
    barStyle(rootBar_, "Root note (Keys mode): the MIDI note at which the sample plays at its original pitch.");
    rootAttach_ = std::make_unique<SliderParameterAttachment>(*a.getParameter(params::rootNote), rootBar_, nullptr);
    barStyle(stepsBar_, "Number of sequencer steps before the pattern repeats (1 to 16).");
    stepsAttach_ = std::make_unique<SliderParameterAttachment>(*a.getParameter(params::seqLength), stepsBar_, nullptr);
    stepsBar_.setTextValueSuffix(" steps");

    barsLabel_.setText("loop", dontSendNotification);
    rootLabel_.setText("root", dontSendNotification);
    bpmLabel_.setText("sample bpm", dontSendNotification);
    for (auto* l : { &barsLabel_, &rootLabel_, &bpmLabel_ })
    {
        l->setFont(th::font(11.f, th::Weight::Medium));
        l->setColour(Label::textColourId, col::textDim);
        l->setJustificationType(Justification::centredRight);
        addAndMakeVisible(*l);
    }
    bpmField_.setJustification(Justification::centred);
    bpmField_.setInputRestrictions(6, "0123456789.");
    bpmField_.setFont(th::font(12.f, th::Weight::Medium));
    bpmField_.setColour(TextEditor::backgroundColourId, col::field);
    bpmField_.setColour(TextEditor::outlineColourId, col::border);
    bpmField_.setColour(TextEditor::focusedOutlineColourId, col::purple);
    bpmField_.setColour(TextEditor::textColourId, col::text);
    bpmField_.setTooltip("Tempo of the sample itself. Detected automatically for loops; type a number and press Return to override (this sets the loop length).");
    bpmField_.onReturnKey = [this] { proc_.setLoopFromSampleBpm(bpmField_.getText().getDoubleValue()); bpmField_.giveAwayKeyboardFocus(); };
    bpmField_.onEscapeKey = [this] { bpmField_.giveAwayKeyboardFocus(); };
    addAndMakeVisible(bpmField_);
    oneShot_.setTooltip("Keys mode: play the sample once from its start instead of looping while the key is held.");
    addAndMakeVisible(oneShot_);
    oneShotAttach_ = std::make_unique<ButtonParameterAttachment>(*a.getParameter(params::oneShot), oneShot_);

    // ---- panels --------------------------------------------------------------------------------------------------
    stepGrid_.onInfo = [this](const String& s) { stepInfo_ = s; setInfo(s); };
    addAndMakeVisible(stepGrid_);
    addAndMakeVisible(fxPanel_);
    addAndMakeVisible(lfoPanel_);
    waveform_.onLoadClicked = [this] { chooseFile(); };
    addAndMakeVisible(waveform_);
    lfoStrip_.setTooltip("The six LFOs. Click one to edit it on the lfo tab. The dot shows its live value.");
    lfoStrip_.onPick = [this](int) { showTab(Tab::Lfo); };
    addAndMakeVisible(lfoStrip_);

    // fx tab
    auto fxKnob = [&](const char* id, const String& name, const String& tip, Colour c, bool bip = false) {
        auto k = std::make_unique<Knob>(a, id, name, tip, c, bip);
        fxPanel_.addAndMakeVisible(*k);
        fxKnobs_.push_back(std::move(k));
    };
    fxKnob(params::pitch, "pitch", "Transposes the whole sample in semitones. The sample keeps its length: only the pitch changes.", col::blue, true);
    fxKnob(params::fine, "fine", "Fine tuning in cents (100 cents = 1 semitone).", col::blue, true);
    fxKnob(params::release, "release", "How long the sound fades after a key is released, or after the transport stops.", col::blue);
    fxKnob(params::repeatMix, "mix", "Beat repeat: how much of the repeated slice you hear.", col::orange);
    fxKnob(params::repeatGate, "gate", "Beat repeat: shortens each repeat so the slices sound chopped.", col::orange);
    fxKnob(params::revSize, "size", "Reverb: room size.", col::cyan);
    fxKnob(params::revDamp, "damp", "Reverb: high-frequency damping. More damping = darker tail.", col::cyan);
    fxKnob(params::revMix, "mix", "Reverb amount.", col::cyan);
    fxKnob(params::grainSize, "size", "Granular: grain length in milliseconds.", col::purple);
    fxKnob(params::grainDensity, "density", "Granular: how many grains overlap.", col::purple);
    repeatBox_.addItemList(params::kRepeatNames, 1);
    repeatBox_.setTooltip("Beat repeat: loops the last slice of sound at this note length. The repeat lane on the seq tab can switch it on for single steps.");
    fxPanel_.addAndMakeVisible(repeatBox_);
    repeatAttach_ = std::make_unique<ComboBoxParameterAttachment>(*a.getParameter(params::repeatDiv), repeatBox_);

    // lfo tab
    static const char* what[] = { "shape", "rate", "target" };
    for (int i = 0; i < kNumLfos; ++i)
    {
        auto& c = lfoCols_[static_cast<size_t>(i)];
        c.shape.addItemList(params::kLfoShapeNames, 1);
        c.rate.addItemList(params::kLfoRateNames, 1);
        c.target.addItemList(params::kTargetNames, 1);
        ComboBox* boxes[] = { &c.shape, &c.rate, &c.target };
        const String tips[] = { "LFO wave shape.", "How long one LFO cycle lasts (locked to the song tempo).",
                                "The control this LFO moves. Off = the LFO does nothing." };
        for (int k = 0; k < 3; ++k)
        {
            boxes[k]->getProperties().set("colour", static_cast<int>(th::lfoColour(i).getARGB()));
            boxes[k]->setTooltip("LFO " + String(i + 1) + ": " + tips[k]);
            lfoPanel_.addAndMakeVisible(*boxes[k]);
            lfoComboAttach_[static_cast<size_t>(i * 3 + k)] = std::make_unique<ComboBoxParameterAttachment>(*a.getParameter(params::lfoId(i, what[k])), *boxes[k]);
        }
        c.depth = std::make_unique<Knob>(a, params::lfoId(i, "depth").toRawUTF8(), "depth",
                                         "LFO " + String(i + 1) + " depth. Negative values flip the LFO upside down.", th::lfoColour(i), true);
        lfoPanel_.addAndMakeVisible(*c.depth);
    }

    // ---- knob sections --------------------------------------------------------------------------------------------
    sections_[0].title = "GATE";     sections_[0].colour = col::blue;
    sections_[1].title = "GRANULAR"; sections_[1].colour = col::purple;
    sections_[2].title = "FILTER";   sections_[2].colour = col::yellow;
    sections_[3].title = "TIMING / OUTPUT"; sections_[3].colour = col::green;
    addSectionKnob(sections_[0], params::gateShape, "shape", "Shape of each gated step: low = short and punchy, high = full length.");
    addSectionKnob(sections_[0], params::gateEdges, "edges", "How soft the start and end of each step are. Low = hard clicks, high = smooth fades.");
    addSectionKnob(sections_[0], params::gateDepth, "depth", "How far closed steps are turned down. 100 % = full silence on closed steps.");
    addSectionKnob(sections_[1], params::grainMix, "mix", "How much granular sound is blended in. 0 = none.");
    addSectionKnob(sections_[1], params::grainPitch, "pitch", "Pitch of the grains in semitones, on top of the main pitch.", true);
    addSectionKnob(sections_[1], params::grainScatter, "scatter", "How randomly the grains jump around the sample.");
    addSectionKnob(sections_[2], params::filterFreq, "freq", "Filter cutoff in Hz. Low-pass removes highs, high-pass removes lows.");
    addSectionKnob(sections_[2], params::filterReso, "reso", "Filter resonance: emphasis around the cutoff.");
    addSectionKnob(sections_[2], params::grit, "grit", "Distortion and crunch. 0 = clean.");
    addSectionKnob(sections_[3], params::swing, "swing", "Delays every second step for a shuffled feel. Also applies to the LFO-independent step clock.");
    addSectionKnob(sections_[3], params::pan, "pan", "Left / right position.", true);
    addSectionKnob(sections_[3], params::outDb, "out", "Output level in dB.");
    sections_[2].knobs[0]->slider().getProperties().set("colour", static_cast<int>(col::yellow.getARGB()));
    filterTypeBox_.addItemList(params::kFilterNames, 1);
    filterTypeBox_.setTooltip("Filter type: low-pass or high-pass.");
    addAndMakeVisible(filterTypeBox_);
    filterTypeAttach_ = std::make_unique<ComboBoxParameterAttachment>(*a.getParameter(params::filterType), filterTypeBox_);

    // ---- footer ----------------------------------------------------------------------------------------------------------
    dryWet_.setSliderStyle(Slider::LinearHorizontal);
    dryWet_.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
    dryWet_.getProperties().set("colour", static_cast<int>(col::purple.getARGB()));
    dryWet_.setTooltip("Dry / wet: 0 % is the plain sample, 100 % is all the effects.");
    addAndMakeVisible(dryWet_);
    dryWetAttach_ = std::make_unique<SliderParameterAttachment>(*a.getParameter(params::dryWet), dryWet_, nullptr);
    dryWetLabel_.setText("DRY / WET", dontSendNotification);
    dryWetLabel_.setFont(Font(th::font(10.5f, th::Weight::SemiBold)).withExtraKerningFactor(0.1f));
    dryWetLabel_.setColour(Label::textColourId, col::textDim);
    addAndMakeVisible(dryWetLabel_);
    dryWetValue_.setFont(th::font(12.f, th::Weight::Medium));
    dryWetValue_.setColour(Label::textColourId, col::text);
    dryWetValue_.setJustificationType(Justification::centredRight);
    addAndMakeVisible(dryWetValue_);

    auto shape = [](ShapeButton& b, const Path& path, const String& tip) {
        b.setShape(path, false, true, false);
        b.setTooltip(tip);
    };
    shape(gearButton_, th::icons::gear({ 0, 0, 20, 20 }), "Menu: load or clear the sample, reset controls, open the log.");
    shape(prevButton_, th::icons::arrowLeft({ 0, 0, 10, 16 }), "Previous preset.");
    shape(nextButton_, th::icons::arrowRight({ 0, 0, 10, 16 }), "Next preset.");
    shape(diceButton_, th::icons::dice({ 0, 0, 20, 20 }), "Randomise the sequencer, filter, grains and LFOs (Cmd/Ctrl+Z in Logic undoes it). Keeps your sample and loop length.");
    gearButton_.onClick = [this] { openGearMenu(); };
    prevButton_.onClick = [this] { proc_.nextPreset(-1); };
    nextButton_.onClick = [this] { proc_.nextPreset(1); };
    diceButton_.onClick = [this] { proc_.randomise(); };
    for (auto* b : { &gearButton_, &prevButton_, &nextButton_, &diceButton_ }) addAndMakeVisible(*b);

    presetLabel_.setJustificationType(Justification::centred);
    presetLabel_.setFont(th::font(13.f, th::Weight::Medium));
    presetLabel_.setColour(Label::textColourId, col::text);
    presetLabel_.setInterceptsMouseClicks(true, false);
    presetLabel_.setTooltip("Preset name. Presets are also Logic's plug-in presets.");
    addAndMakeVisible(presetLabel_);

    infoLabel_.setJustificationType(Justification::topLeft);
    infoLabel_.setFont(th::font(11.5f));
    infoLabel_.setColour(Label::textColourId, col::textDim);
    infoLabel_.setMinimumHorizontalScale(1.f);
    addAndMakeVisible(infoLabel_);

    setInfo("Drop an audio file onto the window to start. Hover over anything to see what it does.");
    showTab(Tab::Seq);
    constrainer_.setMinimumSize(920, 660);
    constrainer_.setMaximumSize(1600, 1200);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(kW, kH);
    proc_.addChangeListener(this);
    updateFileLabels();
    startTimerHz(30);
}

MangleEditor::~MangleEditor()
{
    stopTimer();
    proc_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void MangleEditor::addSectionKnob(Section& s, const char* id, const String& name, const String& tip, bool bipolar)
{
    auto k = std::make_unique<Knob>(proc_.apvts(), id, name, tip, s.colour, bipolar);
    addAndMakeVisible(*k);
    s.knobs.push_back(std::move(k));
}

void MangleEditor::setInfo(const String& s)
{
    if (s == info_) return;
    info_ = s;
    infoLabel_.setText(s, dontSendNotification);
}

String MangleEditor::fileMetaText() const
{
    const auto& i = proc_.sampleInfo();
    if (i.loading) return "loading...";
    if (!i.loaded) return {};
    String t = String(i.seconds, 1) + " s";
    if (i.detectedBpm > 0.0 && i.confidence >= 0.2f) t << "  -  ~" << String(roundToInt(i.detectedBpm)) << " bpm";
    return t;
}

void MangleEditor::updateFileLabels()
{
    const auto& i = proc_.sampleInfo();
    String t = i.loaded ? i.name + "   " + fileMetaText() : (i.loading ? String("loading...") : (i.name.isNotEmpty() ? i.name + " (missing)" : String("no file loaded")));
    if (t != fileButton_.getButtonText()) fileButton_.setButtonText(t);
    fileButton_.getProperties().set("colour", static_cast<int>((i.loaded ? col::purple : (i.message.isNotEmpty() ? col::yellow : col::textFaint)).getARGB()));
    waveform_.invalidateWaveform();
    if (!bpmField_.hasKeyboardFocus(true))
    {
        String b;
        const double secs = proc_.regionSeconds();
        if (i.loaded && secs > 0.01) b = String(proc_.refs().get(params::loopBars) * 4.0 * 60.0 / secs, 1);
        if (bpmField_.getText() != b) bpmField_.setText(b, dontSendNotification);
    }
    if (i.message.isNotEmpty()) setInfo(i.message);
    repaint();
}

void MangleEditor::changeListenerCallback(ChangeBroadcaster*) { updateFileLabels(); }

String MangleEditor::hoverInfo()
{
    auto* c = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    for (; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (c == &stepGrid_) return {};   // handled by the grid itself
        if (auto* tc = dynamic_cast<TooltipClient*>(c))
        {
            const auto t = tc->getTooltip();
            if (t.isNotEmpty()) return t;
        }
    }
    return {};
}

void MangleEditor::timerCallback()
{
    static String lastHover;
    const auto h = hoverInfo();
    if (h.isNotEmpty() && h != lastHover) { lastHover = h; setInfo(h); }

    const auto pn = proc_.presetName();
    if (pn != lastPresetShown_) { lastPresetShown_ = pn; presetLabel_.setText(pn, dontSendNotification); }
    for (auto& s : sections_) for (auto& k : s.knobs) k->refresh();
    for (auto& k : fxKnobs_) k->refresh();
    for (auto& c : lfoCols_) if (c.depth) c.depth->refresh();
    dryWetValue_.setText(String(roundToInt(proc_.refs().get(params::dryWet) * 100.f)) + " %", dontSendNotification);

    stepGrid_.repaint();
    lfoStrip_.repaint();
    waveform_.repaint();
    const bool keys = roundToInt(proc_.refs().get(params::mode)) == 2;
    rootBar_.setVisible(keys);
    rootLabel_.setVisible(keys);
    oneShot_.setVisible(keys);
    if (!bpmField_.hasKeyboardFocus(true)) updateFileLabels();
}

void MangleEditor::showTab(Tab t)
{
    tab_ = t;
    stepGrid_.setVisible(t == Tab::Seq);
    fxPanel_.setVisible(t == Tab::Fx);
    lfoPanel_.setVisible(t == Tab::Lfo);
    seqOnButton_.setVisible(t == Tab::Seq);
    stepsBar_.setVisible(t == Tab::Seq);
    seqTab_.setToggleState(t == Tab::Seq, dontSendNotification);
    fxTab_.setToggleState(t == Tab::Fx, dontSendNotification);
    lfoTab_.setToggleState(t == Tab::Lfo, dontSendNotification);
    resized();
    repaint();
}

// ---- files -----------------------------------------------------------------------------------------------------------
bool MangleEditor::isInterestedInFileDrag(const StringArray& files)
{
    for (auto& f : files) if (SampleLoader::isSupportedFile(File(f))) return true;
    return files.size() > 0;   // accept and explain when the type is wrong
}

void MangleEditor::filesDropped(const StringArray& files, int, int)
{
    waveform_.setDropHighlight(false);
    for (auto& f : files)
        if (SampleLoader::isSupportedFile(File(f))) { proc_.loadSampleFile(File(f)); return; }
    proc_.showMessage("That file type is not supported. Use WAV, AIFF, FLAC, MP3 or M4A.");
}

void MangleEditor::chooseFile()
{
    chooser_ = std::make_unique<FileChooser>("Choose an audio file", File::getSpecialLocation(File::userMusicDirectory), SampleLoader::supportedExtensions());
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) proc_.loadSampleFile(f);
    });
}

void MangleEditor::openGearMenu()
{
    PopupMenu m;
    m.addItem(1, "Load sample...");
    m.addItem(2, "Clear sample", proc_.sampleInfo().loaded);
    m.addSeparator();
    m.addItem(3, "Reset all controls");
    m.addSeparator();
    m.addItem(4, "Show log file");
    m.addItem(5, String("Mangle ") + log::versionString(), false);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(gearButton_), [this](int r) {
        switch (r)
        {
            case 1: chooseFile(); break;
            case 2: proc_.clearSample(); break;
            case 3: proc_.setCurrentProgram(0); break;
            case 4: log::file().revealToUser(); break;
            default: break;
        }
    });
}

// ---- layout & painting -----------------------------------------------------------------------------------------------
void MangleEditor::resized()
{
    auto r = getLocalBounds().reduced(kPad, 12);
    const int W = r.getWidth();

    // header
    auto head = r.removeFromTop(34);
    headerBounds_ = head;
    auto left = head.removeFromLeft(W * 34 / 100);
    fileButton_.setBounds(left.removeFromLeft(left.getWidth() - 70));
    left.removeFromLeft(6);
    loadButton_.setBounds(left);
    fileMetaBounds_ = {};
    auto right = head.removeFromRight(W * 22 / 100);
    rateBox_.setBounds(right.removeFromRight(84));
    rateLabel_.setBounds(right.removeFromRight(40));
    auto tabs = head.withSizeKeepingCentre(3 * 64 + 12, 30);
    seqTab_.setBounds(tabs.removeFromLeft(64)); tabs.removeFromLeft(6);
    fxTab_.setBounds(tabs.removeFromLeft(64)); tabs.removeFromLeft(6);
    lfoTab_.setBounds(tabs.removeFromLeft(64));
    r.removeFromTop(10);

    // footer
    auto foot = r.removeFromBottom(58);
    footerBounds_ = foot;
    r.removeFromBottom(8);
    auto dw = foot.removeFromLeft(W * 27 / 100);
    dryWetLabel_.setBounds(dw.removeFromTop(20).removeFromLeft(90));
    dryWetValue_.setBounds(dryWetLabel_.getBounds().withX(dw.getRight() - 64).withWidth(64));
    dryWet_.setBounds(dw.reduced(0, 6));
    foot.removeFromLeft(20);
    auto mid = foot.removeFromLeft(W * 27 / 100);
    {
        auto row = mid.withSizeKeepingCentre(mid.getWidth(), 28);
        gearButton_.setBounds(row.removeFromLeft(28).reduced(4));
        diceButton_.setBounds(row.removeFromRight(28).reduced(4));
        prevButton_.setBounds(row.removeFromLeft(26).reduced(7, 5));
        nextButton_.setBounds(row.removeFromRight(26).reduced(7, 5));
        presetLabel_.setBounds(row);
    }
    foot.removeFromLeft(14);
    infoLabel_.setBounds(foot.reduced(0, 2));

    // knob sections
    auto knobs = r.removeFromBottom(134);
    r.removeFromBottom(10);
    const int gap = 12;
    const int secW = (knobs.getWidth() - gap * 3) / 4;
    for (size_t i = 0; i < sections_.size(); ++i)
    {
        auto b = knobs.removeFromLeft(secW);
        knobs.removeFromLeft(gap);
        sections_[i].bounds = b;
        auto inner = b.reduced(8, 6);
        auto titleRow = inner.removeFromTop(16);
        if (i == 2) filterTypeBox_.setBounds(titleRow.removeFromRight(78).withHeight(18));
        const int kw = inner.getWidth() / 3;
        for (auto& k : sections_[i].knobs) k->setBounds(inner.removeFromLeft(kw));
    }

    // lfo strip
    lfoStripBounds_ = r.removeFromBottom(52);
    lfoStrip_.setBounds(lfoStripBounds_);
    r.removeFromBottom(10);

    // tab panel (top) and sample bar, waveform gets the rest
    const int panelH = tab_ == Tab::Lfo ? 132 : (tab_ == Tab::Fx ? 132 : 122);
    auto panel = r.removeFromTop(panelH);
    r.removeFromTop(8);
    auto sbar = r.removeFromBottom(26);
    r.removeFromBottom(6);
    waveform_.setBounds(r);

    stepGrid_.setBounds(panel);
    fxPanel_.setBounds(panel);
    lfoPanel_.setBounds(panel);
    {
        // sample bar
        auto s = sbar;
        modeBox_.setBounds(s.removeFromLeft(190));
        s.removeFromLeft(10);
        barsLabel_.setBounds(s.removeFromLeft(34));
        barsBar_.setBounds(s.removeFromLeft(96)); s.removeFromLeft(12);
        bpmLabel_.setBounds(s.removeFromLeft(78));
        bpmField_.setBounds(s.removeFromLeft(64)); s.removeFromLeft(14);
        rootLabel_.setBounds(s.removeFromLeft(34));
        rootBar_.setBounds(s.removeFromLeft(64)); s.removeFromLeft(10);
        oneShot_.setBounds(s.removeFromLeft(90));
        auto e = s.removeFromRight(230);
        stepsBar_.setBounds(e.removeFromRight(100));
        seqOnButton_.setBounds(e.removeFromRight(50).reduced(0, 0)); 
        seqOnButton_.setBounds(seqOnButton_.getBounds().translated(-6, 0));
    }

    // fx panel content: four groups
    {
        auto p = fxPanel_.getLocalBounds();
        fxGroups_.clear();
        const int g4 = 10;
        const int gw[] = { p.getWidth() * 26 / 100, p.getWidth() * 25 / 100, p.getWidth() * 26 / 100, 0 };
        auto place = [&](const String& title, Rectangle<int> area, std::initializer_list<size_t> idx, bool combo) {
            fxGroups_.emplace_back(title, area);
            auto in = area.reduced(8, 6);
            in.removeFromTop(16);
            if (combo) repeatBox_.setBounds(in.removeFromTop(22).withWidth(90));
            const int n = static_cast<int>(idx.size());
            const int w = in.getWidth() / n;
            for (auto i : idx) fxKnobs_[i]->setBounds(in.removeFromLeft(w));
        };
        auto a = p.removeFromLeft(gw[0]); p.removeFromLeft(g4);
        place("PITCH / RELEASE", a, { 0, 1, 2 }, false);
        auto b = p.removeFromLeft(gw[1]); p.removeFromLeft(g4);
        {   // beat repeat: combo in the title row
            fxGroups_.emplace_back("BEAT REPEAT", b);
            auto in = b.reduced(8, 6);
            repeatBox_.setBounds(in.removeFromTop(16).removeFromRight(70).withHeight(18));
            const int w = in.getWidth() / 2;
            fxKnobs_[3]->setBounds(in.removeFromLeft(w));
            fxKnobs_[4]->setBounds(in);
        }
        auto c = p.removeFromLeft(gw[2]); p.removeFromLeft(g4);
        place("REVERB", c, { 5, 6, 7 }, false);
        place("GRAIN SETUP", p, { 8, 9 }, false);
    }
    // lfo panel content
    {
        auto p = lfoPanel_.getLocalBounds();
        const int g = 8;
        const int w = (p.getWidth() - g * (kNumLfos - 1)) / kNumLfos;
        for (int i = 0; i < kNumLfos; ++i)
        {
            auto c = p.removeFromLeft(w);
            p.removeFromLeft(g);
            auto& col_ = lfoCols_[static_cast<size_t>(i)];
            auto in = c.reduced(6, 5);
            in.removeFromTop(16);
            auto k = in.removeFromRight(56);
            col_.shape.setBounds(in.removeFromTop(24)); in.removeFromTop(4);
            col_.rate.setBounds(in.removeFromTop(24)); in.removeFromTop(4);
            col_.target.setBounds(in.removeFromTop(24));
            col_.depth->setBounds(k.withTrimmedTop(6));
        }
    }
}

void MangleEditor::paint(Graphics& g)
{
    g.fillAll(col::window);
    {
        ColourGradient bg(Colour(0xff0d1019), 0.f, 0.f, Colour(0xff07080c), 0.f, static_cast<float>(getHeight()), false);
        g.setGradientFill(bg);
        g.fillAll();
    }
    // logo mark + name in the top-right corner? keep the header clean: draw a small wordmark under the header line.
    auto line = [&](int y, Colour c) {
        ColourGradient gr(c.withAlpha(0.f), static_cast<float>(kPad), 0.f, c.withAlpha(0.f), static_cast<float>(getWidth() - kPad), 0.f, false);
        gr.addColour(0.2, c.withAlpha(0.55f));
        gr.addColour(0.8, c.withAlpha(0.55f));
        g.setGradientFill(gr);
        g.fillRect(static_cast<float>(kPad), static_cast<float>(y), static_cast<float>(getWidth() - 2 * kPad), 1.f);
    };
    line(headerBounds_.getBottom() + 5, col::purple);

    // fx / lfo panel group frames
    if (tab_ == Tab::Fx)
        for (auto& [title, rect] : fxGroups_)
        {
            const auto r = rect.translated(fxPanel_.getX(), fxPanel_.getY()).toFloat();
            g.setColour(col::panel);
            g.fillRoundedRectangle(r, 8.f);
            g.setColour(col::border);
            g.drawRoundedRectangle(r.reduced(0.5f), 8.f, 1.f);
            g.setColour(col::textDim);
            g.setFont(Font(th::font(10.5f, th::Weight::SemiBold)).withExtraKerningFactor(0.12f));
            g.drawText(title, r.reduced(12.f, 6.f).removeFromTop(16.f), Justification::centredLeft);
        }
    if (tab_ == Tab::Lfo)
        for (int i = 0; i < kNumLfos; ++i)
        {
            const auto p = lfoPanel_.getBounds().toFloat();
            const float gapW = 8.f, w = (p.getWidth() - gapW * (kNumLfos - 1)) / kNumLfos;
            const Rectangle<float> r(p.getX() + static_cast<float>(i) * (w + gapW), p.getY(), w, p.getHeight());
            const auto c = th::lfoColour(i);
            g.setColour(col::panel);
            g.fillRoundedRectangle(r, 8.f);
            g.setColour(c.withAlpha(0.45f));
            g.drawRoundedRectangle(r.reduced(0.5f), 8.f, 1.f);
            g.setColour(c);
            g.setFont(Font(th::font(10.5f, th::Weight::SemiBold)).withExtraKerningFactor(0.12f));
            g.drawText("LFO " + String(i + 1), r.reduced(10.f, 6.f).removeFromTop(16.f), Justification::centredLeft);
        }

    // knob sections
    for (auto& s : sections_)
    {
        const auto r = s.bounds.toFloat();
        g.setColour(col::panel);
        g.fillRoundedRectangle(r, 8.f);
        g.setColour(col::border);
        g.drawRoundedRectangle(r.reduced(0.5f), 8.f, 1.f);
        // glowing top edge in the section colour
        ColourGradient gr(s.colour.withAlpha(0.f), r.getX(), 0.f, s.colour.withAlpha(0.f), r.getRight(), 0.f, false);
        gr.addColour(0.5, s.colour.withAlpha(0.9f));
        g.setGradientFill(gr);
        g.fillRect(r.getX() + 6.f, r.getY(), r.getWidth() - 12.f, 1.5f);
        g.setColour(s.colour.withAlpha(0.10f));
        g.fillRect(r.getX() + 20.f, r.getY() + 1.5f, r.getWidth() - 40.f, 6.f);
        g.setColour(s.colour.brighter(0.2f));
        g.setFont(Font(th::font(10.5f, th::Weight::SemiBold)).withExtraKerningFactor(0.14f));
        g.drawText(s.title, r.reduced(12.f, 6.f).removeFromTop(16.f), Justification::centredLeft);
    }

    // footer separator
    line(footerBounds_.getY() - 4, col::blue);
    // info bar
    {
        const auto r = infoLabel_.getBounds().toFloat().expanded(8.f, 2.f);
        g.setColour(Colour(0xff0a0d14));
        g.fillRoundedRectangle(r, 6.f);
        g.setColour(col::border);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    }
    // preset field frame
    {
        const auto r = presetLabel_.getBounds().toFloat();
        g.setColour(col::field);
        g.fillRoundedRectangle(r, 6.f);
        g.setColour(col::border);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    }
}

} // namespace mangle
