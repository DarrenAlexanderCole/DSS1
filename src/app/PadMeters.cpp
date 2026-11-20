#include "PadMeters.h"
#include "Hardware.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    constexpr int rows = 16;
    constexpr float floorDb = -42.0f;  // the bottom light; the top one is +3 dB over full scale

    float toUnit (float gain)
    {
        if (gain <= 0.0f)
            return 0.0f;
        const auto db = 20.0f * std::log10 (gain);
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / (3.0f - floorDb));
    }
}

PadMeters::PadMeters (engine::Sampler& s, std::function<int()> currentBank) : sampler (s), bank (std::move (currentBank))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (60);
}

void PadMeters::timerCallback()
{
    const auto b = juce::jlimit (0, engine::Sampler::banks - 1, bank ? bank() : 0);
    if (b != shownBank)
    {
        // A new bank: start from dark rather than carrying the other bank's lights over.
        shownBank = b;
        level = {};
        peak = {};
    }
    // Every bank's peaks are taken, so a pad from another bank (MIDI, say) doesn't flash in later.
    for (int other = 0; other < engine::Sampler::banks; ++other)
        for (int p = 0; p < 16; ++p)
        {
            const auto v = toUnit (sampler.takePeak (other * 16 + p));
            if (other != b)
                continue;
            auto& l = level[static_cast<std::size_t> (p)];
            l = v > l ? v : l * 0.90f;  // jumps up, falls back
            auto& pk = peak[static_cast<std::size_t> (p)];
            auto& hold = peakHold[static_cast<std::size_t> (p)];
            if (l >= pk)
            {
                pk = l;
                hold = 36;  // hangs for about 0.6 s
            }
            else if (hold > 0)
                --hold;
            else
                pk = juce::jmax (0.0f, pk - 0.012f);
        }
    if (isShowing())
        repaint();
}

void PadMeters::paint (juce::Graphics& g)
{
    const auto lit = hw::light();
    const auto window = getLocalBounds().toFloat();
    hw::recess (g, window, 6.0f);
    auto area = window.reduced (9.0f, 9.0f);
    auto labels = area.removeFromBottom (12.0f);
    area.removeFromBottom (3.0f);

    const auto colGap = juce::jmax (1.5f, area.getWidth() * 0.012f);
    const auto colW = (area.getWidth() - colGap * 15.0f) / 16.0f;
    const auto rowGap = juce::jmax (1.5f, area.getHeight() * 0.012f);
    const auto rowH = (area.getHeight() - rowGap * static_cast<float> (rows - 1)) / static_cast<float> (rows);
    const auto amber = hw::colour::amber;
    const juce::Colour hot (0xffffe7c2), red (0xffff4a2a);

    for (int p = 0; p < 16; ++p)
    {
        const auto x = area.getX() + static_cast<float> (p) * (colW + colGap);
        const auto lv = level[static_cast<std::size_t> (p)];
        const auto lightsOn = lv * static_cast<float> (rows);
        const auto peakRow = static_cast<int> (std::ceil (peak[static_cast<std::size_t> (p)] * static_cast<float> (rows))) - 1;
        for (int r = 0; r < rows; ++r)
        {
            const auto y = area.getBottom() - static_cast<float> (r + 1) * rowH - static_cast<float> (r) * rowGap;
            const auto cell = juce::Rectangle<float> (x, y, colW, rowH);
            // The top lights are red (over full scale), the next ones warm white, the rest amber.
            const auto c = r >= rows - 2 ? red : r >= rows - 5 ? amber.interpolatedWith (hot, 0.45f) : amber;
            // How lit: fully below the level, partly in the light it's reaching into; the peak light on too.
            auto on = juce::jlimit (0.0f, 1.0f, lightsOn - static_cast<float> (r));
            if (r == peakRow && peakRow >= 0)
                on = juce::jmax (on, 0.8f);
            g.setColour (c.withAlpha (0.07f * lit));
            g.fillRoundedRectangle (cell, juce::jmin (colW, rowH) * 0.2f);
            if (on > 0.02f)
            {
                g.setColour (c.withAlpha (0.16f * on * lit));
                g.fillRoundedRectangle (cell.expanded (juce::jmin (colW, rowH) * 0.45f), juce::jmin (colW, rowH) * 0.5f);
                juce::ColourGradient face (c.interpolatedWith (hot, 0.5f).withAlpha (on * lit), cell.getCentreX(), cell.getCentreY(),
                                           c.withAlpha (0.9f * on * lit), cell.getX(), cell.getY(), true);
                g.setGradientFill (face);
                g.fillRoundedRectangle (cell, juce::jmin (colW, rowH) * 0.2f);
            }
        }
        // Pad numbers under the columns (every pad when there's room, else 1, 4, 8, 12, 16).
        if (colW >= 9.0f || p == 0 || (p + 1) % 4 == 0)
        {
            g.setFont (hw::vfd (11.0f));
            g.setColour (amber.withAlpha (0.55f * lit));
            g.drawText (juce::String (p + 1), juce::Rectangle<float> (x - 6.0f, labels.getY(), colW + 12.0f, labels.getHeight()),
                        juce::Justification::centred, false);
        }
    }
    hw::glass (g, window, 6.0f);
}

} // namespace dss1::ui
