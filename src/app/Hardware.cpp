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
        juce::Image panelWear, grime, edgeWear, aluminium, inkWear, brushed, walnut, scratches, keyWear;

        Assets()
        {
            using namespace dss1_assets;
            labelBold = juce::Typeface::createSystemTypefaceFor (ArchivoNarrowBold_ttf, ArchivoNarrowBold_ttfSize);
            labelMedium = juce::Typeface::createSystemTypefaceFor (ArchivoNarrowMedium_ttf, ArchivoNarrowMedium_ttfSize);
            brandFace = juce::Typeface::createSystemTypefaceFor (MichromaRegular_ttf, MichromaRegular_ttfSize);
            vfdFace = juce::Typeface::createSystemTypefaceFor (RajdhaniTabSemiBold_ttf, RajdhaniTabSemiBold_ttfSize);
            panelWear = juce::ImageCache::getFromMemory (panelwear_png, panelwear_pngSize);
            grime = juce::ImageCache::getFromMemory (grime_png, grime_pngSize);
            edgeWear = juce::ImageCache::getFromMemory (edgewear_png, edgewear_pngSize);
            aluminium = juce::ImageCache::getFromMemory (aluminium_png, aluminium_pngSize);
            inkWear = juce::ImageCache::getFromMemory (inkwear_png, inkwear_pngSize);
            brushed = juce::ImageCache::getFromMemory (brushed_png, brushed_pngSize);
            walnut = juce::ImageCache::getFromMemory (walnut_png, walnut_pngSize);
            scratches = juce::ImageCache::getFromMemory (scratches_png, scratches_pngSize);
            keyWear = juce::ImageCache::getFromMemory (keywear_png, keywear_pngSize);
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
// Rajdhani's capitals sit lower in its line than the old display face's did; scaled so readouts keep their size.
juce::Font vfd (float size) { return fontFrom (Assets::get().vfdFace, size * 1.06f); }

//--- Light ----------------------------------------------------------------------------------------------------

namespace
{
    struct Light
    {
        double start = juce::Time::getMillisecondCounterHiRes();
        float sag = 0;  // 0 … 1, how far the supply has dipped
        double lastFeed = 0;
    };
    Light& lightState()
    {
        static Light l;
        return l;
    }
}

float light()
{
    auto& l = lightState();
    const auto t = (juce::Time::getMillisecondCounterHiRes() - l.start) / 1000.0;
    // Warm-up: filaments glow in over about 1.6 s, with a couple of flickers as they catch.
    auto warm = static_cast<float> (juce::jlimit (0.0, 1.0, (t - 0.15) / 1.6));
    warm = warm * warm * (3.0f - 2.0f * warm);
    if (t < 1.2)
        warm *= 1.0f - 0.35f * static_cast<float> (std::max (0.0, std::sin (t * 37.0)) * std::exp (-t * 2.0));
    // Breathing: a slow swell, and a faint shimmer like mains hum on the supply.
    const auto breathe = 1.0f + 0.018f * static_cast<float> (std::sin (t * 2.0 * juce::MathConstants<double>::pi * 0.23))
                         + 0.006f * static_cast<float> (std::sin (t * 2.0 * juce::MathConstants<double>::pi * 7.1));
    return juce::jlimit (0.0f, 1.05f, warm * breathe * (1.0f - 0.07f * l.sag));
}

void feedLight (float rms)
{
    // Loud passages pull the supply down quickly; it recovers slowly. Called about 30 times a second.
    auto& l = lightState();
    const auto target = juce::jlimit (0.0f, 1.0f, (rms - 0.12f) * 4.0f);
    l.sag = target > l.sag ? l.sag + (target - l.sag) * 0.6f : l.sag + (target - l.sag) * 0.06f;
}

void wear (juce::Graphics& g, juce::Rectangle<float> cap, juce::Point<float> seedPoint, bool darkSurface)
{
    // A different patch of the wear texture for every control, so no two keys are worn alike.
    auto& a = Assets::get();
    if (! a.keyWear.isValid() || cap.isEmpty())
        return;
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (cap.toNearestInt());
    const auto ox = hash (seedPoint.x, seedPoint.y) * static_cast<float> (a.keyWear.getWidth());
    const auto oy = hash (seedPoint.y, seedPoint.x) * static_cast<float> (a.keyWear.getHeight());
    if (darkSurface)
    {
        g.setTiledImageFill (a.keyWear, juce::roundToInt (cap.getX() - ox), juce::roundToInt (cap.getY() - oy), 0.85f);
        g.fillRect (cap);
    }
    else
    {
        // On ivory and colour, wear shows as a faint grey of handling rather than bright scratches.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawImageAt (a.keyWear, juce::roundToInt (cap.getX() - ox), juce::roundToInt (cap.getY() - oy), true);
    }
}

juce::Image part (const juce::String& name)
{
    // Rendered parts are compiled in as e.g. knob_alu_png.
    int size = 0;
    const auto resource = (name + "_png").replaceCharacter ('-', '_');
    if (const auto* data = dss1_assets::getNamedResource (resource.toRawUTF8(), size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}

void nineSlice (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest, int cx, int cy, float scale)
{
    if (! img.isValid())
        return;
    const auto sw = img.getWidth(), sh = img.getHeight();
    const auto dx = static_cast<float> (cx) * scale, dy = static_cast<float> (cy) * scale;
    const float xs[] { dest.getX(), dest.getX() + dx, dest.getRight() - dx, dest.getRight() };
    const float ys[] { dest.getY(), dest.getY() + dy, dest.getBottom() - dy, dest.getBottom() };
    const int sxs[] { 0, cx, sw - cx, sw };
    const int sys[] { 0, cy, sh - cy, sh };
    g.setOpacity (1.0f);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
        {
            const auto w = xs[i + 1] - xs[i], h = ys[j + 1] - ys[j];
            if (w <= 0 || h <= 0)
                continue;
            g.drawImage (img, juce::roundToInt (xs[i]), juce::roundToInt (ys[j]), juce::roundToInt (xs[i + 1]) - juce::roundToInt (xs[i]),
                         juce::roundToInt (ys[j + 1]) - juce::roundToInt (ys[j]), sxs[i], sys[j], sxs[i + 1] - sxs[i], sys[j + 1] - sys[j]);
        }
}

void screw (juce::Graphics& g, juce::Point<float> c, float r)
{
    if (const auto img = part ("screw"); img.isValid())
    {
        // The rendered screw (its head is about 0.77 of the image width), each turned a little differently.
        const auto size = r * 2.0f / 0.77f;
        const auto angle = hash (c.x, c.y) * juce::MathConstants<float>::pi;
        g.setOpacity (1.0f);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-img.getWidth() / 2.0f, -img.getHeight() / 2.0f)
                                         .rotated (angle)
                                         .scaled (size / static_cast<float> (img.getWidth()))
                                         .translated (c.x, c.y));
        return;
    }
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

    // Brushed aluminium, tiled from fixed positions so the grain doesn't crawl.
    if (a.brushed.isValid())
    {
        g.setTiledImageFill (a.brushed, 0, 0, 1.0f);
        g.fillRect (area);
    }
    else
    {
        g.setColour (juce::Colour (0xff9a9c9f));
        g.fillRect (area);
    }
    // The light: a broad warm sheen from the upper left, falling off to the lower right, and a cool lift at the
    // far edge, the way brushed metal catches a room.
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.20f), area.getX() + area.getWidth() * 0.18f, area.getY(),
                                juce::Colours::black.withAlpha (0.16f), area.getX() + area.getWidth() * 0.62f, area.getBottom(), false);
    sheen.addColour (0.45, juce::Colours::transparentWhite);
    g.setGradientFill (sheen);
    g.fillRect (area);
    juce::ColourGradient lift (juce::Colours::transparentWhite, area.getRight() - area.getWidth() * 0.25f, area.getY(),
                               juce::Colour (0x1ad8e6ff), area.getRight(), area.getY(), false);
    g.setGradientFill (lift);
    g.fillRect (area);

    // A little honest wear: fingerprints and scuffs, faint.
    if (a.panelWear.isValid())
    {
        g.setOpacity (0.22f);
        g.setTiledImageFill (a.panelWear, 0, 0, 0.22f);
        g.fillRect (area);
    }

    // Hairline scratches along the grain: years of use, looked after.
    if (a.scratches.isValid())
    {
        g.setTiledImageFill (a.scratches, juce::roundToInt (area.getX() * 0.37f), juce::roundToInt (area.getY() * 0.61f), 0.55f);
        g.fillRect (area);
    }

    // Bevel: a bright chamfer along the top edge, shadow along the bottom.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRect (area.withHeight (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillRect (area.withHeight (2.0f).translated (0, 1));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (area.withTop (area.getBottom() - 2.0f));

    if (screws && area.getWidth() > 60 && area.getHeight() > 50)
    {
        const auto r = 6.5f, m = 14.0f;
        screw (g, { area.getX() + m, area.getY() + m }, r);
        screw (g, { area.getRight() - m, area.getY() + m }, r);
        screw (g, { area.getX() + m, area.getBottom() - m }, r);
        screw (g, { area.getRight() - m, area.getBottom() - m }, r);
    }
}

