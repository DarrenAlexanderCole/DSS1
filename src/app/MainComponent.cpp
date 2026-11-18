#include "MainComponent.h"
#include "CatalogPanel.h"
#include "PrepPanel.h"
#include "DesignPanel.h"
#include "DeliverPanel.h"
#include "Theme.h"

namespace dss1::ui
{


MainComponent::MainComponent()
{
    setLookAndFeel (&theme);
    juce::LookAndFeel::setDefaultLookAndFeel (&theme);

    auto catalog = std::make_unique<CatalogPanel> (session);
    auto prep = std::make_unique<PrepPanel> (session);
    auto design = std::make_unique<DesignPanel> (session, crate);
    auto deliver = std::make_unique<DeliverPanel> (crate);
    catalog->onExportSelected = [this, d = deliver.get()] {
        d->useSelection();
        showStage (3);
    };

    stages.push_back (std::move (catalog));
    stages.push_back (std::move (prep));
    stages.push_back (std::move (design));
    stages.push_back (std::move (deliver));
    for (auto& s : stages)
        addChildComponent (*s);

    header.onStage = [this] (int i) { showStage (i); };
    addAndMakeVisible (header);
    addAndMakeVisible (transport);
    showStage (0);

    setSize (1440, 980);
}

MainComponent::~MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::showStage (int index)
{
    currentStage = juce::jlimit (0, static_cast<int> (stages.size()) - 1, index);
    header.setStage (currentStage);
    for (std::size_t i = 0; i < stages.size(); ++i)
        stages[i]->setVisible (static_cast<int> (i) == currentStage);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (Header::preferredHeight));
    transport.setBounds (area.removeFromBottom (120));
    for (auto& s : stages)
        s->setBounds (area);
}

} // namespace dss1::ui
