#include "Theme.h"

#include "MangleBinaryData.h"

namespace mangle::theme {

using namespace juce;

Typeface::Ptr typeface(Weight w)
{
    static Typeface::Ptr regular = Typeface::createSystemTypefaceFor(MangleBinaryData::InterRegular_ttf, MangleBinaryData::InterRegular_ttfSize);
    static Typeface::Ptr medium = Typeface::createSystemTypefaceFor(MangleBinaryData::InterMedium_ttf, MangleBinaryData::InterMedium_ttfSize);
    static Typeface::Ptr semibold = Typeface::createSystemTypefaceFor(MangleBinaryData::InterSemiBold_ttf, MangleBinaryData::InterSemiBold_ttfSize);
    switch (w)
    {
        case Weight::Medium:   return medium;
        case Weight::SemiBold: return semibold;
        case Weight::Regular:  break;
    }
    return regular;
}

FontOptions font(float size, Weight w)
{
    if (auto tf = typeface(w)) return FontOptions(tf).withHeight(size);
    return FontOptions(size, w == Weight::Regular ? Font::plain : Font::bold);
}

Colour lfoColour(int i)
{
    static const Colour c[] = { col::blue, col::purple, col::yellow, col::green, col::pink, col::cyan };
    return c[((i % 6) + 6) % 6];
}

namespace icons {

Path gear(Rectangle<float> r)
{
    Path p;
    const auto c = r.getCentre();
    const float ro = r.getWidth() * 0.5f, ri = ro * 0.74f;
    const int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a0 = MathConstants<float>::twoPi * static_cast<float>(i) / (teeth * 2) - MathConstants<float>::pi / (teeth * 2);
        const float rad = i % 2 == 0 ? ro : ri;
        const auto pt = c.getPointOnCircumference(rad, a0 + MathConstants<float>::pi / (teeth * 2));
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    p.closeSubPath();
    p.addEllipse(c.x - ro * 0.3f, c.y - ro * 0.3f, ro * 0.6f, ro * 0.6f);
    p.setUsingNonZeroWinding(false);
    return p;
}

Path arrowLeft(Rectangle<float> r)
{
    Path p;
    p.startNewSubPath(r.getRight(), r.getY());
    p.lineTo(r.getX(), r.getCentreY());
    p.lineTo(r.getRight(), r.getBottom());
    return p;
}

Path arrowRight(Rectangle<float> r)
{
    Path p;
    p.startNewSubPath(r.getX(), r.getY());
    p.lineTo(r.getRight(), r.getCentreY());
    p.lineTo(r.getX(), r.getBottom());
    return p;
}

Path dice(Rectangle<float> r)
{
    Path p;
    p.addRoundedRectangle(r, r.getWidth() * 0.22f);
    const float d = r.getWidth() * 0.12f;
    for (auto pt : { Point<float>(0.3f, 0.3f), Point<float>(0.7f, 0.3f), Point<float>(0.5f, 0.5f), Point<float>(0.3f, 0.7f), Point<float>(0.7f, 0.7f) })
        p.addEllipse(r.getX() + pt.x * r.getWidth() - d, r.getY() + pt.y * r.getHeight() - d, d * 2.f, d * 2.f);
    p.setUsingNonZeroWinding(false);
    return p;
}

Path folder(Rectangle<float> r)
{
    Path p;
    const float tab = r.getHeight() * 0.22f;
    p.startNewSubPath(r.getX(), r.getY() + tab);
    p.lineTo(r.getX(), r.getBottom());
    p.lineTo(r.getRight(), r.getBottom());
    p.lineTo(r.getRight(), r.getY() + tab);
    p.lineTo(r.getX() + r.getWidth() * 0.45f, r.getY() + tab);
    p.lineTo(r.getX() + r.getWidth() * 0.36f, r.getY());
    p.lineTo(r.getX(), r.getY());
    p.closeSubPath();
    return p;
}

Path logo(Rectangle<float> r)
{
    // A jagged, "mangled" waveform.
    Path p;
    static const float ys[] = { 0.5f, 0.2f, 0.8f, 0.35f, 0.95f, 0.05f, 0.65f, 0.3f, 0.5f };
    const int n = 9;
    for (int i = 0; i < n; ++i)
    {
        const float x = r.getX() + r.getWidth() * static_cast<float>(i) / (n - 1);
        const float y = r.getY() + r.getHeight() * ys[i];
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    return p;
}

} // namespace icons

void glowPath(Graphics& g, const Path& p, Colour c, float width)
{
    g.setColour(c.withAlpha(0.10f));
    g.strokePath(p, PathStrokeType(width * 4.f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(c.withAlpha(0.18f));
    g.strokePath(p, PathStrokeType(width * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(c);
    g.strokePath(p, PathStrokeType(width, PathStrokeType::curved, PathStrokeType::rounded));
}

void glowRect(Graphics& g, Rectangle<float> r, float corner, Colour c, float radius, float strength)
{
    const int steps = 5;
    for (int i = steps; i >= 1; --i)
    {
        const float grow = radius * static_cast<float>(i) / steps;
        g.setColour(c.withMultipliedAlpha(strength * (1.f - static_cast<float>(i - 1) / steps)));
        g.fillRoundedRectangle(r.expanded(grow), corner + grow);
    }
}

// ---- LookAndFeel ----------------------------------------------------------------------------------

LookAndFeel::LookAndFeel()
{
    setColour(ResizableWindow::backgroundColourId, col::window);
    setColour(PopupMenu::backgroundColourId, col::panel2);
    setColour(PopupMenu::textColourId, col::text);
    setColour(PopupMenu::highlightedBackgroundColourId, Colour(0xff2a2250));
    setColour(TooltipWindow::backgroundColourId, col::panel2);
    setColour(TooltipWindow::textColourId, col::text);
    setColour(Label::textColourId, col::text);
    setColour(TextEditor::backgroundColourId, col::field);
    setColour(TextEditor::textColourId, col::text);
    setColour(TextEditor::outlineColourId, col::border);
    setColour(TextEditor::focusedOutlineColourId, col::purple);
    setColour(Slider::textBoxTextColourId, col::text);
    setColour(ComboBox::textColourId, col::text);
}

Typeface::Ptr LookAndFeel::getTypefaceForFont(const Font& f)
{
    return typeface(f.isBold() ? Weight::SemiBold : Weight::Regular);
}

void LookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    const bool accent = b.getProperties()["accent"];
    const Colour ac = Colour(static_cast<uint32>(static_cast<int>(b.getProperties().getWithDefault("colour", static_cast<int>(col::purple.getARGB())))));
    if (on || accent)
    {
        glowRect(g, r, 6.f, ac, 5.f, 0.12f);
        g.setColour(ac.withAlpha(on ? 0.22f : 0.14f));
    }
    else g.setColour(col::field);
    g.fillRoundedRectangle(r, 6.f);
    g.setColour((on || accent) ? ac.withAlpha(down ? 1.f : 0.85f) : (over ? col::border.brighter(0.3f) : col::border));
    g.drawRoundedRectangle(r, 6.f, 1.f);
}

Font LookAndFeel::getTextButtonFont(TextButton& b, int h)
{
    return Font(font(jmin(12.5f, h * 0.45f), b.getProperties()["accent"] ? Weight::SemiBold : Weight::Medium));
}

void LookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool, bool)
{
    const Colour ac = Colour(static_cast<uint32>(static_cast<int>(b.getProperties().getWithDefault("colour", static_cast<int>(col::purple.getARGB())))));
    g.setFont(getTextButtonFont(b, b.getHeight()).withExtraKerningFactor(0.05f));
    Colour c = (b.getToggleState() || b.getProperties()["accent"]) ? ac.brighter(0.5f) : col::text;
    if (!b.isEnabled()) c = c.withMultipliedAlpha(0.4f);
    g.setColour(c);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(6, 2), Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    Rectangle<float> r(0.5f, 0.5f, w - 1.f, h - 1.f);
    g.setColour(col::field);
    g.fillRoundedRectangle(r, 6.f);
    g.setColour(box.isMouseOver(true) ? col::border.brighter(0.3f) : col::border);
    g.drawRoundedRectangle(r, 6.f, 1.f);
    Path a;
    const auto ar = Rectangle<float>(static_cast<float>(w) - 19.f, h * 0.5f - 2.5f, 9.f, 5.f);
    a.startNewSubPath(ar.getX(), ar.getY());
    a.lineTo(ar.getCentreX(), ar.getBottom());
    a.lineTo(ar.getRight(), ar.getY());
    g.setColour(col::textDim);
    g.strokePath(a, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font LookAndFeel::getComboBoxFont(ComboBox&) { return Font(font(12.5f, Weight::Medium)); }

void LookAndFeel::positionComboBoxText(ComboBox& box, Label& l)
{
    l.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    l.setFont(getComboBoxFont(box));
}

void LookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int>(x, y, w, h).toFloat();
    const float d = jmin(bounds.getWidth(), bounds.getHeight()) - 6.f;
    const auto c = bounds.getCentre();
    const float ringR = d * 0.5f;
    const Colour ac = Colour(static_cast<uint32>(static_cast<int>(s.getProperties().getWithDefault("colour", static_cast<int>(col::blue.getARGB())))));
    const bool bipolar = s.getProperties()["bipolar"];
    const float angle = start + pos * (end - start);
    const float mid = start + 0.5f * (end - start);

    // glowing value ring
    const float arcR = ringR - 2.f;
    Path track;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.f, start, end, true);
    g.setColour(Colour(0xff1b2232));
    g.strokePath(track, PathStrokeType(3.f, PathStrokeType::curved, PathStrokeType::rounded));
    Path arc;
    const float a0 = bipolar ? jmin(mid, angle) : start, a1 = bipolar ? jmax(mid, angle) : angle;
    if (a1 - a0 > 0.001f)
    {
        arc.addCentredArc(c.x, c.y, arcR, arcR, 0.f, a0, a1, true);
        if (s.isEnabled()) glowPath(g, arc, ac, 3.f);
    }
    // metallic body
    const float r = ringR - 7.f;
    g.setColour(Colour(0x66000000));
    g.fillEllipse(c.x - r - 1.f, c.y - r + 2.f, r * 2.f + 2.f, r * 2.f + 2.f);
    ColourGradient body(Colour(0xffdfe3ee), c.x - r * 0.4f, c.y - r * 0.8f, Colour(0xff3d4458), c.x + r * 0.5f, c.y + r, false);
    body.addColour(0.45, Colour(0xff9299aa));
    g.setGradientFill(body);
    g.fillEllipse(c.x - r, c.y - r, r * 2.f, r * 2.f);
    g.setColour(Colour(0xff0a0c12));
    g.drawEllipse(c.x - r, c.y - r, r * 2.f, r * 2.f, 1.2f);
    // inner cap
    const float rc = r * 0.72f;
    ColourGradient cap(Colour(0xff6b7387), c.x, c.y - rc, Colour(0xff2a3042), c.x, c.y + rc, false);
    g.setGradientFill(cap);
    g.fillEllipse(c.x - rc, c.y - rc, rc * 2.f, rc * 2.f);
    // pointer
    const auto tip = c.getPointOnCircumference(r * 0.95f, angle);
    const auto in = c.getPointOnCircumference(r * 0.42f, angle);
    g.setColour(Colour(0xff10131b));
    g.drawLine(Line<float>(in, tip), 2.6f);
    g.setColour(ac.brighter(0.3f));
    g.drawLine(Line<float>(in + (tip - in) * 0.3f, tip), 1.6f);
}

void LookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0.f, 0.f, style, s);
        return;
    }
    const Colour ac = Colour(static_cast<uint32>(static_cast<int>(s.getProperties().getWithDefault("colour", static_cast<int>(col::purple.getARGB())))));
    const float cy = y + h * 0.5f;
    Rectangle<float> track(static_cast<float>(x), cy - 2.5f, static_cast<float>(w), 5.f);
    g.setColour(Colour(0xff1b2232));
    g.fillRoundedRectangle(track, 2.5f);
    auto fill = track.withRight(jlimit(track.getX(), track.getRight(), pos));
    glowRect(g, fill, 2.5f, ac, 4.f, 0.12f);
    g.setColour(ac);
    g.fillRoundedRectangle(fill, 2.5f);
    const float kx = jlimit(track.getX() + 6.f, track.getRight() - 6.f, pos);
    ColourGradient body(Colour(0xffdfe3ee), kx, cy - 7.f, Colour(0xff4a5268), kx, cy + 7.f, false);
    g.setGradientFill(body);
    g.fillEllipse(kx - 7.f, cy - 7.f, 14.f, 14.f);
    g.setColour(Colour(0xff0a0c12));
    g.drawEllipse(kx - 7.f, cy - 7.f, 14.f, 14.f, 1.f);
}

Label* LookAndFeel::createSliderTextBox(Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(font(12.f, Weight::Medium));
    return l;
}

void LookAndFeel::drawPopupMenuBackground(Graphics& g, int w, int h)
{
    g.fillAll(col::panel2);
    g.setColour(col::border);
    g.drawRect(0, 0, w, h, 1);
}

Font LookAndFeel::getPopupMenuFont() { return Font(font(13.f, Weight::Medium)); }

void LookAndFeel::drawPopupMenuItem(Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                    bool isTicked, bool hasSubMenu, const String& text, const String& shortcut,
                                    const Drawable* icon, const Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour(col::divider);
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(Colour(0xff2a2250));
        g.fillRect(area);
    }
    g.setColour(!isActive ? col::textFaint : (textColour != nullptr ? *textColour : col::text));
    g.setFont(getPopupMenuFont());
    auto r = area.reduced(14, 0);
    if (isTicked) { g.setColour(col::purple); g.fillEllipse(static_cast<float>(area.getX()) + 5.f, static_cast<float>(area.getCentreY()) - 2.5f, 5.f, 5.f); g.setColour(col::text); }
    g.drawFittedText(text, r, Justification::centredLeft, 1);
    if (shortcut.isNotEmpty()) { g.setColour(col::textDim); g.drawText(shortcut, r, Justification::centredRight); }
    juce::ignoreUnused(hasSubMenu, icon);
}

void LookAndFeel::drawTooltip(Graphics& g, const String& text, int w, int h)
{
    auto r = Rectangle<float>(0.5f, 0.5f, w - 1.f, h - 1.f);
    g.setColour(col::panel2);
    g.fillRoundedRectangle(r, 5.f);
    g.setColour(col::border.brighter(0.2f));
    g.drawRoundedRectangle(r, 5.f, 1.f);
    g.setColour(col::text);
    g.setFont(font(12.f));
    g.drawFittedText(text, r.reduced(7.f, 3.f).toNearestInt(), Justification::centredLeft, 6);
}

} // namespace mangle::theme