void rackEar (juce::Graphics&, juce::Rectangle<float>, int) {}

void sideBoard (juce::Graphics& g, juce::Rectangle<float> area, bool left)
{
    // One piece of oiled walnut from top to bottom, rounded on its outer edge, with the slot the racks sit in
    // along its inner edge.
    auto& a = Assets::get();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());
    if (a.walnut.isValid())
    {
        // Each side from a different part of the plank (the right one mirrored), so they aren't twins.
        const auto half = a.walnut.getWidth() / 2;
        const auto h = juce::jmin (a.walnut.getHeight() - (left ? 0 : a.walnut.getHeight() / 3), juce::roundToInt (area.getHeight()));
        const auto srcY = left ? 0 : a.walnut.getHeight() / 3;
        g.setOpacity (1.0f);
        if (! left)
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, area.getCentreX(), 0));
        g.drawImage (a.walnut, juce::roundToInt (area.getX()), juce::roundToInt (area.getY()), juce::roundToInt (area.getWidth()), juce::roundToInt (area.getHeight()),
                     left ? 0 : half, srcY, half, h);
        if (! left)
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, area.getCentreX(), 0));
    }
    else
    {
        g.setColour (juce::Colour (0xff4a2412));
        g.fillRect (area);
    }
    const auto outer = left ? area.getX() : area.getRight();
    const auto inner = left ? area.getRight() : area.getX();
    // Rounded outer edge, a soft highlight where it turns towards the light, flat towards the racks.
    // Matte, oiled not lacquered: no shine, just the turn of the edge into shadow, so the board stays back and
    // the machine stands forward.
    juce::ColourGradient round (juce::Colours::black.withAlpha (0.7f), outer, 0, juce::Colours::black.withAlpha (0.35f), inner, 0, false);
    round.addColour (0.15, juce::Colours::black.withAlpha (0.2f));
    round.addColour (0.35, juce::Colour (0x0affefd8));
    round.addColour (0.6, juce::Colours::black.withAlpha (0.12f));
    g.setGradientFill (round);
    g.fillRect (area);
    // Light from above on the oil finish, fading down the board; the ends a little darker (end grain).
    juce::ColourGradient top (juce::Colour (0x0cfff2e2), 0, area.getY(), juce::Colours::transparentWhite, 0, area.getY() + area.getHeight() * 0.45f, false);
    g.setGradientFill (top);
    g.fillRect (area);
    juce::ColourGradient ends (juce::Colours::black.withAlpha (0.45f), 0, area.getY(), juce::Colours::transparentBlack, 0, area.getY() + 10, false);
    g.setGradientFill (ends);
    g.fillRect (area);
    juce::ColourGradient endsBottom (juce::Colours::transparentBlack, 0, area.getBottom() - 10, juce::Colours::black.withAlpha (0.5f), 0, area.getBottom(), false);
    g.setGradientFill (endsBottom);
    g.fillRect (area);
    // The routed slot on the inner edge: a dark groove with a bright lip.
    const auto dir = left ? -1.0f : 1.0f;
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRect (juce::Rectangle<float> (inner + (left ? -3.0f : 0.0f), area.getY(), 3.0f, area.getHeight()));
    g.setColour (juce::Colour (0x40ffe8cc));
    g.fillRect (juce::Rectangle<float> (inner + dir * 4.0f - (left ? 1.0f : 0.0f), area.getY(), 1.0f, area.getHeight()));
}

