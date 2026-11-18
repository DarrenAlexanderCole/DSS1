#include "Hardware.h"

#include <BinaryData.h>

#include <map>

namespace dss1::ui::hw
{

namespace
{
    struct Assets
    {
        juce::Typeface::Ptr labelBold, labelMedium, brandFace, vfdFace;
        juce::Image panelWear, grime, edgeWear, aluminium, inkWear;

        Assets()
        {
            using namespace dss1_assets;
            labelBold = juce::Typeface::createSystemTypefaceFor (ArchivoNarrowBold_ttf, ArchivoNarrowBold_ttfSize);
            labelMedium = juce::Typeface::createSystemTypefaceFor (ArchivoNarrowMedium_ttf, ArchivoNarrowMedium_ttfSize);
            brandFace = juce::Typeface::createSystemTypefaceFor (MichromaRegular_ttf, MichromaRegular_ttfSize);
            vfdFace = juce::Typeface::createSystemTypefaceFor (VT323Regular_ttf, VT323Regular_ttfSize);
            panelWear = juce::ImageCache::getFromMemory (panelwear_png, panelwear_pngSize);
            grime = juce::ImageCache::getFromMemory (grime_png, grime_pngSize);
            edgeWear = juce::ImageCache::getFromMemory (edgewear_png, edgewear_pngSize);
            aluminium = juce::ImageCache::getFromMemory (aluminium_png, aluminium_pngSize);
            inkWear = juce::ImageCache::getFromMemory (inkwear_png, inkwear_pngSize);
        }

        static Assets& get()
        {
            static Assets assets;
            return assets;
        }
    };

    juce::Font fontFrom (const juce::Typeface::Ptr& face, float size)
    {
        if (face == nullptr)
            return juce::Font (juce::FontOptions (size));
        return juce::Font (juce::FontOptions (face).withHeight (size));
    }

    /// Cheap, repeatable "randomness" from a position, so screws and wear don't change between repaints.
    float hash (float x, float y)
    {
        const auto v = std::sin (x * 12.9898f + y * 78.233f) * 43758.5453f;
        return v - std::floor (v);
    }
}

juce::Font label (float size, bool bold)
{
    auto& a = Assets::get();
    return fontFrom (bold ? a.labelBold : a.labelMedium, size);
}

juce::Font brand (float size) { return fontFrom (Assets::get().brandFace, size); }
juce::Font vfd (float size) { return fontFrom (Assets::get().vfdFace, size); }

void screw (juce::Graphics& g, juce::Point<float> c, float r)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - r + 0.8f, c.y - r + 1.4f, 2 * r, 2 * r);
    juce::ColourGradient head (juce::Colour (0xffd9d9d6), c.x - r * 0.4f, c.y - r * 0.5f, juce::Colour (0xff4d4e50), c.x + r, c.y + r, true);
    g.setGradientFill (head);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::black);
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
    // A Phillips cross, each screw turned a little differently.
    const auto angle = hash (c.x, c.y) * juce::MathConstants<float>::pi;
    g.setColour (juce::Colour (0xff262626));
    for (const auto a : { angle, angle + juce::MathConstants<float>::halfPi })
    {
        const auto dx = std::cos (a) * r * 0.62f, dy = std::sin (a) * r * 0.62f;
        g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, juce::jmax (1.2f, r * 0.2f));
    }
}

