#include "Widgets.h"

#include "Theme.h"
#include "mangle/Dsp.h"

namespace mangle {

using namespace juce;
namespace th = mangle::theme;

// ---- Knob ----------------------------------------------------------------------------------------------
Knob::Knob(AudioProcessorValueTreeState& a, const String& id, const String& name, const String& tip, Colour colour, bool bipolar)
    : apvts_(a), id_(id), title_(name)
{
    setTooltip(tip);
    slider_.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
    slider_.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    slider_.getProperties().set("colour", static_cast<int>(colour.getARGB()));
    slider_.getProperties().set("bipolar", bipolar);
    slider_.setTooltip(tip);
    slider_.setMouseDragSensitivity(220);
    addAndMakeVisible(slider_);
    attach_ = std::make_unique<SliderParameterAttachment>(*a.getParameter(id), slider_, nullptr);

    for (auto* l : { &value_, &name_ })
    {
        l->setJustificationType(Justification::centred);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    value_.setFont(th::font(11.5f, th::Weight::Medium));
    value_.setColour(Label::textColourId, th::col::text);
    name_.setFont(Font(th::font(10.5f, th::Weight::Medium)).withExtraKerningFactor(0.06f));
    name_.setColour(Label::textColourId, th::col::textDim);
    name_.setText(name, dontSendNotification);
    refresh();
}

String Knob::valueText() const
{
    auto* p = apvts_.getParameter(id_);
    String t = p->getText(p->getValue(), 16);
    const auto label = p->getLabel();
    if (label.isNotEmpty() && !t.endsWithIgnoreCase(label)) t << " " << label;
    return t;
}

void Knob::refresh()
{
    const auto t = valueText();
    if (value_.getText() != t) value_.setText(t, dontSendNotification);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name_.setBounds(r.removeFromBottom(14));
    value_.setBounds(r.removeFromBottom(15));
    slider_.setBounds(r.withSizeKeepingCentre(jmin(r.getWidth(), r.getHeight()), jmin(r.getWidth(), r.getHeight())));
}

// ---- StepGrid -----------------------------------------------------------------------------------------------
Colour StepGrid::laneColour(int lane)
{
    static const Colour c[] = { th::col::blue, th::col::orange, th::col::green, th::col::pink };
    return c[jlimit(0, 3, lane)];
}

StepGrid::StepGrid(MangleProcessor& p) : proc_(p)
{
    setTooltip("Step sequencer. Click a step to switch it on or off, drag up/down to set its value, drag sideways to paint, right-click to reset.");
}

static constexpr float kLabelW = 64.f;

Rectangle<float> StepGrid::cellRect(int lane, int step) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft(kLabelW);
    const float gap = 3.f, rowGap = 4.f;
    const float w = (area.getWidth() - gap * (kMaxSteps - 1)) / kMaxSteps;
    const float h = (area.getHeight() - rowGap * (kNumLanes - 1)) / kNumLanes;
    return { area.getX() + static_cast<float>(step) * (w + gap), area.getY() + static_cast<float>(lane) * (h + rowGap), w, h };
}

bool StepGrid::hit(Point<int> p, int& lane, int& step) const
{
    for (int l = 0; l < kNumLanes; ++l)
        for (int s = 0; s < kMaxSteps; ++s)
            if (cellRect(l, s).expanded(1.5f, 2.f).contains(p.toFloat())) { lane = l; step = s; return true; }
    return false;
}

RangedAudioParameter* StepGrid::param(int lane, int step) const
{
    return dynamic_cast<RangedAudioParameter*>(proc_.apvts().getParameter(params::stepId(lane, step)));
}

float StepGrid::valueOf(int lane, int step) const
{
    auto* p = param(lane, step);
    return p->convertFrom0to1(p->getValue());
}

float StepGrid::onValue(int lane) const
{
    switch (lane)
    {
        case LaneGate: return 1.f;
        case LanePitch: return 5.f / 12.f;
        case LaneFilter: return 0.5f;
        default: return 3.f / 6.f;   // 1/16
    }
}

void StepGrid::setNorm(int lane, int step, float norm)
{
    auto* p = param(lane, step);
    const float v = p->convertFrom0to1(jlimit(0.f, 1.f, norm));
    const float snapped = p->getNormalisableRange().snapToLegalValue(v);
    p->setValueNotifyingHost(p->convertTo0to1(snapped));
}

void StepGrid::describe(int lane, int step)
{
    static const char* names[] = { "GATE", "PITCH", "FILTER", "REPEAT" };
    if (onInfo)
        onInfo(String(names[lane]) + " step " + String(step + 1) + ": " + params::stepText(lane, valueOf(lane, step))
               + (lane == LaneGate ? "  -  click to toggle, drag up/down for level"
                  : lane == LanePitch ? "  -  drag up/down to transpose this step (+/-12 semitones)"
                  : lane == LaneFilter ? "  -  drag up/down to open the filter on this step"
                                       : "  -  drag up/down to choose the beat-repeat division for this step"));
}

void StepGrid::paint(Graphics& g)
{
    static const char* names[] = { "gate", "pitch", "filter", "repeat" };
    const int length = jlimit(1, kMaxSteps, roundToInt(proc_.refs().get(params::seqLength)));
    const bool seqOn = proc_.refs().get(params::seqOn) >= 0.5f;
    const int playing = proc_.live().playing.load() ? proc_.live().step.load() : -1;

    for (int l = 0; l < kNumLanes; ++l)
    {
        const auto c = laneColour(l);
        const auto row = cellRect(l, 0);
        g.setColour(c.withAlpha(seqOn ? 0.9f : 0.4f));
        g.setFont(Font(th::font(11.f, th::Weight::SemiBold)).withExtraKerningFactor(0.08f));
        g.drawText(names[l], Rectangle<float>(0.f, row.getY(), kLabelW - 6.f, row.getHeight()), Justification::centredLeft);

        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto r = cellRect(l, s);
            const bool inRange = s < length;
            const float v = valueOf(l, s);
            const float alpha = (inRange && seqOn) ? 1.f : 0.3f;
            g.setColour((s / 4) % 2 == 0 ? Colour(0xff10141d) : Colour(0xff0c1018));
            g.fillRoundedRectangle(r, 3.f);
            g.setColour(th::col::divider.brighter(0.15f).withAlpha(alpha));
            g.drawRoundedRectangle(r.reduced(0.5f), 3.f, 1.f);

            Rectangle<float> fill;
            bool on = false;
            switch (l)
            {
                case LaneGate:   on = v > 0.001f; fill = r.withTrimmedTop(r.getHeight() * (1.f - v)); break;
                case LaneFilter: on = v > 0.001f; fill = r.withTrimmedTop(r.getHeight() * (1.f - v)); break;
                case LanePitch:
                {
                    on = lanePitchSemis(v) != 0;
                    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
                    fill = v >= 0 ? Rectangle<float>(r.getX(), mid - half * v, r.getWidth(), half * v)
                                  : Rectangle<float>(r.getX(), mid, r.getWidth(), half * -v);
                    break;
                }
                default: on = laneRepeatIndex(v) > 0; fill = r; break;
            }
            if (on && fill.getHeight() > 0.5f)
            {
                if (inRange && seqOn) th::glowRect(g, fill.reduced(1.f), 2.f, c, 4.f, 0.07f);
                g.setColour(c.withAlpha((l == LaneRepeat ? 0.55f : 0.85f) * alpha));
                g.fillRoundedRectangle(fill.reduced(1.f), 2.f);
            }
            if (l == LanePitch)
            {
                g.setColour(th::col::border.withAlpha(0.7f * alpha));
                g.drawHorizontalLine(roundToInt(r.getCentreY()), r.getX() + 2.f, r.getRight() - 2.f);
            }
            if (on && (l == LanePitch || l == LaneRepeat))
            {
                g.setColour(Colours::white.withAlpha(0.95f * alpha));
                g.setFont(th::font(9.5f, th::Weight::SemiBold));
                const String t = l == LanePitch ? ((lanePitchSemis(v) > 0 ? "+" : "") + String(lanePitchSemis(v)))
                                                : params::kRepeatNames[laneRepeatIndex(v)];
                g.drawText(t, r, Justification::centred);
            }
            if (s == playing && inRange && seqOn)
            {
                g.setColour(Colours::white.withAlpha(0.9f));
                g.drawRoundedRectangle(r.reduced(0.5f), 3.f, 1.6f);
                g.setColour(Colours::white.withAlpha(0.08f));
                g.fillRoundedRectangle(r, 3.f);
            }
            if (l == hoverLane_ && s == hoverStep_)
            {
                g.setColour(Colours::white.withAlpha(0.25f));
                g.drawRoundedRectangle(r.reduced(0.5f), 3.f, 1.f);
            }
        }
    }
}