void recess (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    // The window is cut into the aluminium: a bright chamfer below (lit from above), a dark lip above, then
    // the black glass with an inner shadow.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (area.expanded (4).translated (0, 1.0f), corner + 3);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (area.expanded (4).translated (0, -0.5f), corner + 3);
    g.setColour (juce::Colour (0xff111113));
    g.fillRoundedRectangle (area.expanded (2.5f), corner + 2);
    g.setColour (colour::glassBlack);
    g.fillRoundedRectangle (area, corner);
    juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.85f), 0, area.getY(), juce::Colours::transparentBlack, 0, area.getY() + 14, false);
    g.setGradientFill (shadow);
    g.fillRoundedRectangle (area, corner);
}

void glass (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    // Layers over the lit display: a soft vignette where the glass curves into the bezel, a diagonal
    // reflection of the room, and a bright edge where the glass meets the bezel.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());
    juce::ColourGradient vignette (juce::Colours::transparentBlack, area.getCentreX(), area.getCentreY(), juce::Colours::black.withAlpha (0.28f),
                                   area.getX(), area.getY(), true);
    g.setGradientFill (vignette);
    g.fillRoundedRectangle (area, corner);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), area.getX(), area.getY(), juce::Colours::transparentWhite,
                                area.getX() + area.getWidth() * 0.3f, area.getY() + area.getHeight() * 0.7f, false);
    sheen.addColour (0.55, juce::Colours::white.withAlpha (0.025f));
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (area, corner);
    // A soft band of reflection crossing the glass.
    juce::Path band;
    const auto w = area.getWidth(), h = area.getHeight();
    band.startNewSubPath (area.getX() + w * 0.55f, area.getY());
    band.lineTo (area.getX() + w * 0.62f, area.getY());
    band.lineTo (area.getX() + w * 0.62f - h * 0.6f, area.getBottom());
    band.lineTo (area.getX() + w * 0.55f - h * 0.6f, area.getBottom());
    band.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    g.fillPath (band);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (area.getX() + corner, area.getY() + 0.5f, area.getRight() - corner, area.getY() + 0.5f, 1.0f);
}

