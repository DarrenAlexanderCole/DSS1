#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dss1::ui
{

/// The machine's smoked-glass dust cover, closed over it when DSS1 opens: DSS1 etched into the glass, the
/// room reflected in it, a little dust. Click it (or press any key) and it slides up out of the frame like a
/// lid lifting, its edge catching the light and its shadow sweeping off the panels, then it's gone.
class DustCover : public juce::Component, private juce::Timer
{
public:
    DustCover();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /// Lifts the cover (from a click or a key).
    void open();
    bool isOpening() const noexcept { return opening; }

    /// Called once it's fully open (the cover has removed itself from view).
    std::function<void()> onOpened;

private:
    void timerCallback() override;

    bool opening = false;
    double startMs = 0, idleStartMs = 0;
    float progress = 0;  // 0 closed … 1 gone
};

} // namespace dss1::ui
