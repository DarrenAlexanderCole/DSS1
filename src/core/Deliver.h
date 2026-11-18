#pragma once

#include "core/Id3.h"
#include "core/LibraryStore.h"
#include "core/RekordboxXml.h"
#include "core/SeratoTags.h"

#include <string>
#include <vector>

namespace dss1::deliver
{

using Bytes = id3::Bytes;

// Turning the library into what DJ apps read. Nothing here touches the user's music files: Rekordbox gets an
// XML file that points at them, and Serato gets tagged copies in a folder of their own (decided 2026-11-17).

/// True if the user has prepared anything on this track: a cue, a loop, or a grid.
bool isPrepared (const TrackRecord& record);

/// "WAV File", "MP3 File", "AIFF File", "FLAC File": Rekordbox's Kind column.
std::string rekordboxKind (const std::string& path);

/// A Rekordbox collection of `tracks`, with one playlist holding them all.
rekordbox::Collection rekordboxCollection (const std::vector<StoredTrack>& tracks, const std::string& playlistName);

/// Whether Serato tags can be written into this kind of file yet (WAV, AIFF, MP3; not FLAC).
bool canTagForSerato (const std::string& path);

/// `file` (the bytes of an audio file at `path`) with this record's Serato cues and loops written in, and the
/// beat grid when `options` ask for it. Older "Serato Markers_" data is removed so Serato can't prefer it.
Bytes seratoTaggedCopy (const Bytes& file, const std::string& path, const TrackRecord& record,
                        const serato::ExportOptions& options);

/// File names for the copies, one per source path in order: the source's own name, with " (2)", " (3)" …
/// added when two sources share a name. Comparison ignores case, as the Mac's file system does.
std::vector<std::string> copyNames (const std::vector<std::string>& sourcePaths);

} // namespace dss1::deliver
