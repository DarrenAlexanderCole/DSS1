#include "Theme.h"
#include "Hardware.h"

namespace dss1::ui
{

using hw::part;
using hw::nineSlice;

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

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    if (drawRenderedKey (g, b, over, down))
        return;
    drawCodeKey (g, b, base, over, down);
}

bool Theme::drawRenderedKey (juce::Graphics& g, juce::Button& b, bool over, bool down)
{
    // The rendered keys (tools/render): a key in its cut-out, nine-sliced to the button's size. The render's
    // cut-out spans about 289 x 112 of its 320 x 128 pixels.
    const auto style = styleOf (b);
    const bool enabled = b.isEnabled();
    auto r = b.getLocalBounds().toFloat();
    if (style == KeyStyle::pad)
    {
        // Treated rawhide in a dark bezel (the render's bezel spans 0.9 of the image), filling the pad's
        // space, with a light-pipe ring round it that glows in the pad's colour.
        const auto img = part (down ? "pad_hide_down" : "pad_hide_up");
        if (! img.isValid())
            return false;
        const auto colour = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (b.getProperties()[padColourId])));
        const bool lit = b.getProperties()[padLitId];
        const auto lightLevel = hw::light();
        const auto size = juce::jmin (r.getWidth(), r.getHeight());
        const auto sq = r.withSizeKeepingCentre (size, size);
        g.setOpacity (1.0f);
        g.drawImage (img, sq.withSizeKeepingCentre (size / 0.9f, size / 0.9f), juce::RectanglePlacement::stretchToFit);
        // The ring sits between 0.85 and 0.80 of the render: about 3.5% in from the bezel's edge.
        const auto ring = sq.reduced (size * 0.035f);
        const auto ringW = juce::jmax (2.0f, size * 0.03f);
        if (lit)
        {
            const auto c = colour.withMultipliedBrightness (0.75f + 0.25f * lightLevel);
            // Spill onto the hide and the panel, then the lit pipe itself.
            g.setColour (c.withAlpha (0.18f * lightLevel));
            g.drawRoundedRectangle (ring, size * 0.07f, ringW * 4.0f);
            g.setColour (c.withAlpha (0.35f * lightLevel));
            g.drawRoundedRectangle (ring, size * 0.07f, ringW * 2.0f);
            g.setColour (c.withAlpha (0.95f));
            g.drawRoundedRectangle (ring, size * 0.07f, ringW);
            g.setColour (juce::Colours::white.withAlpha (0.35f * lightLevel));
            g.drawRoundedRectangle (ring, size * 0.07f, ringW * 0.35f);
        }
        if (! enabled)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (sq, size * 0.06f);
        }
        return true;
    }

    const char* name = "key_black";  // black phenolic, the default key
    switch (style)
    {
        case KeyStyle::cream: name = "key_cream"; break;
        case KeyStyle::cue: name = "key_cue"; break;
        case KeyStyle::rec: name = "key_rec"; break;
        case KeyStyle::dark:
        case KeyStyle::play:
        case KeyStyle::pad:
        default: break;
    }
    const bool lowered = down || (style == KeyStyle::rec && b.getToggleState());
    const auto img = part (juce::String (name) + (lowered ? "_down" : "_up"));
    if (! img.isValid())
        return false;
    // Scale so the cut-out's height (112 px of the render) fills the button; its margins hang outside.
    const auto scale = r.getHeight() / 112.0f;
    const auto dest = r.expanded (15.5f * scale, 8.0f * scale);
    nineSlice (g, img, dest, 40, 34, scale);
    {
        // Each key worn in its own way.
        const auto cap = r.reduced (6.0f * scale + 1.0f, 6.0f * scale + 1.0f).translated (0, lowered ? 1.0f : 0.0f);
        const auto* top = b.getTopLevelComponent();
        const auto seed = (top != nullptr ? top->getLocalPoint (&b, b.getLocalBounds().getCentre()) : b.getPosition()).toFloat()
                          + juce::Point<float> (static_cast<float> (b.getButtonText().hashCode() % 97), 0.0f);
        hw::wear (g, cap, seed, style == KeyStyle::dark);
    }
    if (over && enabled)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r.reduced (3.0f * scale + 1.0f), 3.0f);
    }
    // Lit: an orange key glows through; a switched-on key shows its lamp.
    if (style == KeyStyle::cue && enabled)
    {
        g.setColour (juce::Colour (0xffff7828).withAlpha (0.14f * hw::light()));
        g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 3.0f);
    }
    if (style == KeyStyle::rec && b.getToggleState())
    {
        g.setColour (juce::Colour (0xffff3020).withAlpha (0.3f * hw::light()));
        g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 4.0f);
    }
    if (b.getToggleState() && style != KeyStyle::rec)
        hw::led (g, { r.getX() + 9.0f, r.getY() + 8.0f + (down ? 1.5f : 0.0f) }, 2.6f, hw::colour::amber, true);
    if (! enabled)
    {
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);
    }
    return true;
}

