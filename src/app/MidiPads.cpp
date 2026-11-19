#include "MidiPads.h"

namespace dss1::ui
{

MidiPads::MidiPads()
{
    openAll();
    // Controllers plugged in (or out) later.
    deviceWatch = juce::MidiDeviceListConnection::make ([this] { openAll(); });
}

MidiPads::~MidiPads()
{
    *alive = false;
    for (auto& in : inputs)
        in->stop();
}

void MidiPads::openAll()
{
    const auto available = juce::MidiInput::getAvailableDevices();
    // Close the ones that went away.
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(),
                                  [&] (const auto& in) {
                                      const bool still = std::any_of (available.begin(), available.end(),
                                                                      [&] (const auto& d) { return d.identifier == in->getIdentifier(); });
                                      if (! still)
                                          in->stop();
                                      return ! still;
                                  }),
                  inputs.end());
    // Open the new ones.
    for (const auto& d : available)
    {
        if (std::any_of (inputs.begin(), inputs.end(), [&] (const auto& in) { return in->getIdentifier() == d.identifier; }))
            continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
        }
    }
}

juce::StringArray MidiPads::deviceNames() const
{
    juce::StringArray names;
    for (const auto& in : inputs)
        names.add (in->getName());
    return names;
}

void MidiPads::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // On the MIDI thread: pass a copy to the message thread.
    juce::MessageManager::callAsync ([this, m, alive = std::weak_ptr<bool> (alive)] {
        if (alive.expired())
            return;
        if (onActivity)
            onActivity();
        if (m.isNoteOn() && onNoteOn)
            onNoteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff() && onNoteOff)
            onNoteOff (m.getNoteNumber());
        else if (m.isController() && onControl)
            onControl (m.getControllerNumber(), m.getControllerValue());
        else if ((m.isMidiStart() || m.isMidiContinue()) && onTransport)
            onTransport (true);
        else if (m.isMidiStop() && onTransport)
            onTransport (false);
        else if (m.isMidiMachineControlMessage() && onTransport)
        {
            const auto command = m.getMidiMachineControlCommand();
            if (command == juce::MidiMessage::mmc_play || command == juce::MidiMessage::mmc_deferredplay)
                onTransport (true);
            else if (command == juce::MidiMessage::mmc_stop || command == juce::MidiMessage::mmc_pause)
                onTransport (false);
        }
    });
}

} // namespace dss1::ui
