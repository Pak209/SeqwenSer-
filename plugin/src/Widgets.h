#pragma once
// Custom controls of the Mangle window: knob with value/name, step grid, waveform view, LFO shape boxes.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "PluginProcessor.h"

namespace mangle {

/** Rotary knob + value read-out + name, attached to one parameter. */
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& name, const juce::String& tip,
         juce::Colour colour, bool bipolar = false);
    void resized() override;
    void refresh();                       // update the value text (call from a timer)
    juce::Slider& slider() { return slider_; }
    const juce::String& title() const { return title_; }
    juce::String valueText() const;

private:
    juce::AudioProcessorValueTreeState& apvts_;
    juce::String id_, title_;
    juce::Slider slider_;
    juce::Label value_, name_;
    std::unique_ptr<juce::SliderParameterAttachment> attach_;
};

/** 4 lanes x 16 steps. Click toggles, drag up/down changes the value, drag sideways paints, wheel nudges, right-click resets. */
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid(MangleProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static juce::Colour laneColour(int lane);
    std::function<void(const juce::String&)> onInfo;   // hover / edit text for the info bar

    juce::Rectangle<float> cellRect(int lane, int step) const;
    bool hit(juce::Point<int> p, int& lane, int& step) const;

private:
    juce::RangedAudioParameter* param(int lane, int step) const;
    float valueOf(int lane, int step) const;
    void setNorm(int lane, int step, float norm);
    float onValue(int lane) const;
    void describe(int lane, int step);

    MangleProcessor& proc_;
    int dragLane_ = -1, dragStep_ = -1, lastPaintStep_ = -1, hoverLane_ = -1, hoverStep_ = -1;
    float startNorm_ = 0.f, paintNorm_ = 0.f;
    bool moved_ = false, verticalMode_ = false, gestureOpen_ = false;
    juce::Point<int> downPos_;
};

/** Waveform of the loaded sample with beat grid, region handles and playhead. Accepts drops via the editor. */
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveformView(MangleProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override { imageDirty_ = true; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void invalidateWaveform() { imageDirty_ = true; repaint(); }
    void setDropHighlight(bool b) { drop_ = b; repaint(); }
    std::function<void()> onLoadClicked;

private:
    void rebuildImage();
    int handleAt(int x) const;   // 0 start, 1 end, -1 none
    MangleProcessor& proc_;
    juce::Image image_;
    bool imageDirty_ = true, drop_ = false;
    int drag_ = -1, hover_ = -1;
};

/** Six small boxes showing each LFO's shape with the live value dot. Click one to open the lfo tab. */
class LfoStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LfoStrip(MangleProcessor& p) : proc_(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(int)> onPick;
    juce::Rectangle<float> boxRect(int i) const;

private:
    MangleProcessor& proc_;
};

} // namespace mangle