void StepGrid::mouseMove(const MouseEvent& e)
{
    int l, s;
    const bool h = hit(e.getPosition(), l, s);
    if (!h) { mouseExit(e); return; }
    if (l != hoverLane_ || s != hoverStep_) { hoverLane_ = l; hoverStep_ = s; repaint(); describe(l, s); }
}

void StepGrid::mouseExit(const MouseEvent&)
{
    if (hoverLane_ >= 0) { hoverLane_ = hoverStep_ = -1; repaint(); }
}

void StepGrid::mouseDown(const MouseEvent& e)
{
    int l, s;
    if (!hit(e.getPosition(), l, s)) return;
    dragLane_ = l; dragStep_ = s; lastPaintStep_ = s;
    downPos_ = e.getPosition();
    moved_ = verticalMode_ = false;
    auto* p = param(l, s);
    startNorm_ = p->getValue();
    p->beginChangeGesture();
    gestureOpen_ = true;
    if (e.mods.isPopupMenu())
    {
        p->setValueNotifyingHost(p->getDefaultValue());
        paintNorm_ = p->getDefaultValue();
        moved_ = true;   // no toggle on mouse up
        describe(l, s);
        repaint();
        return;
    }
    const bool isOn = l == LaneGate ? valueOf(l, s) > 0.001f : l == LanePitch ? lanePitchSemis(valueOf(l, s)) != 0
                                     : l == LaneFilter ? valueOf(l, s) > 0.001f : laneRepeatIndex(valueOf(l, s)) > 0;
    const float offNorm = l == LaneGate ? 0.f : p->convertTo0to1(0.f);
    const float onNorm = p->convertTo0to1(onValue(l));
    paintNorm_ = isOn ? offNorm : onNorm;
}