void faceplate (juce::Graphics& g, juce::Rectangle<float> area, bool screws)
{
    auto& a = Assets::get();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff25262a), area.getX(), area.getY(),
                                             juce::Colour (0xff131416), area.getX(), area.getBottom(), false));
    g.fillRect (area);

    // Grime, then grain, scratches and scuffs, tiled from fixed positions so they don't crawl when scrolling.
    if (a.grime.isValid())
    {
        g.setTiledImageFill (a.grime, 0, 0, 1.0f);
        g.fillRect (area);
    }
    if (a.panelWear.isValid())
    {
        g.setTiledImageFill (a.panelWear, 0, 0, 1.0f);
        g.fillRect (area);
    }
    if (a.edgeWear.isValid())
    {
        g.setOpacity (0.8f);
        g.drawImage (a.edgeWear, area, juce::RectanglePlacement::stretchToFit, false);
    }

    // Bevel: light along the top edge, shadow along the bottom.
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (area.withHeight (1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (area.withTop (area.getBottom() - 2.0f));

    if (screws && area.getWidth() > 60 && area.getHeight() > 50)
    {
        const auto r = 7.0f, m = 15.0f;
        screw (g, { area.getX() + m, area.getY() + m }, r);
        screw (g, { area.getRight() - m, area.getY() + m }, r);
        screw (g, { area.getX() + m, area.getBottom() - m }, r);
        screw (g, { area.getRight() - m, area.getBottom() - m }, r);
    }
}

void rackEar (juce::Graphics& g, juce::Rectangle<float> area, int slots)
{
    auto& a = Assets::get();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());
    if (a.aluminium.isValid())
    {
        g.setTiledImageFill (a.aluminium, 0, 0, 1.0f);
        g.fillRect (area);
    }
    else
    {
        g.setColour (juce::Colour (0xff9a9c9e));
        g.fillRect (area);
    }
    // Curvature: darker at the sides.
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.35f), area.getX(), 0, juce::Colours::transparentBlack, area.getCentreX(), 0, false);
    shade.addColour (1.0, juce::Colours::black.withAlpha (0.3f));
    shade.point2 = { area.getRight(), 0 };
    shade.addColour (0.45, juce::Colours::white.withAlpha (0.08f));
    g.setGradientFill (shade);
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (area.withHeight (1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (area.withTop (area.getBottom() - 2.0f));

    // Mounting slots.
    const auto slotW = juce::jmin (28.0f, area.getWidth() - 14.0f), slotH = 14.0f;
    for (int i = 0; i < slots; ++i)
    {
        const auto cy = area.getY() + area.getHeight() * (static_cast<float> (i) + 0.5f) / static_cast<float> (slots);
        const juce::Rectangle<float> slot (area.getCentreX() - slotW / 2, cy - slotH / 2, slotW, slotH);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillRoundedRectangle (slot.translated (0, 1), slotH / 2);
        g.setColour (juce::Colour (0xff141415));
        g.fillRoundedRectangle (slot, slotH / 2);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (slot, slotH / 2, 1.0f);
    }
}

void recess (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    // The bezel: a dark ring, then the window with an inner shadow.
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (area.expanded (4), corner + 3);
    g.setColour (juce::Colour (0xff2a2a2c));
    g.fillRoundedRectangle (area.expanded (3), corner + 2);
    g.setColour (colour::glassBlack);
    g.fillRoundedRectangle (area, corner);
    juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.85f), 0, area.getY(), juce::Colours::transparentBlack, 0, area.getY() + 14, false);
    g.setGradientFill (shadow);
    g.fillRoundedRectangle (area, corner);
}

void glass (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.09f), area.getX(), area.getY(), juce::Colours::transparentWhite,
                                area.getX() + area.getWidth() * 0.25f, area.getY() + area.getHeight() * 0.6f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (area, corner);
}

