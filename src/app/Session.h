#pragma once

#include "AudioEngine.h"
#include "Crate.h"

#include "io/AudioDecoder.h"

#include <juce_events/juce_events.h>

#include <memory>
#include <optional>

namespace dss1::ui
{

/// The waveform at a fine resolution: lowest and highest sample of each block of frames (channels averaged).
struct WavePeaks
{
    int blockSize = 64;
    std::vector<float> low, high;

    bool empty() const noexcept { return high.empty(); }
    static WavePeaks from (const io::DecodedAudio& audio, int blockSize = 64);
};

/// The track on the deck, shared by every stage: Catalog loads it, Prep edits it, the transport bar plays it.
/// Edits go through `update`, which saves them to the library straight away. Message thread only.
class Session : public juce::ChangeBroadcaster, private juce::ChangeListener
{
public:
    enum class State { empty, loading, loaded, failed };

    Session (Crate& crate, AudioEngine& audio);
    ~Session() override;

    /// Decodes the track in the background, then puts it on the deck (and plays it if `play`).
    void load (const StoredTrack& track, bool play);

    State state() const noexcept { return currentState; }
    const juce::String& problem() const noexcept { return loadProblem; }
    /// The track loading or loaded; nullptr when empty.
    const StoredTrack* track() const noexcept { return current ? &*current : nullptr; }
    const TrackRecord* record() const noexcept { return current ? &current->record : nullptr; }
    std::shared_ptr<const io::DecodedAudio> audio() const noexcept { return decoded; }
    const WavePeaks& peaks() const noexcept { return detail; }
    const std::vector<float>& overview() const noexcept { return overviewColumns; }

    /// Replaces the loaded track's record (cues, loops, grid …) and saves it.
    void update (const TrackRecord& record);

    engine::Deck& deck() noexcept { return engine.deck(); }
    Crate& library() noexcept { return crate; }
    /// Empty when the audio output started fine.
    const juce::String& audioProblem() const noexcept { return engine.problem(); }

    /// "Artist  |  128.0 BPM  |  8A F minor" for the loaded track.
    juce::String details() const;
    juce::String title() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Crate& crate;
    AudioEngine& engine;
    State currentState = State::empty;
    juce::String loadProblem;
    std::optional<StoredTrack> current;
    std::shared_ptr<const io::DecodedAudio> decoded;
    WavePeaks detail;
    std::vector<float> overviewColumns;
    int generation = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Session)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace dss1::ui