void StepGrid::mouseDrag(const MouseEvent& e)
{
    if (dragLane_ < 0 || e.mods.isPopupMenu()) return;
    const auto d = e.getPosition() - downPos_;
    if (!moved_ && (std::abs(d.x) > 4 || std::abs(d.y) > 4))
    {
        moved_ = true;
        verticalMode_ = std::abs(d.y) >= std::abs(d.x);
    }
    if (!moved_) return;
    if (verticalMode_)
    {
        const float range = 90.f;
        setNorm(dragLane_, dragStep_, startNorm_ - static_cast<float>(e.getDistanceFromDragStartY()) / range);
        describe(dragLane_, dragStep_);
    }
    else
    {
        int l, s;
        if (hit(e.getPosition(), l, s) && l == dragLane_ && s != lastPaintStep_)
        {
            // paint: the first cell gets the toggled value on first horizontal move
            if (lastPaintStep_ == dragStep_) setNorm(dragLane_, dragStep_, paintNorm_);
            auto* p = param(l, s);
            p->beginChangeGesture();
            setNorm(l, s, paintNorm_);
            p->endChangeGesture();
            lastPaintStep_ = s;
        }
    }
    repaint();
}

void StepGrid::mouseUp(const MouseEvent&)
{
    if (dragLane_ < 0) return;
    if (!moved_) setNorm(dragLane_, dragStep_, paintNorm_);   // plain click = toggle
    if (gestureOpen_) param(dragLane_, dragStep_)->endChangeGesture();
    gestureOpen_ = false;
    describe(dragLane_, dragStep_);
    dragLane_ = dragStep_ = -1;
    repaint();
}

void StepGrid::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w)
{
    int l, s;
    if (!hit(e.getPosition(), l, s)) return;
    auto* p = param(l, s);
    const float q = l == LanePitch ? 1.f / 24.f : l == LaneRepeat ? 1.f / 6.f : 0.05f;
    p->beginChangeGesture();
    setNorm(l, s, p->getValue() + (w.deltaY > 0 ? q : -q));
    p->endChangeGesture();
    describe(l, s);
    repaint();
}

// ---- WaveformView -----------------------------------------------------------------------------------------
WaveformView::WaveformView(MangleProcessor& p) : proc_(p)
{
    setTooltip("Drop an audio file here. Drag the bright markers to choose which part plays; double-click the waveform to load a file.");
}

