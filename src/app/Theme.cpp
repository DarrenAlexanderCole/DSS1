#include "Theme.h"

namespace dss1::ui
{

namespace
{
    const juce::Identifier styleId ("hwStyle"), padColourId ("hwPadColour"), padLitId ("hwPadLit"), scaleId ("hwScale");

    KeyStyle styleOf (const juce::Button& b)
    {
        return static_cast<KeyStyle> (static_cast<int> (b.getProperties().getWithDefault (styleId, 0)));
    }

    /// A square hardware key: a bevelled cap sitting on a black shadow that shrinks when pressed.
    void drawKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, bool down, bool over)
    {
        const auto travel = down ? 2.0f : 0.0f;
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (r.translated (0, 3.5f), 2.5f);
        auto cap = r.withTrimmedBottom (3.0f).translated (0, travel);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (cap, 2.5f);
        cap = cap.reduced (1.0f);
        juce::ColourGradient body (over ? top.brighter (0.08f) : top, 0, cap.getY(), bottom, 0, cap.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawLine (cap.getX() + 2, cap.getY() + 0.5f, cap.getRight() - 2, cap.getY() + 0.5f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (cap.withTop (cap.getBottom() - 3.0f));
    }
}

void hwStyle (juce::Button& button, KeyStyle style)
{
    button.getProperties().set (styleId, static_cast<int> (style));
    button.repaint();
}

void hwPad (juce::Button& button, juce::Colour colour, bool lit)
{
    button.getProperties().set (styleId, static_cast<int> (KeyStyle::pad));
    button.getProperties().set (padColourId, static_cast<juce::int64> (colour.getARGB()));
    button.getProperties().set (padLitId, lit);
    button.repaint();
}

void hwScale (juce::Slider& slider, const juce::String& labels)
{
    slider.getProperties().set (scaleId, labels);
    slider.repaint();
}

Theme::Theme()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::DocumentWindow::textColourId, hw::colour::silk);
    setColour (juce::Label::textColourId, hw::colour::silk);
    setColour (juce::TextButton::textColourOffId, hw::colour::silk);
    setColour (juce::TextButton::textColourOnId, hw::colour::silk);
    setColour (juce::ToggleButton::textColourId, hw::colour::silk);
    setColour (juce::ComboBox::textColourId, hw::colour::amber);
    setColour (juce::ComboBox::arrowColourId, hw::colour::amber);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1d20));
    setColour (juce::PopupMenu::textColourId, hw::colour::silk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, hw::colour::amber.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff1c0a02));
    setColour (juce::PopupMenu::headerTextColourId, hw::colour::red);
    setColour (juce::TextEditor::textColourId, hw::colour::amber);
    setColour (juce::TextEditor::highlightColourId, hw::colour::amber.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, hw::colour::amber);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TableListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff6f7173));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1c1d20));
    setColour (juce::TooltipWindow::textColourId, hw::colour::silk);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::black);
    setColour (juce::AlertWindow::backgroundColourId, hw::colour::faceplate);
    setColour (juce::AlertWindow::textColourId, hw::colour::silk);
    setColour (juce::AlertWindow::outlineColourId, juce::Colours::black);
    setColour (juce::DialogWindow::backgroundColourId, hw::colour::faceplate);
}

juce::Typeface::Ptr Theme::getTypefaceForFont (const juce::Font& font)
{
    // Every default-font text in the app is Archivo Narrow.
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return hw::label (font.getHeight(), font.isBold()).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (font);
}

juce::Font Theme::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return hw::label (juce::jlimit (11.0f, 14.0f, buttonHeight * 0.4f));
}

