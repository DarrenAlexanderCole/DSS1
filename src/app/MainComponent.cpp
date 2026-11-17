#include "MainComponent.h"
#include "CatalogPanel.h"
#include "PrepPanel.h"
#include "StagePanel.h"
#include "TestExportPanel.h"
#include "Theme.h"

namespace dss1::ui
{

namespace
{
    /// Tools that check DSS1's exports by hand. Kept out of the way: the MP3 timing question is settled
    /// (docs/ARCHITECTURE.md), but the check stays useful when a new DJ app or file type comes along.
    class ExportCheckLauncher : public juce::Component
    {
    public:
        ExportCheckLauncher()
        {
            button.onClick = [] {
                juce::DialogWindow::LaunchOptions o;
                auto panel = std::make_unique<TestExportPanel>();
                panel->setSize (900, 560);
                o.content.setOwned (panel.release());
                o.dialogTitle = "Export timing check";
                o.dialogBackgroundColour = Colours::background;
                o.escapeKeyTriggersCloseButton = true;
                o.useNativeTitleBar = true;
                o.resizable = true;
                o.launchAsync();
            };
            addAndMakeVisible (button);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Checks that cues land on the same beat in Serato and Rekordbox for a file you choose.",
                              getLocalBounds().withTrimmedTop (40), juce::Justification::topLeft, 2);
        }

        void resized() override { button.setBounds (0, 0, 220, 30); }

    private:
        juce::TextButton button { "Export Timing Check..." };
    };
} // namespace

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

    auto deliver = std::make_unique<StagePanel> (
        "Deliver", "Send prepared tracks to the software you play on.",
        juce::StringArray { "Rekordbox XML export and import",
                            "Serato-tagged copies",
                            "MPC projects and WAV slices" });
    deliver->setShowcase (std::make_unique<ExportCheckLauncher>());

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