int WaveformView::handleAt(int x) const
{
    const float w = static_cast<float>(getWidth());
    const float a = proc_.refs().get(params::sampleStart) * w, b = proc_.refs().get(params::sampleEnd) * w;
    if (std::abs(static_cast<float>(x) - a) < 8.f && std::abs(static_cast<float>(x) - a) <= std::abs(static_cast<float>(x) - b)) return 0;
    if (std::abs(static_cast<float>(x) - b) < 8.f) return 1;
    return -1;
}

void WaveformView::mouseMove(const MouseEvent& e)
{
    const int h = handleAt(e.x);
    if (h != hover_) { hover_ = h; setMouseCursor(h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor); repaint(); }
}

void WaveformView::mouseDown(const MouseEvent& e)
{
    drag_ = proc_.sampleInfo().loaded ? handleAt(e.x) : -1;
    if (drag_ >= 0)
        if (auto* p = proc_.apvts().getParameter(drag_ == 0 ? params::sampleStart : params::sampleEnd)) p->beginChangeGesture();
}

void WaveformView::mouseDrag(const MouseEvent& e)
{
    if (drag_ < 0) return;
    const float x = jlimit(0.f, 1.f, static_cast<float>(e.x) / static_cast<float>(jmax(1, getWidth())));
    const float other = proc_.refs().get(drag_ == 0 ? params::sampleEnd : params::sampleStart);
    const float v = drag_ == 0 ? jmin(x, other - 0.01f) : jmax(x, other + 0.01f);
    if (auto* p = proc_.apvts().getParameter(drag_ == 0 ? params::sampleStart : params::sampleEnd)) p->setValueNotifyingHost(jlimit(0.f, 1.f, v));
    repaint();
}

void WaveformView::mouseUp(const MouseEvent&)
{
    if (drag_ >= 0)
        if (auto* p = proc_.apvts().getParameter(drag_ == 0 ? params::sampleStart : params::sampleEnd)) p->endChangeGesture();
    drag_ = -1;
}

void WaveformView::mouseDoubleClick(const MouseEvent& e)
{
    if (handleAt(e.x) < 0 && onLoadClicked) onLoadClicked();
}

void WaveformView::rebuildImage()
{
    const float scale = 1.f;
    const int w = jmax(1, getWidth()), h = jmax(1, getHeight());
    image_ = Image(Image::ARGB, roundToInt(static_cast<float>(w) * scale), roundToInt(static_cast<float>(h) * scale), true);
    Graphics g(image_);
    const auto& wf = proc_.waveform();
    if (wf.empty()) { imageDirty_ = false; return; }
    const int cols = static_cast<int>(wf.mins.size());
    const float mid = static_cast<float>(h) * 0.5f, amp = static_cast<float>(h) * 0.46f;
    Path outline;
    std::vector<std::pair<float, float>> px(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x)
    {
        const int c0 = x * cols / w, c1 = jmax(c0 + 1, (x + 1) * cols / w);
        float lo = 0.f, hi = 0.f;
        for (int c = c0; c < c1 && c < cols; ++c) { lo = jmin(lo, wf.mins[static_cast<size_t>(c)]); hi = jmax(hi, wf.maxs[static_cast<size_t>(c)]); }
        px[static_cast<size_t>(x)] = { lo, hi };
    }
    // find a display gain so quiet files still fill the view
    float peak = 0.05f;
    for (auto& p : px) peak = jmax(peak, jmax(-p.first, p.second));
    const float gain = jmin(1.f / peak, 8.f);
    const auto purple = th::col::purple;
    for (int pass = 0; pass < 2; ++pass)
    {
        g.setColour(pass == 0 ? purple.withAlpha(0.25f) : purple.withAlpha(0.95f));
        for (int x = 0; x < w; ++x)
        {
            const float y0 = mid - px[static_cast<size_t>(x)].second * gain * amp - (pass == 0 ? 2.f : 0.f);
            const float y1 = mid - px[static_cast<size_t>(x)].first * gain * amp + (pass == 0 ? 2.f : 0.f);
            g.fillRect(static_cast<float>(x), y0, pass == 0 ? 1.6f : 1.f, jmax(1.f, y1 - y0));
        }
    }
    g.setColour(th::col::purpleHi.withAlpha(0.55f));
    for (int x = 0; x < w; ++x)
    {
        const float y0 = mid - px[static_cast<size_t>(x)].second * gain * amp * 0.55f;
        const float y1 = mid - px[static_cast<size_t>(x)].first * gain * amp * 0.55f;
        g.fillRect(static_cast<float>(x), y0, 1.f, jmax(1.f, y1 - y0));
    }
    imageDirty_ = false;
}