juce::Font Theme::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 0.5f);
    const bool enabled = b.isEnabled();
    switch (styleOf (b))
    {
        case KeyStyle::cream:
            drawKey (g, r, juce::Colour (0xffd3c9ad), juce::Colour (0xff978b6f), down, over && enabled);
            // Grime in the corners of a much-pressed key.
            g.setColour (juce::Colour (0x22463712));
            g.drawRoundedRectangle (r.withTrimmedBottom (3.0f).reduced (2.0f).translated (0, down ? 2.0f : 0.0f), 2.0f, 2.0f);
            break;
        case KeyStyle::cue:
            drawKey (g, r, juce::Colour (0xffff8a3d), juce::Colour (0xffc4471a), down, over && enabled);
            if (enabled)
            {
                g.setColour (juce::Colour (0xffff7828).withAlpha (0.18f));
                g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 3.0f);
            }
            break;
        case KeyStyle::rec:
            drawKey (g, r, b.getToggleState() ? juce::Colour (0xffff4a3a) : juce::Colour (0xffb3261a),
                     b.getToggleState() ? juce::Colour (0xffa01c10) : juce::Colour (0xff5e1209), down, over && enabled);
            if (b.getToggleState())
            {
                g.setColour (juce::Colour (0xffff3020).withAlpha (0.25f));
                g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 4.0f);
            }
            break;
        case KeyStyle::pad:
        {
            const auto colour = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (b.getProperties()[padColourId])));
            const bool lit = b.getProperties()[padLitId];
            if (lit)
            {
                g.setColour (colour.withAlpha (0.22f));
                g.fillRoundedRectangle (r.expanded (3.0f), 6.0f);
                drawKey (g, r, colour.brighter (0.25f), colour.darker (0.9f), down, over && enabled);
                // Backlight: brighter in the middle of the cap.
                const auto cap = r.withTrimmedBottom (3.0f).reduced (4.0f).translated (0, down ? 2.0f : 0.0f);
                juce::ColourGradient glow (juce::Colours::white.withAlpha (0.28f), cap.getCentreX(), cap.getY() + cap.getHeight() * 0.35f,
                                           juce::Colours::transparentWhite, cap.getX(), cap.getBottom(), true);
                g.setGradientFill (glow);
                g.fillRoundedRectangle (cap, 2.0f);
            }
            else
            {
                drawKey (g, r, juce::Colour (0xff2e2f32), juce::Colour (0xff19191b), down, over && enabled);
            }
            break;
        }
        case KeyStyle::dark:
        case KeyStyle::play:
        default:
            drawKey (g, r, juce::Colour (0xff4b4c50), juce::Colour (0xff2a2b2e), down, over && enabled);
            break;
    }

    // A key that's switched on shows a small LED in its top-left corner.
    if (b.getToggleState() && styleOf (b) != KeyStyle::pad && styleOf (b) != KeyStyle::rec)
        hw::led (g, { r.getX() + 8.0f, r.getY() + 7.0f + (down ? 2.0f : 0.0f) }, 2.6f, hw::colour::amber, true);

    if (! enabled)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 2.5f);
    }
}

void Theme::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto area = b.getLocalBounds().toFloat().withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f).reduced (4.0f, 0);
    const auto text = b.getButtonText();
    const auto alpha = b.isEnabled() ? 1.0f : 0.45f;
    switch (styleOf (b))
    {
        case KeyStyle::cream:
            g.setColour (juce::Colour (0xff1e1b14).withAlpha (alpha));
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 2, 0.8f);
            break;
        case KeyStyle::cue:
            g.setColour (juce::Colour (0xff1c0a02).withAlpha (alpha));
            g.setFont (text.length() > 6 ? hw::label (14.0f) : hw::brand (13.0f));
            g.drawText (text, area, juce::Justification::centred);
            break;
        case KeyStyle::rec:
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.setFont (hw::brand (13.0f));
            g.drawText (text, area, juce::Justification::centred);
            break;
        case KeyStyle::play:
            hw::glowText (g, text, area, hw::brand (12.0f), hw::colour::playGreen.withAlpha (alpha), juce::Justification::centred);
            break;
        case KeyStyle::pad:
        {
            const bool lit = b.getProperties()[padLitId];
            g.setFont (hw::brand (juce::jlimit (12.0f, 20.0f, b.getHeight() * 0.32f)));
            g.setColour ((lit ? juce::Colours::white : juce::Colour (0xff6e6c66)).withAlpha (alpha));
            g.drawText (text, area, juce::Justification::centred);
            break;
        }
        case KeyStyle::dark:
        default:
            g.setColour (hw::colour::silk.withAlpha (alpha));
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 2, 0.8f);
            break;
    }
}

void Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    // A chrome bat-handle toggle switch: up is on.
    const auto h = static_cast<float> (b.getHeight());
    const auto sw = juce::Rectangle<float> (0, 0, 22.0f, juce::jmin (h, 34.0f)).withCentre ({ 13.0f, h / 2 });
    const auto c = sw.getCentre();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (22, 22).withCentre (c.translated (1, 2)));
    juce::ColourGradient nut (juce::Colour (0xffe1e2e4), c.x - 6, c.y - 6, juce::Colour (0xff6c6d70), c.x + 8, c.y + 8, true);
    g.setGradientFill (nut);
    g.fillEllipse (juce::Rectangle<float> (21, 21).withCentre (c));
    g.setColour (juce::Colour (0xff222222));
    g.drawEllipse (juce::Rectangle<float> (21, 21).withCentre (c), 1.0f);
    g.setColour (juce::Colour (0xff8d8e90));
    g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (c));
    const bool on = b.getToggleState();
    const auto tipY = on ? sw.getY() + 1.0f : sw.getBottom() - 1.0f;
    juce::Path lever;
    lever.startNewSubPath (c.x - 2.0f, c.y);
    lever.lineTo (c.x - 3.5f, tipY);
    lever.lineTo (c.x + 3.5f, tipY);
    lever.lineTo (c.x + 2.0f, c.y);
    lever.closeSubPath();
    juce::ColourGradient chrome (juce::Colour (0xfff4f4f2), c.x - 3, 0, juce::Colour (0xff8a8b8d), c.x + 4, 0, false);
    g.setGradientFill (chrome);
    g.fillPath (lever);
    g.setColour (juce::Colour (0xff555555));
    g.strokePath (lever, juce::PathStrokeType (0.6f));
    g.setColour (juce::Colour (0xffcfd0d2));
    g.fillEllipse (juce::Rectangle<float> (8.5f, 6.0f).withCentre ({ c.x, tipY }));

    auto text = b.getLocalBounds().toFloat().withTrimmedLeft (32.0f);
    const auto alpha = b.isEnabled() ? 1.0f : 0.45f;
    g.setFont (hw::label (12.0f));
    g.setColour (hw::colour::silk.withAlpha (alpha));
    g.drawText (b.getButtonText().toUpperCase(), text.withTrimmedBottom (h / 2 - 1), juce::Justification::bottomLeft);
    g.setColour (hw::colour::red.withAlpha (alpha));
    g.setFont (hw::label (11.0f));
    g.drawText (on ? "ON" : "OFF", text.withTrimmedTop (h / 2 + 1), juce::Justification::topLeft);
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h));
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const auto scaleR = size * 0.36f, labelR = size * 0.45f, knobR = size * 0.29f;

    // Printed scale: a dot and a number at each position.
    const auto labels = juce::StringArray::fromTokens (s.getProperties()[scaleId].toString(), "|", "");
    g.setFont (hw::label (juce::jmax (9.0f, size * 0.075f)));
    const auto n = juce::jmax (2, labels.size());
    for (int i = 0; i < n; ++i)
    {
        const auto a = start + (end - start) * static_cast<float> (i) / static_cast<float> (n - 1);
        const auto dot = c.getPointOnCircumference (scaleR, a);
        g.setColour (hw::colour::silk);
        g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (dot));
        if (i < labels.size())
            g.drawText (labels[i], juce::Rectangle<float> (26, 12).withCentre (c.getPointOnCircumference (labelR, a)), juce::Justification::centred);
    }

    // Knob: shadow, fluted skirt, cap with a rubbed shine, white pointer.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (knobR * 2, knobR * 2).withCentre (c.translated (0, 3)));
    juce::ColourGradient body (juce::Colour (0xff4a4a4c), c.x - knobR * 0.4f, c.y - knobR * 0.6f, juce::Colour (0xff050505), c.x + knobR, c.y + knobR, true);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (knobR * 2, knobR * 2).withCentre (c));
    g.setColour (juce::Colours::black);
    const int flutes = 36;
    for (int i = 0; i < flutes; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / flutes;
        g.drawLine ({ c.getPointOnCircumference (knobR * 0.86f, a), c.getPointOnCircumference (knobR, a) }, 1.6f);
    }
    const auto capR = knobR * 0.7f;
    juce::ColourGradient cap (juce::Colour (0xff3d3d40), c.x - capR * 0.5f, c.y - capR * 0.7f, juce::Colour (0xff0b0b0c), c.x + capR, c.y + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (juce::Rectangle<float> (capR * 2, capR * 2).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (juce::Rectangle<float> (capR * 2, capR * 2).withCentre (c), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillEllipse (juce::Rectangle<float> (capR * 0.9f, capR * 0.6f).withCentre (c.translated (-capR * 0.25f, -capR * 0.4f)));

    const auto angle = start + pos * (end - start);
    g.setColour (juce::Colour (0xfff4f2ec).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.drawLine ({ c.getPointOnCircumference (knobR * 0.25f, angle), c.getPointOnCircumference (knobR * 0.92f, angle) }, 3.0f);
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                              juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    // A fader: a black slot with printed ticks, and a chrome cap with a red line.
    const auto b = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h));
    const auto cy = b.getCentreY();
    g.setColour (hw::colour::silkDim);
    for (int i = 0; i <= 16; ++i)
    {
        const auto tx = b.getX() + b.getWidth() * static_cast<float> (i) / 16.0f;
        const auto len = (i % 4 == 0) ? 6.0f : 3.0f;
        g.drawLine (tx, cy - 10 - len, tx, cy - 10, i == 8 ? 2.0f : 1.0f);
        g.drawLine (tx, cy + 10, tx, cy + 10 + len, i == 8 ? 2.0f : 1.0f);
    }
    const auto slot = juce::Rectangle<float> (b.getX(), cy - 3, b.getWidth(), 6);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (slot, 3);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (slot.getX() + 3, slot.getBottom() + 0.5f, slot.getRight() - 3, slot.getBottom() + 0.5f, 1.0f);

    const auto cap = juce::Rectangle<float> (16, 30).withCentre ({ pos, cy });
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (1, 3), 2);
    juce::ColourGradient chrome (juce::Colour (0xfff0efea), cap.getX(), cap.getY(), juce::Colour (0xff8f8e89), cap.getX(), cap.getBottom(), false);
    chrome.addColour (0.5, juce::Colour (0xffbab9b3));
    g.setGradientFill (chrome);
    g.fillRoundedRectangle (cap, 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    for (int i = -2; i <= 2; ++i)
        if (i != 0)
            g.drawLine (cap.getX() + 3, cap.getCentreY() + i * 4.0f, cap.getRight() - 3, cap.getCentreY() + i * 4.0f, 0.8f);
    g.setColour (hw::colour::red);
    g.drawLine (cap.getX() + 2, cap.getCentreY(), cap.getRight() - 2, cap.getCentreY(), 2.0f);
}

void Theme::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (4.0f);
    hw::recess (g, r, 3.0f);
    juce::Path arrow;
    const auto ax = r.getRight() - 12.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.3f));
    g.fillPath (arrow);
    hw::glass (g, r, 3.0f);
}

