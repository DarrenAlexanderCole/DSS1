#include "core/SeratoTags.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dss1::serato
{

namespace
{
    // --- Big-endian bytes ---------------------------------------------------------------------------------

    void putU32 (Bytes& out, std::uint32_t v)
    {
        out.push_back (static_cast<std::uint8_t> (v >> 24));
        out.push_back (static_cast<std::uint8_t> ((v >> 16) & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFF));
        out.push_back (static_cast<std::uint8_t> (v & 0xFF));
    }

    void putF32 (Bytes& out, float f)
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &f, sizeof bits);
        putU32 (out, bits);
    }

    void putString (Bytes& out, const std::string& s)
    {
        out.insert (out.end(), s.begin(), s.end());
        out.push_back (0);
    }

    void putColour (Bytes& out, const CueColour& c)
    {
        out.push_back (c.red);
        out.push_back (c.green);
        out.push_back (c.blue);
    }

    /// Reads from a byte range and throws if it runs out.
    struct Reader
    {
        const Bytes& bytes;
        std::size_t pos = 0;
        std::size_t end = 0;

        Reader (const Bytes& b, std::size_t start, std::size_t stop) : bytes (b), pos (start), end (stop) {}

        void need (std::size_t n) const
        {
            if (pos + n > end)
                throw std::runtime_error ("Serato tag is cut short");
        }

        std::uint8_t u8()
        {
            need (1);
            return bytes[pos++];
        }

        std::uint32_t u32()
        {
            need (4);
            const auto v = (std::uint32_t (bytes[pos]) << 24) | (std::uint32_t (bytes[pos + 1]) << 16)
                         | (std::uint32_t (bytes[pos + 2]) << 8) | std::uint32_t (bytes[pos + 3]);
            pos += 4;
            return v;
        }

        float f32()
        {
            const auto bits = u32();
            float f = 0;
            std::memcpy (&f, &bits, sizeof f);
            return f;
        }

        CueColour colour()
        {
            need (3);
            CueColour c { bytes[pos], bytes[pos + 1], bytes[pos + 2] };
            pos += 3;
            return c;
        }

        std::string string()
        {
            const auto start = pos;
            while (pos < end && bytes[pos] != 0)
                ++pos;
            if (pos >= end)
                throw std::runtime_error ("Serato tag has an unterminated name");
            std::string s (bytes.begin() + static_cast<std::ptrdiff_t> (start), bytes.begin() + static_cast<std::ptrdiff_t> (pos));
            ++pos;
            return s;
        }
    };

    // --- Base64 -----------------------------------------------------------------------------------------

    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string base64 (const Bytes& in)
    {
        std::string out;
        out.reserve ((in.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < in.size(); i += 3)
        {
            const auto n = std::min<std::size_t> (3, in.size() - i);
            std::uint32_t v = std::uint32_t (in[i]) << 16;
            if (n > 1) v |= std::uint32_t (in[i + 1]) << 8;
            if (n > 2) v |= std::uint32_t (in[i + 2]);
            out += alphabet[(v >> 18) & 63];
            out += alphabet[(v >> 12) & 63];
            out += n > 1 ? alphabet[(v >> 6) & 63] : '=';
            out += n > 2 ? alphabet[v & 63] : '=';
        }
        return out;
    }

    Bytes unbase64 (std::string text)
    {
        // Serato writes 'A' where padding would go and sometimes leaves the length short of a multiple of 4.
        text.erase (std::remove_if (text.begin(), text.end(), [] (char c) { return c == '\n' || c == '\r' || c == ' '; }), text.end());
        while (text.size() % 4 != 0)
            text += (text.size() % 4 == 1) ? 'A' : '=';

        const auto value = [] (char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        Bytes out;
        for (std::size_t i = 0; i < text.size(); i += 4)
        {
            std::uint32_t v = 0;
            int chars = 0;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const auto c = text[i + k];
                if (c == '=')
                    break;
                const auto d = value (c);
                if (d < 0)
                    throw std::runtime_error ("Serato tag has invalid base64");
                v |= std::uint32_t (d) << (18 - 6 * static_cast<int> (k));
                ++chars;
            }
            if (chars >= 2) out.push_back (static_cast<std::uint8_t> ((v >> 16) & 0xFF));
            if (chars >= 3) out.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFF));
            if (chars >= 4) out.push_back (static_cast<std::uint8_t> (v & 0xFF));
        }
        return out;
    }

    void entry (Bytes& out, const char* name, const Bytes& body)
    {
        putString (out, name);
        putU32 (out, static_cast<std::uint32_t> (body.size()));
        out.insert (out.end(), body.begin(), body.end());
    }

    std::optional<std::uint32_t> toMs (Frame frame, double sampleRate)
    {
        const auto ms = std::llround (static_cast<double> (frame) * 1000.0 / sampleRate);
        if (ms < 0 || ms > static_cast<long long> (UINT32_MAX))
            return std::nullopt;
        return static_cast<std::uint32_t> (ms);
    }

    Frame toFrame (std::uint32_t ms, double sampleRate)
    {
        return static_cast<Frame> (std::llround (static_cast<double> (ms) * sampleRate / 1000.0));
    }

    std::string plural (std::size_t n, const char* one, const char* many)
    {
        return std::to_string (n) + " " + (n == 1 ? one : many);
    }

    /// Builds Serato's grid; sets `approximated` when an anchor isn't a whole number of beats from the one
    /// before (Serato can only count whole beats between markers).
    std::optional<Grid> toSeratoGrid (const BeatGrid& grid, bool& approximated)
    {
        approximated = false;
        if (grid.isEmpty())
            return std::nullopt;

        // Serato counts bars from its first marker, so start on the first downbeat inside the track.
        auto beat = static_cast<std::int64_t> (std::ceil (grid.beatAt (0.0) - 1.0e-9));
        for (int i = 0; i < 32 && grid.beatInBar (beat) != 1; ++i)
            ++beat;

        std::vector<double> positions { grid.frameAt (static_cast<double> (beat)) };
        for (std::size_t i = 1; i < grid.anchors().size(); ++i)
        {
            const auto f = static_cast<double> (grid.anchors()[i].frame);
            if (f > positions.back() + 0.5)
                positions.push_back (f);
        }

        const auto rate = grid.sampleRate();
        Grid out;
        for (std::size_t k = 0; k + 1 < positions.size(); ++k)
        {
            const auto exact = grid.beatAt (positions[k + 1]) - grid.beatAt (positions[k]);
            const auto whole = std::max<long long> (1, std::llround (exact));
            const auto framesOff = std::abs (exact - static_cast<double> (whole)) * rate * 60.0 / grid.bpmAt (positions[k]);
            if (framesOff > rate * 0.001)
                approximated = true;
            out.markers.push_back ({ static_cast<float> (positions[k] / rate), static_cast<std::uint32_t> (whole) });
        }
        out.lastPositionSeconds = static_cast<float> (positions.back() / rate);
        out.lastBpm = static_cast<float> (grid.bpmAt (positions.back()));
        return out;
    }
} // namespace