void readout (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font, juce::Colour c)
{
    // As wide as the text needs (a little more), centred where it was asked for.
    const auto textW = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
    const auto win = area.withSizeKeepingCentre (juce::jmax (textW, area.getHeight() * 1.6f), area.getHeight());
    recess (g, win, 3.0f);
    glowText (g, text, win, font, c, juce::Justification::centred);
    glass (g, win, 3.0f);
}

void bay (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    // Engraved: a dark line, with a bright line just below and right of it where the light catches the cut.
    g.setColour (juce::Colours::black.withAlpha (0.045f));
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (area.translated (0.8f, 1.0f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.drawRoundedRectangle (area, corner, 1.0f);
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
            s.append (text, label (size), colour::ink);
            if (sub.isNotEmpty())
                s.append ("  " + juce::String::fromUTF8 ("\xe2\x80\x94") + " " + sub, label (size), colour::inkRed);
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
    // The display's light: dims while warming up, breathes, dips with the music.
    const auto lit = light();
    c = c.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * lit)).withMultipliedBrightness (0.9f + 0.1f * lit);
    // Bloom: a wide faint glow, a soft halo, then the bright text.
    g.setColour (c.withAlpha (0.035f * lit));
    for (const auto& d : { juce::Point<float> (-3.0f, 0.0f), { 3.0f, 0.0f }, { 0.0f, -3.0f }, { 0.0f, 3.0f } })
        g.drawText (text, area.translated (d.x, d.y), justification, true);
    g.setColour (c.withAlpha (0.09f * lit));
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
        colour = colour.withMultipliedBrightness (0.55f + 0.45f * light());
        g.setColour (colour.withAlpha (0.25f * light()));
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
    // Backlit: two lamps behind the face, warm, following the light level.
    const auto lit = light();
    const auto lamp = juce::Colour (0xfff7c98a).interpolatedWith (juce::Colour (0xffffe9c4), lit);
    juce::ColourGradient cream (lamp.withMultipliedBrightness (0.55f + 0.45f * lit), face.getCentreX(), face.getBottom() + face.getHeight() * 0.1f,
                                juce::Colour (0xff8a6a3a).withMultipliedBrightness (0.6f + 0.4f * lit), face.getX(), face.getY(), true);
    cream.addColour (0.55, juce::Colour (0xffeccf98).withMultipliedBrightness (0.6f + 0.4f * lit));
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

    // Glass: a curved reflection across the top, and the bezel's shadow on the face.
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.18f), face.getX(), face.getY(), juce::Colours::transparentWhite, face.getX(),
                               face.getY() + face.getHeight() * 0.38f, false);
    g.setGradientFill (refl);
    g.fillRect (face.withHeight (face.getHeight() * 0.38f));
    juce::ColourGradient lip (juce::Colours::black.withAlpha (0.45f), face.getX(), face.getY(), juce::Colours::transparentBlack, face.getX(),
                              face.getY() + 10.0f, false);
    g.setGradientFill (lip);
    g.fillRect (face);
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
    startTimerHz (60);
}

