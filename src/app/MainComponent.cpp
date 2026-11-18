#include "MainComponent.h"
#include "CatalogPanel.h"
#include "PrepPanel.h"
#include "StagePanel.h"
#include "DeliverPanel.h"
#include "Theme.h"

namespace dss1::ui
{


MainComponent::MainComponent()
{
    setLookAndFeel (&theme);

    auto catalog = std::make_unique<CatalogPanel> (session);
    auto prep = std::make_unique<PrepPanel> (session);

    auto design = std::make_unique<StagePanel> (
        "Listen & Design", "Hear it, shape it, make your own samples.",
        juce::StringArray { "Listening deck with real-time effects",
                            "Recorder for takes",
                            "XY performance pad and a pad sampler" });

    auto deliver = std::make_unique<DeliverPanel> (crate);

    const auto bg = Colours::background;
    stages.setTabBarDepth (44);
    stages.addTab ("Catalog", bg, catalog.release(), true);
    stages.addTab ("Prep", bg, prep.release(), true);
    stages.addTab ("Listen & Design", bg, design.release(), true);
    stages.addTab ("Deliver", bg, deliver.release(), true);
    stages.setCurrentTabIndex (0);
    addAndMakeVisible (stages);
    addAndMakeVisible (transport);

    setSize (1280, 860);
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    transport.setBounds (area.removeFromBottom (130));
    stages.setBounds (area);
}

} // namespace dss1::ui
