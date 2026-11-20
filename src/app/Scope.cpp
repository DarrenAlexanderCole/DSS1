#include "Scope.h"
#include "Hardware.h"

namespace dss1::ui::scope
{

namespace
{
    const juce::Colour hot { 0xfffff0d8 };  // the white-hot centre of the warm light
}

juce::Path smooth (const std::vector<juce::Point<float>>& p)
{
    juce::Path path;
    if (p.empty())
        return path;
    path.startNewSubPath (p.front());
    if (p.size() < 3)
    {
        for (std::size_t i = 1; i < p.size(); ++i)
            path.lineTo (p[i]);
        return path;
    }
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
    {
        const auto& p0 = p[i == 0 ? 0 : i - 1];
        const auto& p1 = p[i];
        const auto& p2 = p[i + 1];
        const auto& p3 = p[juce::jmin (p.size() - 1, i + 2)];
        const juce::Point<float> c1 (p1.x + (p2.x - p0.x) / 6.0f, p1.y + (p2.y - p0.y) / 6.0f);
        const juce::Point<float> c2 (p2.x - (p3.x - p1.x) / 6.0f, p2.y - (p3.y - p1.y) / 6.0f);
        path.cubicTo (c1, c2, p2);
    }
    return path;
}

void trace (juce::Graphics& g, const juce::Path& path, juce::Colour c, float width, float glow)
{
    const auto lit = hw::light();
    g.setColour (c.withAlpha (0.07f * glow * lit));
    g.strokePath (path, juce::PathStrokeType (width * 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.20f * glow * lit));
    g.strokePath (path, juce::PathStrokeType (width * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.interpolatedWith (hot, 0.35f).withAlpha (0.95f * juce::jlimit (0.2f, 1.0f, lit)));
    g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void fillUnder (juce::Graphics& g, const juce::Path& curve, float topY, float floorY, juce::Colour c, float alpha)
{
    if (curve.isEmpty())
        return;
    auto filled = curve;
    const auto b = curve.getBounds();
    filled.lineTo (b.getRight(), floorY);
    filled.lineTo (b.getX(), floorY);
    filled.closeSubPath();
    const auto lit = hw::light();
    juce::ColourGradient grad (c.withAlpha (alpha * lit), 0, topY, c.withAlpha (0.04f * lit), 0, floorY, false);
    grad.addColour (0.55, c.withAlpha (alpha * 0.45f * lit));
    g.setGradientFill (grad);
    g.fillPath (filled);
}

void graticule (juce::Graphics& g, juce::Rectangle<float> area, int across, int down, juce::Colour c, bool centre)
{
    const auto lit = hw::light();
    g.setColour (c.withAlpha (0.055f * lit));
    for (int i = 1; i < across; ++i)
        g.fillRect (area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (across), area.getY(), 1.0f, area.getHeight());
    for (int i = 1; i < down; ++i)
        g.fillRect (area.getX(), area.getY() + area.getHeight() * static_cast<float> (i) / static_cast<float> (down), area.getWidth(), 1.0f);
    if (centre)
    {
        g.setColour (c.withAlpha (0.13f * lit));
        g.fillRect (area.getX(), area.getCentreY() - 0.5f, area.getWidth(), 1.0f);
    }
}

void wave (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<Span>& input, float step, juce::Colour c,
           float playedX, float scale)
{
    // Mirrored about the centre line, like a DJ waveform: each column's loudest swing either way, lightly
    // smoothed so the silhouette flows (transients keep most of their height).
    std::vector<Span> cols (input.size());
    {
        std::vector<float> amp (input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            amp[i] = juce::jmin (1.0f, juce::jmax (input[i].hi, -input[i].lo, 0.0f));
        for (std::size_t i = 0; i < amp.size(); ++i)
        {
            const auto prev = amp[i > 0 ? i - 1 : i], next = amp[i + 1 < amp.size() ? i + 1 : i];
            const auto a = juce::jmax ((prev + 2.0f * amp[i] + next) * 0.25f, amp[i] * 0.85f);
            cols[i] = { -a, a };
        }
    }
    const auto n = cols.size();
    if (n == 0)
        return;
    const auto lit = hw::light();
    const auto mid = area.getCentreY();
    const auto half = area.getHeight() * scale;
    const auto xAt = [&] (std::size_t i) { return area.getX() + (static_cast<float> (i) + 0.5f) * step; };

    // The body (a smoothed RMS-like core): a moving average of each column's height.
    std::vector<float> body (n);
    {
        const int radius = juce::jlimit (1, 6, static_cast<int> (4.0f / juce::jmax (0.25f, step)));
        float sum = 0;
        int count = 0;
        const auto h = [&] (std::size_t i) { return (cols[i].hi - cols[i].lo) * 0.5f; };
        for (std::size_t i = 0; i < n + static_cast<std::size_t> (radius); ++i)
        {
            if (i < n)
            {
                sum += h (i);
                ++count;
            }
            if (i >= static_cast<std::size_t> (2 * radius + 1) && i - static_cast<std::size_t> (2 * radius + 1) < n)
            {
                sum -= h (i - static_cast<std::size_t> (2 * radius + 1));
                --count;
            }
            if (i >= static_cast<std::size_t> (radius) && i - static_cast<std::size_t> (radius) < n)
                body[i - static_cast<std::size_t> (radius)] = sum / static_cast<float> (juce::jmax (1, count)) * 0.62f;
        }
    }

    // Outline of the sound: along the top, back along the bottom.
    juce::Path outer, top, bottom, core;
    for (std::size_t i = 0; i < n; ++i)
    {
        const juce::Point<float> p (xAt (i), mid - juce::jlimit (-1.0f, 1.0f, cols[i].hi) * half);
        if (i == 0)
        {
            outer.startNewSubPath (p);
            top.startNewSubPath (p);
        }
        else
        {
            outer.lineTo (p);
            top.lineTo (p);
        }
    }
    for (std::size_t i = n; i-- > 0;)
    {
        const juce::Point<float> p (xAt (i), mid - juce::jlimit (-1.0f, 1.0f, cols[i].lo) * half);
        outer.lineTo (p);
        if (i == n - 1)
            bottom.startNewSubPath (p);
        else
            bottom.lineTo (p);
    }
    outer.closeSubPath();
    for (std::size_t i = 0; i < n; ++i)
    {
        const juce::Point<float> p (xAt (i), mid - juce::jmin (1.0f, body[i]) * half);
        if (i == 0)
            core.startNewSubPath (p);
        else
            core.lineTo (p);
    }
    for (std::size_t i = n; i-- > 0;)
        core.lineTo (xAt (i), mid + juce::jmin (1.0f, body[i]) * half);
    core.closeSubPath();

    // Bloom around the sound, then the fill: brightest at its edges, deep in the middle, then the hot core.
    g.setColour (c.withAlpha (0.06f * lit));
    g.strokePath (outer, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
    juce::ColourGradient fill (c.withAlpha (0.95f * lit), 0, mid - half, c.withAlpha (0.95f * lit), 0, mid + half, false);
    fill.addColour (0.5, c.darker (0.4f).withAlpha (0.7f * lit));
    g.setGradientFill (fill);
    g.fillPath (outer);
    juce::ColourGradient body2 (c.interpolatedWith (hot, 0.4f).withAlpha (0.7f * lit), 0, mid - half * 0.6f,
                                c.interpolatedWith (hot, 0.4f).withAlpha (0.7f * lit), 0, mid + half * 0.6f, false);
    body2.addColour (0.5, c.interpolatedWith (hot, 0.15f).withAlpha (0.4f * lit));
    g.setGradientFill (body2);
    g.fillPath (core);
    g.setColour (c.interpolatedWith (hot, 0.2f).withAlpha (0.85f * lit));
    g.strokePath (top, juce::PathStrokeType (1.0f));
    g.strokePath (bottom, juce::PathStrokeType (1.0f));

    // What's already been played sits back in the dark.
    if (playedX > area.getX())
    {
        g.setColour (hw::colour::glassBlack.withAlpha (0.55f));
        g.fillRect (area.withRight (juce::jmin (area.getRight(), playedX)).expanded (0, 4));
    }
}

juce::Colour monitorInk() { return juce::Colour (0xff2a1606); }

juce::Rectangle<float> monitorBegin (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto lit = hw::light();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (area, 4.0f);
    const auto face = area.reduced (4.0f);
    const auto b = 0.55f + 0.45f * lit;
    juce::ColourGradient back (juce::Colour (0xffffd28a).withMultipliedBrightness (b), face.getCentreX(), face.getBottom(),
                               juce::Colour (0xff9a6a2a).withMultipliedBrightness (b), face.getX(), face.getY(), true);
    back.addColour (0.5, juce::Colour (0xfff0b968).withMultipliedBrightness (b));
    g.setGradientFill (back);
    g.fillRoundedRectangle (face, 2.0f);
    return face.reduced (6.0f, 5.0f);
}

void monitorEnd (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto face = area.reduced (4.0f);
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.22f), face.getX(), face.getY(), juce::Colours::transparentWhite, face.getX(),
                               face.getY() + face.getHeight() * 0.45f, false);
    g.setGradientFill (refl);
    g.fillRoundedRectangle (face.withHeight (face.getHeight() * 0.45f), 2.0f);
    juce::ColourGradient lip (juce::Colours::black.withAlpha (0.5f), face.getX(), face.getY(), juce::Colours::transparentBlack, face.getX(), face.getY() + 6.0f, false);
    g.setGradientFill (lip);
    g.fillRoundedRectangle (face, 2.0f);
}

void monitorGrid (juce::Graphics& g, juce::Rectangle<float> plot, int across, int down)
{
    g.setColour (monitorInk().withAlpha (0.09f));
    for (int i = 1; i < across; ++i)
        g.fillRect (plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (across), plot.getY(), 1.0f, plot.getHeight());
    for (int i = 1; i < down; ++i)
        g.fillRect (plot.getX(), plot.getY() + plot.getHeight() * static_cast<float> (i) / static_cast<float> (down), plot.getWidth(), 1.0f);
}

void inked (juce::Graphics& g, const juce::Path& p, float width, float alpha)
{
    const auto ink = monitorInk();
    g.setColour (ink.withAlpha (0.12f * alpha));
    g.strokePath (p, juce::PathStrokeType (width * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ink.withAlpha (alpha));
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void dashed (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float alpha)
{
    juce::Path p, d;
    p.startNewSubPath (a);
    p.lineTo (b);
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (d, p, dashes, 2);
    g.setColour (monitorInk().withAlpha (alpha));
    g.fillPath (d);
}

void monitorCaption (juce::Graphics& g, juce::Rectangle<float> plot, const juce::String& text, juce::Justification j)
{
    g.setColour (monitorInk().withAlpha (0.55f));
    g.setFont (hw::label (9.5f));
    g.drawText (text, plot.reduced (2.0f, 1.0f), j, false);
}

} // namespace dss1::ui::scope
