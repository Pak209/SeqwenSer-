#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

namespace mangle {

class MangleEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit MangleEditor(MangleProcessor&);
    ~MangleEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { waveform_.setDropHighlight(true); }
    void fileDragExit(const juce::StringArray&) override { waveform_.setDropHighlight(false); }

    enum class Tab { Seq = 0, Fx, Lfo };
    void showTab(Tab);
    Tab currentTab() const { return tab_; }
    void chooseFile();
    void openGearMenu();
    void setInfo(const juce::String& s);
    const juce::String& infoText() const { return info_; }

    // for tests
    WaveformView& waveformView() { return waveform_; }
    juce::String fileLabelText() const { return fileButton_.getButtonText(); }
    juce::String fileMetaText() const;

private:
    struct Section
    {
        juce::String title;
        juce::Colour colour;
        juce::Rectangle<int> bounds;
        std::vector<std::unique_ptr<Knob>> knobs;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateFileLabels();
    juce::String hoverInfo();
    void addSectionKnob(Section&, const char* id, const juce::String& name, const juce::String& tip, bool bipolar = false);
    void styleCombo(juce::ComboBox&);
    void styleBar(juce::Slider&, const juce::String& tip);

    MangleProcessor& proc_;
    theme::LookAndFeel laf_;
    Tab tab_ = Tab::Seq;

    // header
    juce::TextButton fileButton_, seqTab_ { "seq" }, fxTab_ { "fx" }, lfoTab_ { "lfo" }, seqOnButton_ { "on" }, diceTop_, loadButton_ { "Load" };
    juce::ComboBox rateBox_, modeBox_;
    juce::Slider stepsBar_, barsBar_, rootBar_;
    juce::ToggleButton oneShot_ { "one-shot" };
    juce::TextEditor bpmField_;
    juce::Label rateLabel_, bpmLabel_, barsLabel_, rootLabel_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> rateAttach_, modeAttach_, filterTypeAttach_;
    std::array<std::unique_ptr<juce::ComboBoxParameterAttachment>, kNumLfos * 3> lfoComboAttach_;
    std::unique_ptr<juce::SliderParameterAttachment> stepsAttach_, barsAttach_, rootAttach_;
    std::unique_ptr<juce::ButtonParameterAttachment> seqOnAttach_, oneShotAttach_;

    // panels
    StepGrid stepGrid_;
    WaveformView waveform_;
    LfoStrip lfoStrip_;
    juce::Component fxPanel_, lfoPanel_;
    std::vector<std::unique_ptr<Knob>> fxKnobs_;
    struct LfoColumn { juce::ComboBox shape, rate, target; std::unique_ptr<Knob> depth; };
    std::array<LfoColumn, kNumLfos> lfoCols_;
    std::array<Section, 4> sections_;
    juce::ComboBox filterTypeBox_, repeatBox_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> repeatAttach_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> fxGroups_;
    juce::Rectangle<int> fileMetaBounds_;

    // footer
    juce::Slider dryWet_;
    juce::Label dryWetLabel_, dryWetValue_, infoLabel_, presetLabel_;
    juce::ShapeButton gearButton_ { "gear", theme::col::textDim, theme::col::text, theme::col::purpleHi };
    juce::ShapeButton prevButton_ { "prev", theme::col::textDim, theme::col::text, theme::col::purpleHi };
    juce::ShapeButton nextButton_ { "next", theme::col::textDim, theme::col::text, theme::col::purpleHi };
    juce::ShapeButton diceButton_ { "dice", theme::col::textDim, theme::col::text, theme::col::purpleHi };
    std::unique_ptr<juce::SliderParameterAttachment> dryWetAttach_;

    juce::String info_, stepInfo_;
    juce::Rectangle<int> headerBounds_, footerBounds_, lfoStripBounds_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ComponentBoundsConstrainer constrainer_;
    juce::String lastPresetShown_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MangleEditor)
};

} // namespace mangle