void silk (juce::Graphics& g, const juce::String& text, const juce::String& sub, juce::Rectangle<float> area,
           juce::Justification justification, float size)
{
    // Rendered once into an image with worn ink (patchy alpha), then cached: labels don't change.
    struct Key
    {
        juce::String text, sub;
        int w, h, size, just;
        bool operator< (const Key& o) const
        {
            return std::tie (text, sub, w, h, size, just) < std::tie (o.text, o.sub, o.w, o.h, o.size, o.just);
        }
    };
    static std::map<Key, juce::Image> cache;

    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const Key key { text, sub, juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale),
                    juce::roundToInt (size * 10), justification.getFlags() };
    if (key.w <= 0 || key.h <= 0)
        return;

    auto found = cache.find (key);
    if (found == cache.end())
    {
        if (cache.size() > 400)
            cache.clear();
        juce::Image img (juce::Image::ARGB, key.w, key.h, true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (scale));
            const auto bounds = juce::Rectangle<float> (0, 0, area.getWidth(), area.getHeight());
            juce::AttributedString s;
            s.setJustification (justification);
            s.append (text, label (size), colour::silk);
            if (sub.isNotEmpty())
                s.append ("  " + juce::String::fromUTF8 ("\xe2\x80\x94") + " " + sub, label (size), colour::red);
            s.draw (ig, bounds);
        }
        // Worn ink: multiply alpha by the wear mask.
        auto& mask = Assets::get().inkWear;
        if (mask.isValid())
        {
            const juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
            const juce::Image::BitmapData src (mask, juce::Image::BitmapData::readOnly);
            const auto ox = static_cast<int> (hash (static_cast<float> (text.hashCode() & 1023), 1.0f) * 400.0f);
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                {
                    auto p = dst.getPixelColour (x, y);
                    if (p.getAlpha() == 0)
                        continue;
                    const auto m = src.getPixelColour ((x * 2 / static_cast<int> (scale) + ox) % mask.getWidth(),
                                                       (y * 2 / static_cast<int> (scale)) % mask.getHeight()).getFloatAlpha();
                    dst.setPixelColour (x, y, p.withMultipliedAlpha (m));
                }
        }
        found = cache.emplace (key, img).first;
    }
    g.setOpacity (1.0f);  // drawImage uses the current colour's alpha
    g.drawImage (found->second, area, juce::RectanglePlacement::stretchToFit);
}

void glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
               juce::Colour c, juce::Justification justification)
{
    g.setFont (font);
    // A soft halo, then the bright text.
    g.setColour (c.withAlpha (0.09f));
    for (const auto& d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (text, area.translated (d.x, d.y), justification, true);
    g.setColour (c.withAlpha (0.05f));
    for (const auto& d : { juce::Point<float> (-2.0f, -1.0f), { 2.0f, 1.0f }, { -2.0f, 1.0f }, { 2.0f, -1.0f } })
        g.drawText (text, area.translated (d.x, d.y), justification, true);
    g.setColour (c);
    g.drawText (text, area, justification, true);
}

void led (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool on)
{
    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (c.x - r * 2.2f, c.y - r * 2.2f, r * 4.4f, r * 4.4f);
        juce::ColourGradient lens (colour.brighter (0.8f), c.x - r * 0.3f, c.y - r * 0.4f, colour.darker (0.4f), c.x + r, c.y + r, true);
        g.setGradientFill (lens);
    }
    else
    {
        g.setColour (colour.darker (2.2f).withSaturation (0.5f));
    }
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.8f);
}

