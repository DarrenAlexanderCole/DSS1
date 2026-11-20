#include "core/Deliver.h"
#include "core/RekordboxMapping.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace dss1::deliver
{

namespace
{
    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }

    std::string extensionOf (const std::string& path)
    {
        const auto slash = path.find_last_of ("/\\");
        const auto dot = path.find_last_of ('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return {};
        return lower (path.substr (dot));
    }

    std::string fileNameOf (const std::string& path)
    {
        const auto slash = path.find_last_of ("/\\");
        return slash == std::string::npos ? path : path.substr (slash + 1);
    }
}

bool isPrepared (const TrackRecord& r)
{
    if (r.grid && ! r.grid->isEmpty())
        return true;
    if (! r.memoryCues.empty())
        return true;
    for (const auto& c : r.hotCues)
        if (c)
            return true;
    for (const auto& l : r.savedLoops)
        if (l)
            return true;
    return false;
}

std::string rekordboxKind (const std::string& path)
{
    const auto e = extensionOf (path);
    if (e == ".mp3")
        return "MP3 File";
    if (e == ".aif" || e == ".aiff")
        return "AIFF File";
    if (e == ".flac")
        return "FLAC File";
    return "WAV File";
}

rekordbox::Collection rekordboxCollection (const std::vector<StoredTrack>& tracks, const std::string& playlistName)
{
    rekordbox::Collection c;
    c.productName = "DSS1";
    c.productCompany = "DSS1";

    rekordbox::Node playlist;
    playlist.kind = rekordbox::Node::Kind::playlist;
    playlist.name = playlistName;

    for (const auto& t : tracks)
    {
        const auto& r = t.record;
        rekordbox::TrackDetails details;
        details.trackId = std::to_string (t.id);
        details.name = r.info.title;
        details.artist = r.info.artist;
        details.album = r.info.album;
        details.genre = r.info.genre;
        details.kind = rekordboxKind (r.identity.path);
        details.tonality = r.info.camelot;
        details.averageBpm = r.info.bpm;
        c.tracks.push_back (rekordbox::exportTrack (r, details));
        if (! r.info.notes.empty())
            c.tracks.back().otherAttributes.emplace_back ("Comments", r.info.notes);
        playlist.trackKeys.push_back (details.trackId);
    }
    c.playlists.children.push_back (std::move (playlist));
    return c;
}

bool canTagForSerato (const std::string& path)
{
    return id3::containerForExtension (extensionOf (path)).has_value();
}

Bytes seratoTaggedCopy (const Bytes& file, const std::string& path, const TrackRecord& record, const serato::ExportOptions& options)
{
    const auto container = id3::containerForExtension (extensionOf (path));
    if (! container)
        throw std::runtime_error ("DSS1 can't write Serato cues into " + extensionOf (path) + " files yet");

    const auto exported = serato::exportTrack (record, options);
    std::vector<id3::GeobFrame> frames { { serato::markers2Description, serato::wrapMarkersForGeob (serato::encodeMarkers (exported.markers)) } };
    std::vector<std::string> remove { serato::legacyMarkersDescription };
    // Copies are always made from the original, so nothing stale from an earlier export can be in it; a grid
    // Serato itself wrote into the original is left alone unless DSS1 writes its own.
    if (exported.grid)
        frames.push_back ({ serato::beatGridDescription, serato::encodeGrid (*exported.grid) });
    return id3::writeGeobFrames (file, *container, frames, remove);
}

std::vector<std::string> copyNames (const std::vector<std::string>& sourcePaths)
{
    std::vector<std::string> names;
    std::map<std::string, int> used;
    for (const auto& path : sourcePaths)
    {
        const auto name = fileNameOf (path);
        auto candidate = name;
        for (int n = 2; used.count (lower (candidate)) != 0; ++n)
        {
            const auto dot = name.find_last_of ('.');
            const auto stem = dot == std::string::npos ? name : name.substr (0, dot);
            const auto ext = dot == std::string::npos ? std::string() : name.substr (dot);
            candidate = stem + " (" + std::to_string (n) + ")" + ext;
        }
        used[lower (candidate)] = 1;
        names.push_back (candidate);
    }
    return names;
}

} // namespace dss1::deliver
