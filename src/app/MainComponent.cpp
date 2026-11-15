#include "MainComponent.h"
#include "GridPreview.h"
#include "StagePanel.h"
#include "TestExportPanel.h"

namespace dss1::ui
{

MainComponent::MainComponent()
{
    setLookAndFeel (&theme);

    auto catalog = std::make_unique<StagePanel> (
        "Catalog", "Bring music in and keep it organised.",
        juce::StringArray { "Drop files, folders and record-pool zips",
                            "Browse by BPM, key, loudness and tags",
                            "Moved or renamed files keep their cues" });

    auto prep = std::make_unique<StagePanel> (
        "Prep", "Get every track gig-ready: grid, cues and loops, set once.",
        juce::StringArray { "Beat grids, constant or variable tempo",
                            "Hot cues A-H and unlimited memory cues",
                            "Saved loops, checked against Rekordbox and Serato limits" });
    prep->setShowcase (std::make_unique<GridPreview>());

    auto design = std::make_unique<StagePanel> (
        "Listen & Design", "Hear it, shape it, make your own samples.",
        juce::StringArray { "Listening deck with real-time effects",
                            "Recorder for takes",
                            "XY performance pad and a pad sampler" });

    auto deliver = std::make_unique<StagePanel> (
        "Deliver", "Send prepared tracks to the software you play on.",
        juce::StringArray { "Rekordbox XML export and import",
                            "Serato-tagged copies",
                            "MPC projects and WAV slices" });
    deliver->setShowcase (std::make_unique<TestExportPanel>());

    const auto bg = Colours::background;
    stages.setTabBarDepth (44);
    stages.addTab ("Catalog", bg, catalog.release(), true);
    stages.addTab ("Prep", bg, prep.release(), true);
    stages.addTab ("Listen & Design", bg, design.release(), true);
    stages.addTab ("Deliver", bg, deliver.release(), true);
    stages.setCurrentTabIndex (3);  // test builds open on Deliver, where the test export lives
    addAndMakeVisible (stages);

    setSize (1200, 760);
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
}

void MainComponent::resized()
{
    stages.setBounds (getLocalBounds());
}

} // namespace dss1::ui