CueColour defaultCueColour (int index)
{
    static constexpr std::array<CueColour, 8> colours { {
        { 0xCC, 0x00, 0x00 }, { 0xCC, 0x44, 0x00 }, { 0xCC, 0x88, 0x00 }, { 0xCC, 0xCC, 0x00 },
        { 0x00, 0xCC, 0x00 }, { 0x00, 0xCC, 0xCC }, { 0x00, 0x00, 0xCC }, { 0xCC, 0x00, 0xCC } } };
    return colours[static_cast<std::size_t> (((index % 8) + 8) % 8)];
}

Bytes encodeMarkers (const Markers& m)
{
    Bytes out { 0x01, 0x01 };

    Bytes colour { 0x00 };
    putColour (colour, m.trackColour);
    entry (out, "COLOR", colour);

    auto cues = m.cues;
    std::stable_sort (cues.begin(), cues.end(), [] (const Cue& a, const Cue& b) { return a.index < b.index; });
    for (const auto& c : cues)
    {
        Bytes b { 0x00, static_cast<std::uint8_t> (c.index) };
        putU32 (b, c.positionMs);
        b.push_back (0x00);
        putColour (b, c.colour);
        b.push_back (0x00);
        b.push_back (0x00);
        putString (b, c.name);
        entry (out, "CUE", b);
    }

    auto loops = m.loops;
    std::stable_sort (loops.begin(), loops.end(), [] (const Loop& a, const Loop& b) { return a.index < b.index; });
    for (const auto& l : loops)
    {
        Bytes b { 0x00, static_cast<std::uint8_t> (l.index) };
        putU32 (b, l.startMs);
        putU32 (b, l.endMs);
        b.insert (b.end(), { 0xFF, 0xFF, 0xFF, 0xFF });
        b.push_back (0x00);
        putColour (b, l.colour);
        b.push_back (0x00);
        b.push_back (l.locked ? 0x01 : 0x00);
        putString (b, l.name);
        entry (out, "LOOP", b);
    }

    entry (out, "BPMLOCK", Bytes { static_cast<std::uint8_t> (m.bpmLocked ? 1 : 0) });
    out.push_back (0x00);
    return out;
}