void WaveformView::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(Colour(0xff0a0d14));
    g.fillRoundedRectangle(b, 6.f);
    const auto& info = proc_.sampleInfo();
    const float w = b.getWidth(), h = b.getHeight();
    const float s0 = proc_.refs().get(params::sampleStart), s1 = proc_.refs().get(params::sampleEnd);
    const float regionX0 = s0 * w, regionX1 = s1 * w;

    // grid: beats of the loop across the selected region
    const float beats = proc_.refs().get(params::loopBars) * 4.f;
    const int nBeats = jlimit(1, 512, roundToInt(beats));
    const bool fine = nBeats <= 64;
    for (int i = 0; i <= nBeats; ++i)
    {
        const float x = regionX0 + (regionX1 - regionX0) * static_cast<float>(i) / static_cast<float>(nBeats);
        const bool bar = i % 4 == 0;
        if (!fine && !bar) continue;
        g.setColour(Colour(bar ? 0xff2a3246 : 0xff1a2030).withAlpha(bar ? 0.9f : 0.8f));
        g.drawVerticalLine(roundToInt(x), 0.f, h);
    }
    g.setColour(Colour(0xff222a3c));
    g.drawHorizontalLine(roundToInt(h * 0.5f), 0.f, w);
    g.setColour(Colour(0xff161c29));
    g.drawHorizontalLine(roundToInt(h * 0.25f), 0.f, w);
    g.drawHorizontalLine(roundToInt(h * 0.75f), 0.f, w);

    if (info.loaded && !proc_.waveform().empty())
    {
        if (imageDirty_ || image_.getWidth() != getWidth() || image_.getHeight() != getHeight()) rebuildImage();
        g.drawImageAt(image_, 0, 0);
        // dim outside the region
        g.setColour(Colour(0xcc090b10));
        g.fillRect(0.f, 0.f, regionX0, h);
        g.fillRect(regionX1, 0.f, w - regionX1, h);
        for (int hd = 0; hd < 2; ++hd)
        {
            const float x = hd == 0 ? regionX0 : regionX1;
            const bool active = hover_ == hd || drag_ == hd;
            g.setColour(th::col::yellow.withAlpha(active ? 1.f : 0.65f));
            g.fillRect(x - 1.f, 0.f, 2.f, h);
            g.fillRect(hd == 0 ? x : x - 7.f, 0.f, 7.f, 3.f);
            g.fillRect(hd == 0 ? x : x - 7.f, h - 3.f, 7.f, 3.f);
        }
        // playhead
        if (proc_.live().playing.load())
        {
            const float ph = jlimit(0.f, 1.f, proc_.live().phase.load());
            const float x = regionX0 + (regionX1 - regionX0) * ph;
            g.setColour(th::col::cyan.withAlpha(0.10f));
            g.fillRect(x - 5.f, 0.f, 10.f, h);
            g.setColour(th::col::cyan.withAlpha(0.25f));
            g.fillRect(x - 2.5f, 0.f, 5.f, h);
            g.setColour(th::col::cyan);
            g.fillRect(x - 0.75f, 0.f, 1.5f, h);
            Path tri;
            tri.addTriangle(x - 5.f, 0.f, x + 5.f, 0.f, x, 7.f);
            g.fillPath(tri);
        }
    }
    else
    {
        g.setColour(th::col::textDim);
        g.setFont(th::font(15.f, th::Weight::Medium));
        String t = info.loading ? "Loading..." : "Drop an audio file here";
        g.drawText(t, getLocalBounds().withTrimmedBottom(18), Justification::centred);
        g.setColour(th::col::textFaint);
        g.setFont(th::font(11.5f));
        g.drawText(info.loading ? String() : String("WAV, AIFF, FLAC, MP3, M4A  -  or click Load"), getLocalBounds().withTrimmedTop(24), Justification::centred);
    }
    if (info.message.isNotEmpty())
    {
        auto bar = getLocalBounds().removeFromBottom(22).toFloat();
        g.setColour(Colour(0xdd090b10));
        g.fillRect(bar);
        g.setColour(th::col::yellow);
        g.setFont(th::font(11.5f, th::Weight::Medium));
        g.drawText(info.message, bar.reduced(8.f, 0.f), Justification::centredLeft, true);
    }
    g.setColour(drop_ ? th::col::purple : th::col::border);
    g.drawRoundedRectangle(b.reduced(0.5f), 6.f, drop_ ? 2.f : 1.f);
    if (drop_) th::glowRect(g, b.reduced(1.f), 6.f, th::col::purple, 6.f, 0.05f);
}

