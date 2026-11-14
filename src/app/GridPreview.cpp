#include "GridPreview.h"
#include "Theme.h"

namespace dss1::ui
{

GridPreview::GridPreview()
{
    // Demo: 32 bars at 124 BPM that speed up to 128 BPM from bar 17.
    constexpr double rate = 44100.0;
    const auto change = static_cast<Frame> (16 * 4 * rate * 60.0 / 124.0);
    const BeatGrid demo (rate, { { 0, 124.0, 1, 4 }, { change, 128.0, 1, 4 } });
    setGrid (demo, static_cast<Frame> (demo.frameAt (32 * 4)));
}

void GridPreview::setGrid (BeatGrid newGrid, Frame lengthInFrames)
{
    grid = std::move (newGrid);
    length = lengthInFrames;
    repaint();
}

void GridPreview::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 10.0f);

    if (grid.isEmpty() || length <= 0)
        return;

    const auto strip = area.reduced (16.0f).withTrimmedTop (22.0f);
    const auto toX = [&] (double frame) {
        return strip.getX() + static_cast<float> (frame / static_cast<double> (length)) * strip.getWidth();
    };

    // Beat and bar lines.
    for (const auto& beat : grid.beatsBetween (0, static_cast<double> (length)))
    {
        const auto x = toX (beat.frame);
        g.setColour (beat.isDownbeat() ? Colours::downbeat : Colours::beat);
        g.fillRect (x, strip.getY(), beat.isDownbeat() ? 1.5f : 1.0f, strip.getHeight());
    }

    // Tempo markers along the top, one per anchor.
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (const auto& anchor : grid.anchors())
    {
        const auto x = toX (static_cast<double> (anchor.frame));
        g.setColour (Colours::tempoMark);
        g.fillRect (x, strip.getY() - 18.0f, 2.0f, strip.getHeight() + 18.0f);
        g.drawText (juce::String (anchor.bpm, 1) + " BPM",
                    juce::Rectangle<float> (x + 5.0f, strip.getY() - 20.0f, 90.0f, 16.0f),
                    juce::Justification::centredLeft);
    }
}

} // namespace dss1::ui
