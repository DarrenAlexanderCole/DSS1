#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dss1::rekordbox
{

// A faithful model of the Rekordbox XML format ("DJ_PLAYLISTS", version 1.0.0): the collection of tracks
// with their beat grids and position marks, and the playlist tree. Values are kept as Rekordbox writes them
// (seconds, 1-based beats, file:// locations); converting to DSS1's own track data is RekordboxMapping's job.

/// <TEMPO>: one beat-grid marker. Rekordbox writes one per tempo change (and often one per bar or beat).
struct Tempo
{
    double inizio = 0;          ///< position in seconds
    double bpm = 0;
    std::string metro = "4/4";  ///< time signature
    int battito = 1;            ///< beat in the bar at this marker, 1 … beats per bar

    bool operator== (const Tempo&) const = default;
};

enum class MarkType
{
    cue = 0,
    fadeIn = 1,
    fadeOut = 2,
    load = 3,
    loop = 4
};

struct Rgb
{
    int red = 0, green = 0, blue = 0;
    bool operator== (const Rgb&) const = default;
};

/// <POSITION_MARK>: a hot cue (num 0–7 = A–H), a memory cue (num -1), or either as a loop (type loop + end).
struct PositionMark
{
    std::string name;
    MarkType type = MarkType::cue;
    double start = 0;                 ///< seconds
    std::optional<double> end;        ///< seconds, loops only
    int num = -1;                     ///< 0–7 = hot cue A–H, -1 = memory cue
    std::optional<Rgb> colour;

    bool isHotCue() const noexcept    { return num >= 0; }

    bool operator== (const PositionMark&) const = default;
};

/// <TRACK> in the collection. The attributes DSS1 understands are named fields; any others are kept in
/// `otherAttributes`, in their original order, so a file can be read and written back without losing them.
struct Track
{
    std::string trackId;
    std::string name;
    std::string artist;
    std::string album;
    std::string genre;
    std::string kind;                 ///< "WAV File", "MP3 File" …
    std::optional<double> totalTime;  ///< seconds (Rekordbox writes whole seconds)
    std::optional<double> averageBpm;
    std::optional<double> sampleRate;
    std::string tonality;             ///< key, in whatever notation Rekordbox was set to
    std::string location;             ///< file://localhost/… URL

    std::vector<std::pair<std::string, std::string>> otherAttributes;

    std::vector<Tempo> tempos;
    std::vector<PositionMark> marks;

    bool operator== (const Track&) const = default;
};

/// <NODE> in the playlist tree: a folder (holding nodes) or a playlist (holding track references).
struct Node
{
    enum class Kind { folder = 0, playlist = 1 };

    Kind kind = Kind::folder;
    std::string name;
    std::vector<Node> children;            ///< folders only
    std::vector<std::string> trackKeys;    ///< playlists only
    int keyType = 0;                       ///< 0 = keys are TrackIDs, 1 = keys are locations

    bool operator== (const Node&) const = default;
};

struct Collection
{
    std::string productName = "DSS1";
    std::string productVersion;
    std::string productCompany;

    std::vector<Track> tracks;
    Node playlists { Node::Kind::folder, "ROOT", {}, {}, 0 };

    bool operator== (const Collection&) const = default;
};

/// Reads a Rekordbox XML document. Throws std::runtime_error with a readable message when the text isn't
/// XML or isn't a Rekordbox collection. Unknown elements are ignored; unknown TRACK attributes are kept.
Collection parse (const std::string& xmlText);

/// Writes a Rekordbox XML document (UTF-8, version 1.0.0) that Rekordbox can import.
std::string write (const Collection& collection);

/// "/Users/dj/My Music/track.mp3" → "file://localhost/Users/dj/My%20Music/track.mp3".
/// Windows paths ("C:\\Music\\a.mp3" or "C:/Music/a.mp3") become "file://localhost/C:/Music/a.mp3".
std::string locationFromPath (const std::string& path);

/// The reverse of locationFromPath. Accepts "file://localhost/…" and "file:///…". Returns an empty string
/// for anything that isn't a file URL.
std::string pathFromLocation (const std::string& location);

} // namespace dss1::rekordbox
