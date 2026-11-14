#pragma once

#include "core/BeatGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// Draws a beat grid as bar and beat lines over a time strip. In Phase 1 it shows a demo
/// variable-tempo grid, proving the app is wired to the core library; Prep will reuse it.
class GridPreview : public juce::Component
{
public:
    GridPreview();

    void setGrid (BeatGrid newGrid, Frame lengthInFrames);
    void paint (juce::Graphics&) override;

private:
    BeatGrid grid;
    Frame length = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPreview)
};

} // namespace dss1::ui
