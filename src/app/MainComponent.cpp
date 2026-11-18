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

    // Keys go to the screens, not to whichever button was last clicked: otherwise Space would press that button
    // again instead of playing. Only text fields and the track list take keyboard focus.
    std::function<void (juce::Component&)> noFocus = [&] (juce::Component& c) {
        if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr)
        {
            c.setWantsKeyboardFocus (false);
            c.setMouseClickGrabsKeyboardFocus (false);
        }
        for (auto* child : c.getChildren())
            noFocus (*child);
    };
    noFocus (*this);
    setWantsKeyboardFocus (true);

    setSize (1440, 980);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Space or P plays and pauses on every screen (unless typing in a text field, which keeps the key).
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (key == juce::KeyPress::spaceKey || (c == 'p' && ! key.getModifiers().isAnyModifierKeyDown()))
    {
        session.playPressed();
        return true;
    }
    return false;
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
    // Keyboard focus to the screen (Prep's keys need it), or here so Space and P always reach us.
    auto& shown = *stages[static_cast<std::size_t> (currentStage)];
    if (shown.getWantsKeyboardFocus() && shown.isShowing())
        shown.grabKeyboardFocus();
    else if (isShowing())
        grabKeyboardFocus();
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
