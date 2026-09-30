#pragma once
// Dark, futuristic look: near-black panels, thin glowing separators, metallic knobs with glowing
// value rings in blue / purple / yellow / green. Fonts: Inter (SIL OFL), embedded.

#include <juce_gui_basics/juce_gui_basics.h>

namespace mangle::theme {

namespace col {
inline const juce::Colour window   { 0xff090b10 };
inline const juce::Colour panel    { 0xff0e1118 };
inline const juce::Colour panel2   { 0xff141925 };
inline const juce::Colour field    { 0xff0a0c12 };
inline const juce::Colour border   { 0xff252d40 };
inline const juce::Colour divider  { 0xff1a2030 };
inline const juce::Colour text     { 0xffe7eaf6 };
inline const juce::Colour textDim  { 0xff8a93ab };
inline const juce::Colour textFaint{ 0xff535b72 };
inline const juce::Colour blue     { 0xff3fa7ff };
inline const juce::Colour purple   { 0xffa15cff };
inline const juce::Colour purpleHi { 0xffc9a4ff };
inline const juce::Colour yellow   { 0xffffc93c };
inline const juce::Colour green    { 0xff38e08c };
inline const juce::Colour pink     { 0xffff5fb4 };
inline const juce::Colour orange   { 0xffff8a3d };
inline const juce::Colour cyan     { 0xff5ee8ff };
inline const juce::Colour red      { 0xffff5a5f };
} // namespace col

enum class Weight { Regular, Medium, SemiBold };
juce::FontOptions font(float size, Weight w = Weight::Regular);
juce::Typeface::Ptr typeface(Weight w);

/** Colour of LFO slot i (blue, purple, yellow, green, pink, cyan). */
juce::Colour lfoColour(int i);

namespace icons {
juce::Path gear(juce::Rectangle<float> r);
juce::Path arrowLeft(juce::Rectangle<float> r);
juce::Path arrowRight(juce::Rectangle<float> r);
juce::Path dice(juce::Rectangle<float> r);
juce::Path folder(juce::Rectangle<float> r);
juce::Path logo(juce::Rectangle<float> r);
} // namespace icons

/** Soft glow: a few expanding translucent strokes around a path / rectangle. */
void glowPath(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width);
void glowRect(juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float radius, float strength = 0.10f);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
};

} // namespace mangle::theme
