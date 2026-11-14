#include "core/TrackRecord.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace dss1
{

char TrackRecord::hotCueLetter (std::size_t slot)
{
    if (slot >= hotCueSlots)
        throw std::out_of_range ("TrackRecord: hot cue slot out of range");
    return static_cast<char> ('A' + slot);
}

void TrackRecord::addMemoryCue (MemoryCue cue)
{
    const auto it = std::lower_bound (memoryCues.begin(), memoryCues.end(), cue.frame,
                                      [] (const MemoryCue& c, Frame f) { return c.frame < f; });
    if (it != memoryCues.end() && it->frame == cue.frame)
        *it = std::move (cue);
    else
        memoryCues.insert (it, std::move (cue));
}

std::vector<std::string> TrackRecord::problems() const
{
    std::vector<std::string> found;
    const auto inTrack = [this] (Frame f) { return f >= 0 && (lengthFrames <= 0 || f <= lengthFrames); };

    if (sampleRate <= 0)
        found.push_back ("sample rate is missing");
    if (grid && ! grid->isEmpty() && grid->sampleRate() != sampleRate)
        found.push_back ("beat grid sample rate differs from the track's");

    for (std::size_t i = 0; i < hotCues.size(); ++i)
    {
        if (! hotCues[i])
            continue;
        const auto& c = *hotCues[i];
        const auto label = std::string ("hot cue ") + hotCueLetter (i);
        if (! inTrack (c.frame))
            found.push_back (label + " is outside the track");
        if (c.loopEnd && *c.loopEnd <= c.frame)
            found.push_back (label + " loop ends before it starts");
    }

    for (const auto& c : memoryCues)
        if (! inTrack (c.frame))
            found.push_back ("a memory cue is outside the track");
    if (! std::is_sorted (memoryCues.begin(), memoryCues.end(),
                          [] (const MemoryCue& a, const MemoryCue& b) { return a.frame < b.frame; }))
        found.push_back ("memory cues are out of order");

    for (std::size_t i = 0; i < savedLoops.size(); ++i)
    {
        if (! savedLoops[i])
            continue;
        const auto& l = *savedLoops[i];
        const auto label = "saved loop " + std::to_string (i + 1);
        if (l.end <= l.start)
            found.push_back (label + " ends before it starts");
        if (! inTrack (l.start) || ! inTrack (l.end))
            found.push_back (label + " is outside the track");
    }
    return found;
}

// JSON ---------------------------------------------------------------------------------------------

using nlohmann::json;

namespace
{
    std::string colourToString (const CueColour& c)
    {
        char text[8];
        std::snprintf (text, sizeof (text), "#%02X%02X%02X", c.red, c.green, c.blue);
        return text;
    }

    CueColour colourFromString (const std::string& text)
    {
        unsigned r = 0, g = 0, b = 0;
        if (text.size() != 7 || text[0] != '#' || std::sscanf (text.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3)
            throw std::runtime_error ("TrackRecord: bad colour \"" + text + "\"");
        return { static_cast<std::uint8_t> (r), static_cast<std::uint8_t> (g), static_cast<std::uint8_t> (b) };
    }

    void putOptionalExtras (json& j, const std::string& name, const std::optional<CueColour>& colour)
    {
        if (! name.empty())
            j["name"] = name;
        if (colour)
            j["colour"] = colourToString (*colour);
    }

    std::optional<CueColour> readColour (const json& j)
    {
        if (const auto it = j.find ("colour"); it != j.end() && ! it->is_null())
            return colourFromString (it->get<std::string>());
        return std::nullopt;
    }

    json gridToJson (const BeatGrid& grid)
    {
        json anchors = json::array();
        for (const auto& a : grid.anchors())
            anchors.push_back ({ { "frame", a.frame }, { "bpm", a.bpm },
                                 { "beatInBar", a.beatInBar }, { "beatsPerBar", a.beatsPerBar } });
        return { { "sampleRate", grid.sampleRate() }, { "anchors", anchors } };
    }

    BeatGrid gridFromJson (const json& j)
    {
        std::vector<TempoAnchor> anchors;
        for (const auto& a : j.at ("anchors"))
            anchors.push_back ({ a.at ("frame").get<Frame>(), a.at ("bpm").get<double>(),
                                 a.value ("beatInBar", 1), a.value ("beatsPerBar", 4) });
        return BeatGrid (j.at ("sampleRate").get<double>(), std::move (anchors));
    }
}

json toJson (const TrackRecord& t)
{
    json j;
    j["schema"] = TrackRecord::schemaVersion;
    j["identity"] = { { "path", t.identity.path }, { "fingerprint", t.identity.fingerprint } };
    j["sampleRate"] = t.sampleRate;
    j["lengthFrames"] = t.lengthFrames;

    if (t.grid)
        j["grid"] = gridToJson (*t.grid);

    json hot = json::array();
    for (std::size_t i = 0; i < t.hotCues.size(); ++i)
    {
        if (! t.hotCues[i])
            continue;
        const auto& c = *t.hotCues[i];
        json item = { { "slot", std::string (1, TrackRecord::hotCueLetter (i)) }, { "frame", c.frame } };
        if (c.loopEnd)
            item["loopEnd"] = *c.loopEnd;
        putOptionalExtras (item, c.name, c.colour);
        hot.push_back (item);
    }
    j["hotCues"] = hot;

    json memory = json::array();
    for (const auto& c : t.memoryCues)
    {
        json item = { { "frame", c.frame } };
        putOptionalExtras (item, c.name, c.colour);
        memory.push_back (item);
    }
    j["memoryCues"] = memory;

    json loops = json::array();
    for (std::size_t i = 0; i < t.savedLoops.size(); ++i)
    {
        if (! t.savedLoops[i])
            continue;
        const auto& l = *t.savedLoops[i];
        json item = { { "slot", i + 1 }, { "start", l.start }, { "end", l.end } };
        if (l.locked)
            item["locked"] = true;
        putOptionalExtras (item, l.name, l.colour);
        loops.push_back (item);
    }
    j["savedLoops"] = loops;
    return j;
}

TrackRecord trackRecordFromJson (const json& j)
{
    const auto schema = j.at ("schema").get<int>();
    if (schema > TrackRecord::schemaVersion)
        throw std::runtime_error ("TrackRecord: saved by a newer version of DSS1 (schema "
                                  + std::to_string (schema) + ")");

    TrackRecord t;
    if (const auto it = j.find ("identity"); it != j.end())
    {
        t.identity.path = it->value ("path", std::string());
        t.identity.fingerprint = it->value ("fingerprint", std::string());
    }
    t.sampleRate = j.at ("sampleRate").get<double>();
    t.lengthFrames = j.value ("lengthFrames", Frame { 0 });

    if (const auto it = j.find ("grid"); it != j.end() && ! it->is_null())
        t.grid = gridFromJson (*it);

    for (const auto& item : j.value ("hotCues", json::array()))
    {
        const auto slot = item.at ("slot").get<std::string>();
        if (slot.size() != 1 || slot[0] < 'A' || slot[0] >= static_cast<char> ('A' + TrackRecord::hotCueSlots))
            throw std::runtime_error ("TrackRecord: bad hot cue slot \"" + slot + "\"");
        HotCue c;
        c.frame = item.at ("frame").get<Frame>();
        if (const auto e = item.find ("loopEnd"); e != item.end() && ! e->is_null())
            c.loopEnd = e->get<Frame>();
        c.name = item.value ("name", std::string());
        c.colour = readColour (item);
        t.hotCues[static_cast<std::size_t> (slot[0] - 'A')] = c;
    }

    for (const auto& item : j.value ("memoryCues", json::array()))
        t.addMemoryCue ({ item.at ("frame").get<Frame>(), item.value ("name", std::string()), readColour (item) });

    for (const auto& item : j.value ("savedLoops", json::array()))
    {
        const auto slot = item.at ("slot").get<std::size_t>();
        if (slot < 1 || slot > TrackRecord::savedLoopSlots)
            throw std::runtime_error ("TrackRecord: bad saved loop slot " + std::to_string (slot));
        SavedLoop l;
        l.start = item.at ("start").get<Frame>();
        l.end = item.at ("end").get<Frame>();
        l.locked = item.value ("locked", false);
        l.name = item.value ("name", std::string());
        l.colour = readColour (item);
        t.savedLoops[slot - 1] = l;
    }
    return t;
}

} // namespace dss1
