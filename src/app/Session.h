#pragma once

#include "AudioEngine.h"
#include "Crate.h"

#include "io/AudioDecoder.h"

#include <juce_events/juce_events.h>

#include <memory>
#include <functional>
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

    // --- Transport, shared by every stage -------------------------------------------------------------------

    /// CDJ-style CUE. Paused somewhere new: sets the cue point there. Paused on it: plays while held. Playing:
    /// back to the cue point, stopped. Call with true on press and false on release.
    void cuePressed (bool down);
    void playPressed();
    /// Moves by `beats` on the grid, keeping the place within the beat (or by tempo without a grid).
    void jumpBeats (double beats);
    int jumpSize = 16;  ///< beats per jump
    /// Where a new cue point lands (Prep sets this to snap to the grid when snapping is on).
    std::function<Frame (Frame)> snap = [] (Frame f) { return f; };
    Frame cuePoint() const noexcept { return cue; }
    bool isCuePreviewing() const noexcept { return cuePreviewing; }

    /// "1:23.456" and "012.3" (bar.beat; empty without a grid) for the playhead.
    juce::String timeText() const;
    juce::String barText() const;
    /// The track's BPM at the deck's current tempo, if known.
    std::optional<double> playingBpm() const;

    float outputLevel (int channel) const noexcept { return engine.level (channel); }
    engine::Effects& effects() noexcept { return engine.effects(); }
    engine::Modular& modular() noexcept { return engine.modular(); }
    engine::GraphicEq& graphicEq() noexcept { return engine.graphicEq(); }
    /// Tells the DELUXE rack the tempo and where the beats are in the playing track (call often).
    void syncModular();
    AudioEngine& output() noexcept { return engine; }

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
    Frame cue = 0;
    bool cuePreviewing = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Session)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace dss1::ui
