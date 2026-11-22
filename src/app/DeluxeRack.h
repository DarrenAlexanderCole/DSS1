#pragma once

#include "engine/Modular.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace dss1::ui
{

/// The DELUXE key on the effects rack: a backlit amber glass key that breathes while it waits to be pressed,
/// and lights up solid while the modular rack is showing.
class DeluxeKey : public juce::Button, private juce::Timer
{
public:
    explicit DeluxeKey (const juce::String& label = "DELUXE",
                        const juce::String& tip = "DELUXE: flip the effects into a modular rack of remix modules (everything starts turned down)");
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    void timerCallback() override;
    float breath = 0;
};

/// DELUXE: the effects rack flipped into a Eurorack case of remix modules (STUTTER, SLICER, NOISE, WAVETABLE,
/// DRIFT, MULTI-TAP, LADDER and an empty slot), each its own brushed panel, all turned down to start. The modules
/// can be moved along the chain. The first time it opens in a session, the classic bays lift out and fade, and
/// the modules slide in one after another; after that it opens straight away.
///
/// The eighth module, MOD, moves a knob of another module in time with the music. Modules can be moved with
/// their < > keys or carried by the grip at their top right.
class DeluxeRack : public juce::Component, private juce::Timer
{
public:
    explicit DeluxeRack (engine::Modular& sound);

    /// Room at the left of the effects header for the STEP key (the titles start after it).
    static constexpr int stepKeySpace = 196;
    ~DeluxeRack() override;

    /// Shows the rack over the effects unit. classicBays is a picture of the classic bays, and where they were
    /// (in this component's coordinates), so they can lift out of the case; animate plays the intro.
    void open (juce::Image classicBays, juce::Rectangle<int> classicArea, bool animate);
    void close();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    struct Module;

private:
    void timerCallback() override;
    void layoutModules (bool immediately);
    void move (int slot, int by);
    void allToZero();

    // Patches: the chain's order, every module's switches and knobs, and what MOD moves, saved by name in the
    // settings file.
    std::unique_ptr<juce::XmlElement> capture() const;
    void restore (const juce::XmlElement& patch);
    void askToSave();
    void showPatches();
    bool anythingOn() const;
    int patchNumber = 0;
    juce::String patchName { "INIT" };
    // Carrying a module by its grip.
    void grab (Module&, float x);
    void carry (Module&, float x);
    void drop (Module&);
    Module* carried = nullptr;
    float carryOffset = 0;
    float introProgress (double delayMs, double lengthMs) const;

    engine::Modular& sound;
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<int> order;  // module index in each slot
    juce::TextButton savePatch { "SAVE PATCH" }, patches { "PATCHES" }, zero { "ALL TO ZERO" };
    juce::Rectangle<int> header, row, patchWindow;

    juce::Image classic;
    juce::Rectangle<int> classicArea;
    bool introRunning = false;
    double introStartMs = 0;
    bool showedSwap = false, showedSwapBack = false;
};

} // namespace dss1::ui