// ---- LfoStrip -----------------------------------------------------------------------------------------------
Rectangle<float> LfoStrip::boxRect(int i) const
{
    const float gap = 10.f;
    const float w = (static_cast<float>(getWidth()) - gap * (kNumLfos - 1)) / kNumLfos;
    return { static_cast<float>(i) * (w + gap), 0.f, w, static_cast<float>(getHeight()) };
}

void LfoStrip::mouseDown(const MouseEvent& e)
{
    for (int i = 0; i < kNumLfos; ++i)
        if (boxRect(i).contains(e.position) && onPick) onPick(i);
}

void LfoStrip::paint(Graphics& g)
{
    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto r = boxRect(i);
        const auto c = th::lfoColour(i);
        const float depth = proc_.refs().get(params::lfoId(i, "depth").toRawUTF8());
        const int shape = roundToInt(proc_.refs().get(params::lfoId(i, "shape").toRawUTF8()));
        const int rate = roundToInt(proc_.refs().get(params::lfoId(i, "rate").toRawUTF8()));
        const int target = roundToInt(proc_.refs().get(params::lfoId(i, "target").toRawUTF8()));
        const bool active = std::abs(depth) > 0.001f && target > 0;
        const float a = active ? 1.f : 0.35f;

        if (active) th::glowRect(g, r, 5.f, c, 5.f, 0.10f);
        g.setColour(Colour(0xff0a0d14));
        g.fillRoundedRectangle(r, 5.f);
        g.setColour(c.withAlpha(active ? 0.9f : 0.35f));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.f, 1.f);

        // number of cycles per sequencer-bar hint: shorter periods draw more cycles (x2, x4)
        const double periodBeats = lfoBeatsForRate(rate);
        const int cycles = periodBeats <= 1.0 ? 4 : periodBeats <= 4.0 ? 2 : 1;
        const auto inner = r.reduced(6.f, 8.f);
        Path p;
        const int N = 96;
        for (int k = 0; k <= N; ++k)
        {
            const double ph = static_cast<double>(k) / N * cycles;
            // evaluate as a function of the period position (beats), so all shapes match the DSP
            const float v = lfoValue(shape, ph * periodBeats, periodBeats) * (active ? jmax(0.3f, std::abs(depth)) : 0.6f) * (depth < 0 ? -1.f : 1.f);
            const float x = inner.getX() + inner.getWidth() * static_cast<float>(k) / N;
            const float y = inner.getCentreY() - v * inner.getHeight() * 0.5f;
            if (k == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.saveState();
        g.reduceClipRegion(r.toNearestInt());
        g.setColour(c.withAlpha(0.35f * a));
        g.strokePath(p, PathStrokeType(3.6f));
        g.setColour(c.withAlpha(a));
        g.strokePath(p, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.restoreState();

        if (active)
        {
            const float lv = proc_.live().lfo[static_cast<size_t>(i)].load() * (depth < 0 ? -1.f : 1.f) * jmax(0.3f, std::abs(depth));
            const float y = inner.getCentreY() - lv * inner.getHeight() * 0.5f;
            g.setColour(c.withAlpha(0.35f));
            g.fillEllipse(r.getRight() - 14.f, y - 6.f, 12.f, 12.f);
            g.setColour(Colours::white);
            g.fillEllipse(r.getRight() - 11.f, y - 3.f, 6.f, 6.f);
        }
        g.setFont(th::font(10.f, th::Weight::SemiBold));
        g.setColour(c.withAlpha(active ? 1.f : 0.6f));
        if (cycles > 1) g.drawText("x" + String(cycles), r.reduced(6.f, 3.f), Justification::topLeft);
        g.setColour(active ? th::col::textDim : th::col::textFaint);
        g.setFont(th::font(9.5f, th::Weight::Medium));
        g.drawText(active ? params::kTargetNames[target] : String("off"), r.reduced(6.f, 3.f), Justification::bottomRight);
    }
}

} // namespace mangle
