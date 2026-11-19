#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

namespace dss1::ui
{

/// Listens to every connected MIDI controller (an Akai MPD, MPK or MPC, or anything else), including ones
/// plugged in later, and hands notes, controls and transport messages to the message thread.
class MidiPads : private juce::MidiInputCallback
{
public:
    MidiPads();
    ~MidiPads() override;

    std::function<void (int note, float velocity)> onNoteOn;
    std::function<void (int note)> onNoteOff;
    std::function<void (int controller, int value)> onControl;
    /// MIDI Start / Continue (true) and Stop (false), and MMC play / stop.
    std::function<void (bool play)> onTransport;
    /// Something arrived (to blink a light).
    std::function<void()> onActivity;

    /// The controllers listening right now.
    juce::StringArray deviceNames() const;

private:
    void openAll();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::MidiDeviceListConnection deviceWatch;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace dss1::ui
