#include "Session.h"
#include "TransportBar.h"

#include <thread>

namespace dss1::ui
{

WavePeaks WavePeaks::from (const io::DecodedAudio& audio, int blockSize)
{
    WavePeaks p;
    p.blockSize = std::max (1, blockSize);
    if (audio.channels.empty())
        return p;
    const auto frames = audio.channels.front().size();
    const auto blocks = (frames + static_cast<std::size_t> (p.blockSize) - 1) / static_cast<std::size_t> (p.blockSize);
    p.low.assign (blocks, 0.0f);
    p.high.assign (blocks, 0.0f);
    const auto scale = 1.0f / static_cast<float> (audio.channels.size());
    for (std::size_t b = 0; b < blocks; ++b)
    {
        float lo = 0, hi = 0;
        const auto from = b * static_cast<std::size_t> (p.blockSize);
        const auto to = std::min (frames, from + static_cast<std::size_t> (p.blockSize));
        for (auto i = from; i < to; ++i)
        {
            float v = 0;
            for (const auto& c : audio.channels)
                v += c[i];
            v *= scale;
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        p.low[b] = lo;
        p.high[b] = hi;
    }
    return p;
}

Session::Session (Crate& c, AudioEngine& a) : crate (c), engine (a)
{
    crate.addChangeListener (this);
}

Session::~Session()
{
    crate.removeChangeListener (this);
}

void Session::load (const StoredTrack& track, bool play)
{
    engine.deck().pause();
    current = track;
    cuePreviewing = false;
    cue = ! track.record.memoryCues.empty() ? track.record.memoryCues.front().frame : 0;
    currentState = State::loading;
    loadProblem.clear();
    decoded.reset();
    detail = {};
    overviewColumns.clear();
    sendChangeMessage();

    const auto myGeneration = ++generation;
    std::thread ([weak = juce::WeakReference<Session> (this), path = track.record.identity.path, play, myGeneration] {
        std::shared_ptr<const io::DecodedAudio> audio;
        WavePeaks peaks;
        std::vector<float> overview;
        juce::String problem;
        try
        {
            auto a = std::make_shared<io::DecodedAudio> (io::decode (path));
            peaks = WavePeaks::from (*a);
            overview = overviewPeaks (a->channels, 1400);
            audio = std::move (a);
        }
        catch (const std::exception& e)
        {
            problem = e.what();
        }
        juce::MessageManager::callAsync ([weak, audio, peaks = std::move (peaks), overview = std::move (overview), problem, play, myGeneration]() mutable {
            auto* s = weak.get();
            if (s == nullptr || myGeneration != s->generation)
                return;  // gone, or another track was chosen meanwhile
            if (! audio)
            {
                s->currentState = State::failed;
                s->loadProblem = problem;
                s->sendChangeMessage();
                return;
            }
            s->decoded = audio;
            s->detail = std::move (peaks);
            s->overviewColumns = std::move (overview);
            s->currentState = State::loaded;
            s->engine.deck().load (audio);
            if (play)
                s->engine.deck().play();
            s->sendChangeMessage();
        });
    }).detach();
}

void Session::cuePressed (bool down)
{
    if (currentState != State::loaded || ! current)
        return;
    auto& deck = engine.deck();
    if (down)
    {
        if (deck.isPlaying() && ! cuePreviewing)
        {
            deck.pause();
            deck.seek (cue);
            sendChangeMessage();
            return;
        }
        const auto here = static_cast<Frame> (std::llround (deck.position()));
        if (std::abs (here - cue) > static_cast<Frame> (0.005 * current->record.sampleRate))
        {
            cue = snap (here);
            deck.seek (cue);
            sendChangeMessage();
            return;
        }
        cuePreviewing = true;
        deck.play();
        return;
    }
    if (cuePreviewing)
    {
        cuePreviewing = false;
        deck.pause();
        deck.seek (cue);
    }
}

void Session::playPressed()
{
    cuePreviewing = false;
    if (engine.deck().isPlaying())
        engine.deck().pause();
    else if (currentState == State::loaded)
        engine.deck().play();
}

void Session::jumpBeats (double beats)
{
    if (currentState != State::loaded || ! current)
        return;
    const auto& r = current->record;
    const auto now = engine.deck().position();
    const auto target = r.grid && ! r.grid->isEmpty() ? r.grid->frameAt (r.grid->beatAt (now) + beats)
                                                       : now + beats * 60.0 / r.info.bpm.value_or (120.0) * r.sampleRate;
    engine.deck().seek (std::clamp<Frame> (static_cast<Frame> (std::llround (target)), 0, std::max<Frame> (0, r.lengthFrames - 1)));
}

juce::String Session::timeText() const
{
    if (! current || current->record.sampleRate <= 0)
        return "-:--.---";
    const auto seconds = engine.deck().position() / current->record.sampleRate;
    const auto whole = static_cast<int> (seconds);
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + "."
           + juce::String (static_cast<int> ((seconds - whole) * 1000)).paddedLeft ('0', 3);
}

juce::String Session::barText() const
{
    if (! current || ! current->record.grid || current->record.grid->isEmpty())
        return "---.-";
    const auto& grid = *current->record.grid;
    const auto pos = engine.deck().position();
    const auto beat = grid.nearestBeat (pos);
    const auto perBar = static_cast<long long> (grid.beatsPerBarAt (pos));
    const auto bar = static_cast<long long> (std::floor (static_cast<double> (beat.index) / static_cast<double> (perBar)));
    const auto inBar = static_cast<long long> (beat.index) - bar * perBar;
    return juce::String (bar + 1).paddedLeft ('0', 3) + "." + juce::String (inBar + 1);
}

std::optional<double> Session::playingBpm() const
{
    if (! current)
        return std::nullopt;
    const auto& r = current->record;
    std::optional<double> base;
    if (r.grid && ! r.grid->isEmpty() && (! r.grid->isConstantTempo() || ! r.info.bpm))
        base = r.grid->bpmAt (engine.deck().position());
    else
        base = r.info.bpm;
    if (! base)
        return std::nullopt;
    return *base * engine.deck().tempo();
}

void Session::update (const TrackRecord& record)
{
    if (! current)
        return;
    current->record = record;
    crate.save (record);
    sendChangeMessage();
}

void Session::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! current)
        return;
    // Take in what the library learnt about this track since it was loaded: analysis results, a first grid, a
    // new path after a relink. Cues and loops are this session's to change, so they're left alone.
    for (const auto& t : crate.tracks())
    {
        if (t.id != current->id)
            continue;
        auto& mine = current->record;
        bool changed = false;
        if (mine.info.analysed && ! t.record.info.analysed)
        {
            // "Analyse Again": the old grid goes, and the new one arrives with the analysis.
            mine.grid.reset();
            changed = true;
        }
        if (t.record.info != mine.info)
        {
            mine.info = t.record.info;
            changed = true;
        }
        if (! mine.grid && t.record.grid)
        {
            mine.grid = t.record.grid;
            changed = true;
        }
        if (t.record.identity.path != mine.identity.path)
        {
            mine.identity.path = t.record.identity.path;
            changed = true;
        }
        if (changed)
            sendChangeMessage();
        return;
    }
}

juce::String Session::title() const
{
    return current ? juce::String::fromUTF8 (current->record.info.title.c_str()) : juce::String();
}

juce::String Session::details() const
{
    if (! current)
        return {};
    const auto& info = current->record.info;
    juce::String d = juce::String::fromUTF8 (info.artist.c_str());
    if (info.bpm)
        d << (d.isEmpty() ? "" : "  |  ") << juce::String (*info.bpm, 1) << " BPM";
    if (! info.camelot.empty())
        d << (d.isEmpty() ? "" : "  |  ") << juce::String (info.camelot) << " " << juce::String (info.key);
    return d;
}

} // namespace dss1::ui