void vuMeter (juce::Graphics& g, juce::Rectangle<float> area, float level, const juce::String& caption)
{
    // Black bezel.
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    const auto face = area.reduced (8.0f);

    // Aged cream face: lighter at the bottom centre, tea-stained at the corners.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (face.toNearestInt());
    juce::ColourGradient cream (juce::Colour (0xfffff4d6), face.getCentreX(), face.getBottom() + face.getHeight() * 0.1f,
                                juce::Colour (0xffc7b283), face.getX(), face.getY(), true);
    cream.addColour (0.6, juce::Colour (0xffeedfb1));
    g.setGradientFill (cream);
    g.fillRect (face);
    g.setColour (juce::Colour (0x18603f10));
    g.fillEllipse (face.getRight() - face.getWidth() * 0.35f, face.getY() + 4, face.getWidth() * 0.18f, face.getHeight() * 0.3f);

    // Scale: -20 … +3 over a 96° arc, red from 0 VU.
    const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.12f);
    const auto radius = face.getHeight() * 0.85f;
    const std::pair<const char*, float> marks[] { { "20", -48 }, { "10", -36 }, { "7", -26 }, { "5", -18 }, { "3", -8 },
                                                  { "2", -2 }, { "1", 4 }, { "0", 12 }, { "1", 20 }, { "2", 30 }, { "3", 42 } };
    const auto ink = juce::Colour (0xff2a2418), redInk = juce::Colour (0xffc4321f);
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0, juce::degreesToRadians (-48.0f), juce::degreesToRadians (42.0f), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.0f));
    juce::Path redArc;
    redArc.addCentredArc (pivot.x, pivot.y, radius + 3, radius + 3, 0, juce::degreesToRadians (12.0f), juce::degreesToRadians (42.0f), true);
    g.setColour (redInk);
    g.strokePath (redArc, juce::PathStrokeType (4.0f));
    g.setFont (label (juce::jmax (8.0f, face.getHeight() * 0.11f)));
    for (std::size_t i = 0; i < std::size (marks); ++i)
    {
        const auto angle = juce::degreesToRadians (marks[i].second);
        const auto c = i >= 7 ? redInk : ink;
        const auto p1 = pivot.getPointOnCircumference (radius, angle), p2 = pivot.getPointOnCircumference (radius + 7, angle);
        g.setColour (c);
        g.drawLine ({ p1, p2 }, 1.3f);
        const auto t = pivot.getPointOnCircumference (radius + 15, angle);
        g.drawText ((i >= 8 ? "+" : i < 7 ? "-" : "") + juce::String (marks[i].first),
                    juce::Rectangle<float> (24, 12).withCentre (t), juce::Justification::centred);
    }
    g.setColour (ink);
    g.setFont (brand (juce::jmax (10.0f, face.getHeight() * 0.15f)));
    g.drawText ("VU", face.withTrimmedTop (face.getHeight() * 0.45f).withHeight (face.getHeight() * 0.2f), juce::Justification::centred);
    g.setFont (label (juce::jmax (7.0f, face.getHeight() * 0.08f)));
    g.drawText (caption, face.withTrimmedTop (face.getHeight() * 0.66f).withHeight (face.getHeight() * 0.12f), juce::Justification::centred);

    // Needle and its housing.
    const auto angle = juce::degreesToRadians (-50.0f + 94.0f * juce::jlimit (0.0f, 1.05f, level));
    const auto tip = pivot.getPointOnCircumference (radius + 4, angle);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawLine ({ pivot.translated (2, 2), tip.translated (2, 2) }, 1.4f);
    g.setColour (juce::Colour (0xff151515));
    g.drawLine ({ pivot, tip }, 1.4f);
    g.fillRect (juce::Rectangle<float> (face.getWidth() * 0.3f, face.getHeight() * 0.16f)
                    .withCentre ({ face.getCentreX(), face.getBottom() - face.getHeight() * 0.06f }));

    // Glass.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (face.withHeight (face.getHeight() * 0.32f));
}

juce::Rectangle<int> RackUnit::content() const
{
    return getLocalBounds().reduced (earWidth, 0).reduced (inset, 16);
}

void RackUnit::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    faceplate (g, b.reduced (static_cast<float> (earWidth), 0));
    rackEar (g, b.withWidth (static_cast<float> (earWidth)), slots);
    rackEar (g, b.withLeft (b.getRight() - static_cast<float> (earWidth)), slots);
    // A thin black gap between units.
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));
}

VuMeter::VuMeter (juce::String c) : caption (std::move (c))
{
    setOpaque (false);
    startTimerHz (30);
}

void VuMeter::timerCallback()
{
    // VU ballistics: ~300 ms to rise to a level. 0 VU sits at -18 dBFS RMS.
    const auto rms = source ? source() : 0.0f;
    const auto db = (rms > 0.001f ? 20.0f * std::log10 (rms) : -60.0f) + 18.0f;  // in VU
    const auto target = juce::jlimit (0.0f, 1.05f, juce::jmap (db, -20.0f, 3.0f, 0.0f, 1.0f));
    needle += (target - needle) * 0.22f;
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    vuMeter (g, getLocalBounds().toFloat(), needle, caption);
}

} // namespace dss1::ui::hw
