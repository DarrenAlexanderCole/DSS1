#pragma once

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

class ChopPanel;

/// The CHOP screen's waveform: the bank's sample with its slice markers, zoomable like Prep's. Scroll up and
/// down (or pinch) to zoom around the mouse; swipe sideways, Shift + scroll or drag the background to move;
/// the strip along the bottom shows the whole sample and where you are in it. Drag a marker to move it; click a
/// slice to hear it (in CHOP mode, a click moves the chosen pad's marker there); double-click a slice to zoom
/// to it; right-click for more. Follows the playhead while the deck (or the file preview) plays.
class ChopView : public juce::Component
{
public:
    explicit ChopView (ChopPanel& owner);

    /// Called by the panel 30 times a second: eases the zoom and follows the playhead.
    void tick();
    void zoomBy (double factor, std::optional<float> anchorX = std::nullopt);
    void showWhole();
    void showRange (double from, double to);
    /// Brings the view to the bank's slices (or the playhead) when the bank or its sample changes.
    void reset();
    /// The frames on screen.
    std::pair<double, double> visibleRange() const { return { viewStart, viewStart + visibleFrames() }; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    double frameAtX (float x) const;
    float xForFrame (double frame) const;
    double visibleFrames() const;
    double length() const;
    double playhead (bool& playing) const;
    void clampView();
    int markerNear (float x) const;
    void ensurePeaks (const io::DecodedAudio*);
    void paintWave (juce::Graphics&, juce::Rectangle<float> area, const io::DecodedAudio&) const;
    void paintOverview (juce::Graphics&, juce::Rectangle<float> area, const io::DecodedAudio&) const;
    void moveViewToOverview (float x);

    ChopPanel& owner;
    juce::Rectangle<int> waveArea, overviewArea;
    double viewStart = 0, framesPerPixel = 400, targetFramesPerPixel = 400, zoomAnchorFrame = 0, zoomAnchorX = 0;
    bool following = true;
    juce::uint32 followAgainAt = 0;

    // Peaks of the shown sample, one min/max pair per 256 frames (for drawing zoomed out).
    const io::DecodedAudio* peaksFor = nullptr;
    std::vector<float> peakMin, peakMax;
    juce::Colour waveColour { 0xffff9a2e };
    static constexpr int peakBlock = 256;

    enum class Drag { none, marker, pan, overview } drag = Drag::none;
    int dragMarker = -1;
    double dragStartView = 0;
    float dragStartX = 0;
    bool moved = false;
};

} // namespace dss1::ui
