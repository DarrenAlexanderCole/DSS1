#pragma once

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dss1::ui
{

/// Prep's big waveform: zoomable and scrollable, with the beat grid, hot cues, memory cues, saved loops and the
/// playhead drawn on it. Scroll up and down (or pinch) to zoom smoothly around the mouse; swipe sideways, Shift +
/// scroll or drag the background to move through the track; drag a cue to move it; click to jump; right-click
/// for actions at that spot. While the deck plays, the view follows it.
class WaveformView : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit WaveformView (Session& session);
    ~WaveformView() override;

    /// Where a new or moved cue lands: the nearest beat when snapping is on and there's a grid.
    std::function<Frame (Frame)> snap = [] (Frame f) { return f; };
    /// Builds the right-click menu for a spot with no marker under it.
    std::function<void (juce::PopupMenu&, Frame)> addBackgroundActions;

    /// Zooms smoothly by `factor` (frames per pixel: below 1 is closer in), keeping the spot at `anchorX` (or the
    /// playhead while it plays and is in view) where it is.
    void zoomBy (double factor, std::optional<float> anchorX = std::nullopt);
    void showWholeTrack();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    struct Marker
    {
        enum class Kind { hotCue, memoryCue, loop } kind = Kind::hotCue;
        std::size_t index = 0;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    double frameAtX (float x) const;
    float xForFrame (double frame) const;
    double visibleFrames() const;
    void clampView();
    std::optional<Marker> markerAt (juce::Point<float> p) const;
    Frame markerFrame (const TrackRecord& r, const Marker& m) const;
    void moveMarker (TrackRecord& r, const Marker& m, Frame to) const;
    void showMarkerMenu (const Marker& m);
    void showBackgroundMenu (Frame at);

    void paintWaveform (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintGrid (juce::Graphics&, juce::Rectangle<float> area, const TrackRecord& r) const;
    void paintMarkers (juce::Graphics&, juce::Rectangle<float> area, const TrackRecord& r) const;

    Session& session;
    double viewStart = 0;          ///< frame at the left edge
    double framesPerPixel = 200;
    bool following = true;         ///< keep the playhead in view while playing
    juce::uint32 followAgainAt = 0;  ///< after scrolling by wheel, follow the playhead again from this time
    double targetFramesPerPixel = 200;  ///< the zoom is easing towards this
    double zoomAnchorFrame = 0;
    double zoomAnchorX = 0;
    double minFramesPerPixel() const noexcept { return 2.0; }
    double maxFramesPerPixel() const;
    Session::State lastState = Session::State::empty;

    // Mouse gestures.
    std::optional<Marker> dragging;
    std::optional<TrackRecord> dragRecord;  ///< the record while a marker is being moved (saved on release)
    double dragStartView = 0;
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

/// True if the deck is looping exactly this saved loop.
bool deckIsPlayingLoop (engine::Deck& deck, const SavedLoop& loop);

/// Hot cue colours A–H when a cue has none of its own (Serato's defaults, which Rekordbox shows well too).
juce::Colour hotCueColour (const TrackRecord& r, std::size_t slot);

} // namespace dss1::ui