Markers decodeMarkers (const Bytes& payload)
{
    if (payload.size() < 2 || payload[0] != 0x01 || payload[1] != 0x01)
        throw std::runtime_error ("Not a Serato Markers2 payload (unknown version)");

    Markers m;
    Reader r (payload, 2, payload.size());
    while (r.pos < r.end && payload[r.pos] != 0x00)
    {
        const auto name = r.string();
        const auto length = r.u32();
        r.need (length);
        Reader body (payload, r.pos, r.pos + length);
        r.pos += length;

        if (name == "COLOR")
        {
            body.u8();
            m.trackColour = body.colour();
        }
        else if (name == "CUE")
        {
            Cue c;
            body.u8();
            c.index = body.u8();
            c.positionMs = body.u32();
            body.u8();
            c.colour = body.colour();
            body.u8();
            body.u8();
            c.name = body.string();
            m.cues.push_back (c);
        }
        else if (name == "LOOP")
        {
            Loop l;
            body.u8();
            l.index = body.u8();
            l.startMs = body.u32();
            l.endMs = body.u32();
            body.u32();  // FF FF FF FF
            body.u8();
            l.colour = body.colour();
            body.u8();
            l.locked = body.u8() != 0;
            l.name = body.string();
            m.loops.push_back (l);
        }
        else if (name == "BPMLOCK")
        {
            m.bpmLocked = body.u8() != 0;
        }
        // Other entries (e.g. FLIP) are skipped.
    }
    return m;
}

Bytes wrapMarkersForGeob (const Bytes& payload)
{
    auto text = base64 (payload);
    std::replace (text.begin(), text.end(), '=', 'A');

    Bytes out { 0x01, 0x01 };
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (i > 0 && i % 72 == 0)
            out.push_back ('\n');
        out.push_back (static_cast<std::uint8_t> (text[i]));
    }
    if (out.size() < 470)
        out.resize (470, 0x00);
    else
        out.push_back (0x00);
    return out;
}

Bytes unwrapMarkersFromGeob (const Bytes& content)
{
    if (content.size() < 2 || content[0] != 0x01 || content[1] != 0x01)
        throw std::runtime_error ("Not a Serato Markers2 frame (unknown version)");
    const auto end = std::find (content.begin() + 2, content.end(), std::uint8_t { 0 });
    return unbase64 (std::string (content.begin() + 2, end));
}

Bytes encodeGrid (const Grid& g)
{
    Bytes out { 0x01, 0x00 };
    putU32 (out, static_cast<std::uint32_t> (g.markers.size() + 1));
    for (const auto& marker : g.markers)
    {
        putF32 (out, marker.positionSeconds);
        putU32 (out, marker.beatsToNext);
    }
    putF32 (out, g.lastPositionSeconds);
    putF32 (out, g.lastBpm);
    out.push_back (0x00);
    return out;
}

Grid decodeGrid (const Bytes& content)
{
    if (content.size() < 6 || content[0] != 0x01 || content[1] != 0x00)
        throw std::runtime_error ("Not a Serato BeatGrid frame (unknown version)");
    Reader r (content, 2, content.size());
    const auto count = r.u32();
    if (count == 0)
        throw std::runtime_error ("Serato BeatGrid has no markers");
    r.need (std::size_t (count) * 8);

    Grid g;
    for (std::uint32_t i = 0; i + 1 < count; ++i)
    {
        Grid::Marker marker;
        marker.positionSeconds = r.f32();
        marker.beatsToNext = r.u32();
        g.markers.push_back (marker);
    }
    g.lastPositionSeconds = r.f32();
    g.lastBpm = r.f32();
    return g;
}

std::optional<Grid> gridFromBeatGrid (const BeatGrid& grid)
{
    bool approximated = false;
    return toSeratoGrid (grid, approximated);
}