void VuMeter::timerCallback()
{
    // VU ballistics: ~300 ms to rise to a level. 0 VU sits at -18 dBFS RMS.
    const auto rms = source ? source() : 0.0f;
    feedLight (rms);
    const auto db = (rms > 0.001f ? 20.0f * std::log10 (rms) : -60.0f) + 18.0f + padDb;  // in VU
    auto target = juce::jlimit (0.0f, 1.05f, juce::jmap (db, -20.0f, 3.0f, 0.0f, 1.0f));
    // On power-up the needles kick across and fall back, like real meters.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    static const auto powerOn = now;
    const auto since = (now - powerOn) / 1000.0;
    if (since > 0.3 && since < 0.75)
        target = juce::jmax (target, 0.85f);
    // A sprung, damped needle (about 300 ms to reach a level, a little overshoot).
    const auto dt = static_cast<float> (juce::jlimit (0.001, 0.05, lastTick > 0 ? (now - lastTick) / 1000.0 : 1.0 / 60.0));
    lastTick = now;
    constexpr float w = 2.0f * 3.14159265f * 2.1f, zeta = 0.62f;
    const auto accel = w * w * (target - needle) - 2.0f * zeta * w * velocity;
    velocity += accel * dt;
    needle = juce::jlimit (-0.02f, 1.1f, needle + velocity * dt);
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    vuMeter (g, getLocalBounds().toFloat(), needle, caption);
}

} // namespace dss1::ui::hw