juce::Font Theme::getComboBoxFont (juce::ComboBox&)
{
    return hw::vfd (22.0f);
}

void Theme::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 2, box.getWidth() - 34, box.getHeight() - 4);
    label.setFont (getComboBoxFont (box));
}

juce::Font Theme::getPopupMenuFont()
{
    return hw::label (15.0f, false);
}

void Theme::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff1c1d20));
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRect (0, 0, w, h);
}

void Theme::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    hw::recess (g, juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (4.0f), 3.0f);
}

void Theme::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    if (e.hasKeyboardFocus (true))
    {
        g.setColour (hw::colour::amber.withAlpha (0.6f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (3.0f), 3.0f, 1.0f);
    }
}

void Theme::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.setColour (juce::Colour (0xff120d07));
    g.fillRect (header.getLocalBounds());
    g.setColour (hw::colour::amber.withAlpha (0.35f));
    g.fillRect (header.getLocalBounds().removeFromBottom (1));
}

void Theme::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name, int, int w, int h,
                                   bool over, bool, int flags)
{
    auto area = juce::Rectangle<int> (0, 0, w, h).reduced (8, 0);
    g.setFont (hw::vfd (19.0f));
    g.setColour ((over ? hw::colour::amber : hw::colour::amberMid));
    juce::String text = name.toUpperCase();
    if ((flags & juce::TableHeaderComponent::sortedForwards) != 0)
        text << " ^";
    else if ((flags & juce::TableHeaderComponent::sortedBackwards) != 0)
        text << " v";
    g.drawText (text, area, juce::Justification::centredLeft, true);
}

void Theme::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize,
                           bool over, bool)
{
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (x, y, w, h);
    if (thumbSize <= 0)
        return;
    const auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (hw::colour::amber.withAlpha (over ? 0.6f : 0.35f));
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}

void Theme::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (juce::Colour (0xff1c1d20));
    g.setColour (juce::Colours::black);
    g.drawRect (0, 0, w, h);
    g.setColour (hw::colour::silk);
    g.setFont (hw::label (14.0f, false));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 3), juce::Justification::centredLeft, 4);
}

void Theme::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    hw::faceplate (g, alert.getLocalBounds().toFloat());
    layout.draw (g, textArea.toFloat());
}

} // namespace dss1::ui