std::optional<BeatGrid> beatGridFromGrid (const Grid& g, double sampleRate)
{
    if (sampleRate <= 0)
        return std::nullopt;

    std::vector<TempoAnchor> anchors;
    std::int64_t beatsSoFar = 0;
    for (std::size_t k = 0; k < g.markers.size(); ++k)
    {
        const auto here = static_cast<double> (g.markers[k].positionSeconds);
        const auto next = static_cast<double> (k + 1 < g.markers.size() ? g.markers[k + 1].positionSeconds : g.lastPositionSeconds);
        const auto beats = g.markers[k].beatsToNext;
        if (beats == 0 || next <= here)
            return std::nullopt;
        TempoAnchor a;
        a.frame = static_cast<Frame> (std::llround (here * sampleRate));
        a.bpm = static_cast<double> (beats) * 60.0 / (next - here);
        a.beatInBar = static_cast<int> (beatsSoFar % 4) + 1;
        anchors.push_back (a);
        beatsSoFar += beats;
    }

    TempoAnchor last;
    last.frame = static_cast<Frame> (std::llround (static_cast<double> (g.lastPositionSeconds) * sampleRate));
    last.bpm = static_cast<double> (g.lastBpm);
    last.beatInBar = static_cast<int> (beatsSoFar % 4) + 1;
    anchors.push_back (last);

    try
    {
        return BeatGrid (sampleRate, std::move (anchors));
    }
    catch (const std::invalid_argument&)
    {
        return std::nullopt;  // tempos out of range or markers out of order
    }
}

Export exportTrack (const TrackRecord& r)
{
    Export out;
    const auto rate = r.sampleRate;
    if (rate <= 0)
    {
        out.notes.push_back ("Nothing exported: the track has no sample rate yet");
        return out;
    }

    std::size_t hotLoops = 0, outOfRange = 0;
    for (std::size_t i = 0; i < r.hotCues.size(); ++i)
    {
        if (! r.hotCues[i])
            continue;
        const auto& c = *r.hotCues[i];
        const auto ms = toMs (c.frame, rate);
        if (! ms)
        {
            ++outOfRange;
            continue;
        }
        if (c.loopEnd)
            ++hotLoops;
        const auto index = static_cast<int> (i);
        out.markers.cues.push_back ({ index, *ms, c.colour.value_or (defaultCueColour (index)), c.name });
    }

    for (std::size_t i = 0; i < r.savedLoops.size(); ++i)
    {
        if (! r.savedLoops[i])
            continue;
        const auto& l = *r.savedLoops[i];
        const auto start = toMs (l.start, rate), end = toMs (l.end, rate);
        if (! start || ! end)
        {
            ++outOfRange;
            continue;
        }
        Loop loop;
        loop.index = static_cast<int> (i);
        loop.startMs = *start;
        loop.endMs = *end;
        if (l.colour)
            loop.colour = *l.colour;
        loop.locked = l.locked;
        loop.name = l.name;
        out.markers.loops.push_back (loop);
    }

    if (r.grid)
    {
        bool approximated = false;
        out.grid = toSeratoGrid (*r.grid, approximated);
        if (approximated)
            out.notes.push_back ("The beat grid was rounded to whole beats between tempo changes, which is all Serato can store");
    }

    if (hotLoops > 0)
        out.notes.push_back (plural (hotLoops, "hot loop goes", "hot loops go") + " to Serato as a plain cue: Serato's hot cues can't loop");
    if (! r.memoryCues.empty())
        out.notes.push_back (plural (r.memoryCues.size(), "memory cue isn't", "memory cues aren't") + " exported: Serato has no memory cues");
    if (outOfRange > 0)
        out.notes.push_back (plural (outOfRange, "cue or loop was", "cues or loops were") + " skipped because it sits before the start of the track");
    return out;
}

void importInto (TrackRecord& record, const Markers& m, const std::optional<Grid>& grid)
{
    const auto rate = record.sampleRate;
    if (rate <= 0)
        return;

    record.hotCues = {};
    for (const auto& c : m.cues)
        if (c.index >= 0 && c.index < static_cast<int> (TrackRecord::hotCueSlots))
            record.hotCues[static_cast<std::size_t> (c.index)] = HotCue { toFrame (c.positionMs, rate), std::nullopt, c.name, c.colour };

    record.savedLoops = {};
    for (const auto& l : m.loops)
        if (l.index >= 0 && l.index < static_cast<int> (TrackRecord::savedLoopSlots) && l.endMs > l.startMs)
            record.savedLoops[static_cast<std::size_t> (l.index)] =
                SavedLoop { toFrame (l.startMs, rate), toFrame (l.endMs, rate), l.name, l.colour, l.locked };

    if (grid)
        if (auto g = beatGridFromGrid (*grid, rate))
            record.grid = std::move (g);
}

} // namespace dss1::serato