void Theme::drawCodeKey (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
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
    // On the rendered keys the cap sits centred and travels a little when pressed.
    auto area = b.getLocalBounds().toFloat().translated (0, down ? 1.5f : 0.0f).reduced (6.0f, 0);
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
            // Stamped into the hide.
            g.setFont (hw::brand (juce::jlimit (12.0f, 20.0f, b.getHeight() * 0.3f)));
            g.setColour (juce::Colour (0xfffff2dc).withAlpha (0.35f * alpha));
            g.drawText (text, area.translated (0, 1.0f), juce::Justification::centred);
            g.setColour (juce::Colour (0xff2c1a0c).withAlpha (0.78f * alpha));
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
    // A chrome bat-handle toggle switch: up is on. Wide: name and state to the right. Tall and narrow (a row
    // of switches): name above, state below.
    const auto h = static_cast<float> (b.getHeight());
    const bool stacked = b.getHeight() > 50 && b.getWidth() < 110;
    const auto onText = b.getProperties().getWithDefault ("onText", "ON").toString();
    const auto offText = b.getProperties().getWithDefault ("offText", "OFF").toString();
    const auto centreX = stacked ? static_cast<float> (b.getWidth()) / 2 : 13.0f;
    const auto sw = juce::Rectangle<float> (0, 0, 22.0f, juce::jmin (stacked ? h - 30.0f : h, 34.0f)).withCentre ({ centreX, h / 2 });
    const auto c = sw.getCentre();
    const bool on = b.getToggleState();
    const auto alpha = b.isEnabled() ? 1.0f : 0.45f;
    if (const auto img = hw::part (on ? "toggle_up" : "toggle_down"); img.isValid())
    {
        // The rendered chrome toggle (its nut is about 0.58 of the render's width).
        const auto size = juce::jmin (sw.getHeight() * 1.45f, 50.0f);
        g.setOpacity (alpha);
        g.drawImage (img, juce::Rectangle<float> (size, size).withCentre (c), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }
    else
    {
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (22, 22).withCentre (c.translated (1, 2)));
    juce::ColourGradient nut (juce::Colour (0xffe1e2e4), c.x - 6, c.y - 6, juce::Colour (0xff6c6d70), c.x + 8, c.y + 8, true);
    g.setGradientFill (nut);
    g.fillEllipse (juce::Rectangle<float> (21, 21).withCentre (c));
    g.setColour (juce::Colour (0xff222222));
    g.drawEllipse (juce::Rectangle<float> (21, 21).withCentre (c), 1.0f);
    g.setColour (juce::Colour (0xff8d8e90));
    g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (c));
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
    }

    if (stacked)
    {
        const auto all = b.getLocalBounds().toFloat();
        g.setFont (hw::label (11.0f));
        g.setColour (hw::colour::ink.withAlpha (alpha));
        g.drawText (b.getButtonText().toUpperCase(), all.withHeight (13.0f), juce::Justification::centred);
        g.setColour ((on ? juce::Colour (0xff9a4a00) : hw::colour::inkRed).withAlpha (alpha));
        g.setFont (hw::label (10.5f));
        g.drawText (on ? onText : offText, all.withTop (all.getBottom() - 13.0f), juce::Justification::centred);
        return;
    }
    auto text = b.getLocalBounds().toFloat().withTrimmedLeft (32.0f);
    g.setFont (hw::label (12.0f));
    g.setColour (hw::colour::ink.withAlpha (alpha));
    g.drawText (b.getButtonText().toUpperCase(), text.withTrimmedBottom (h / 2 - 1), juce::Justification::bottomLeft);
    g.setColour (hw::colour::inkRed.withAlpha (alpha));
    g.setFont (hw::label (11.0f));
    g.drawText (on ? onText : offText, text.withTrimmedTop (h / 2 + 1), juce::Justification::topLeft);
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h));
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const auto scaleR = size * 0.375f, labelR = size * 0.455f, knobR = size * 0.29f;

    // Printed scale: a dot and a number at each position.
    const auto labels = juce::StringArray::fromTokens (s.getProperties()[scaleId].toString(), "|", "");
    g.setFont (hw::label (juce::jmax (9.0f, size * 0.075f)));
    const auto n = juce::jmax (2, labels.size());
    for (int i = 0; i < n; ++i)
    {
        const auto a = start + (end - start) * static_cast<float> (i) / static_cast<float> (n - 1);
        const auto dot = c.getPointOnCircumference (scaleR, a);
        g.setColour (hw::colour::ink);
        g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (dot));
        if (i < labels.size())
            g.drawText (labels[i], juce::Rectangle<float> (26, 12).withCentre (c.getPointOnCircumference (labelR, a)), juce::Justification::centred);
    }

    const auto angle = start + pos * (end - start);
    const bool brass = s.getProperties()["hwKnob"].toString() == "brass";
    if (const auto img = hw::part (brass ? "knob_brass" : "knob_alu"); img.isValid())
    {
        // The rendered knob (its skirt spans 0.71 of the render): lit from the upper left, so it looks the same
        // at any angle; only the engraved pointer turns.
        const auto skirtR = size * 0.31f;
        const auto drawn = skirtR * 2.0f / 0.71f;
        g.setOpacity (s.isEnabled() ? 1.0f : 0.6f);
        g.drawImage (img, juce::Rectangle<float> (drawn, drawn).withCentre (c), juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
        const auto capR = drawn * 0.5f * (0.80f / 1.55f);
        const auto p1 = c.getPointOnCircumference (capR * 0.30f, angle), p2 = c.getPointOnCircumference (capR * 0.92f, angle);
        // Engraved: a dark groove with a bright lower lip.
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine ({ p1.translated (0.6f, 0.9f), p2.translated (0.6f, 0.9f) }, 1.6f);
        g.setColour (juce::Colour (0xff141414).withAlpha (s.isEnabled() ? 0.95f : 0.5f));
        g.drawLine ({ p1, p2 }, 2.2f);
        return;
    }

    // (Fallback, drawn) Knob: shadow, fluted skirt, cap with a rubbed shine, white pointer.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (knobR * 2, knobR * 2).withCentre (c.translated (0, 3)));
    juce::ColourGradient body (juce::Colour (0xff4a4a4c), c.x - knobR * 0.4f, c.y - knobR * 0.6f, juce::Colour (0xff050505), c.x + knobR, c.y + knobR, true);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (knobR * 2, knobR * 2).withCentre (c));
    g.setColour (juce::Colour (0xfff4f2ec).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.drawLine ({ c.getPointOnCircumference (knobR * 0.2f, angle), c.getPointOnCircumference (knobR * 0.9f, angle) }, 2.4f);
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
    g.setColour (hw::colour::ink.withAlpha (0.8f));
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
    // Number columns are right-aligned, so their headings sit over the numbers.
    const bool numeric = name == "Time" || name == "BPM" || name == "Loudness";
    g.drawText (text, area, numeric ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
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
