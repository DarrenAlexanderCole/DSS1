# DSS1 V2 architecture

DSS1 V2 is a new DJ prep app, written in C++20 on JUCE 9 and built with CMake. macOS ships first; anything
platform-specific sits behind small interfaces so a Windows build can follow without a rewrite.

## Stages

| Stage | Purpose |
|---|---|
| Catalog | Import (files, folders, zips), crate browsing, analysis (BPM, key, loudness, downbeats), folder watching |
| Prep | Waveform editor with Grid, Cues and Loops lanes; a target switch (Rekordbox / Serato / Both) that flags what won't export |
| Listen & Design | Listening deck with real-time effects, recorder, XY performance pad, pad sampler |
| Deliver | Rekordbox XML export/import, Serato-tagged copies, MPC projects, WAV slices |

Rules that keep them clean:

- DJ cues (Prep) and sampler pads are separate data, linked only by explicit copy actions.
- Saved loops are one list per track, used everywhere.
- A user's main platform is a preference for defaults and labels; it never hides data.

## Code modules

| Module | Contents | Depends on | Status |
|---|---|---|---|
| `core` | Track record, beat-grid maths, library store, Rekordbox XML, Serato tags, MPC projects, track matching | C++20 standard library, nlohmann/json, pugixml, SQLite | Track record, beat grid, library database, track matching, Rekordbox XML, Serato tags done |
| `analysis` | Tempo, key, loudness, downbeats, waveform peaks | `core` | Phase 3 |
| `io` | Decoding, import, folder watching | `core`, dr_wav, dr_flac, minimp3 | Decoding done |
| `engine` | One audio device, decks, key lock (Signalsmith Stretch), effects, recorder | `io`, JUCE audio | Phase 3 |
| `midi` | Controllers, MIDI learn, presets | `engine` | Phase 4 |
| `app` | The four stages and the visual style | everything | Shell in place |

`core` deliberately has no JUCE or OS dependency: it is the part that must be exactly right (cue positions,
grids, file formats), and keeping it plain C++ makes it fast to test on any machine.

## Track data

One `TrackRecord` per prepared track (`src/core/TrackRecord.h`):

- **Positions are frames** at the file's own sample rate. Seconds are only worked out at the edges
  (display, Rekordbox, Serato), so nothing drifts through repeated conversions.
- **Identity** is an audio fingerprint plus the last known path, so moved or renamed files keep their data.
- **Beat grid** (`src/core/BeatGrid.h`): one or more tempo anchors. Each anchor starts a stretch of constant tempo
  that runs to the next one; the first also runs backwards to the start of the track. Beats are numbered from the
  first anchor and the bar count carries through tempo changes, so constant and variable grids are the same type.
- **Hot cues A–H** (optionally hot loops), **memory cues** (any number, in order) and **saved loops** (8 slots).
- Stored as JSON with a `schema` number. A record from a newer DSS1 is refused rather than misread.

## Library database

`src/core/LibraryStore.h`: one SQLite file (the copy that ships with macOS) holding each track's record as
JSON, found by path or audio fingerprint. A moved file keeps its data through `relink`. The file carries a
schema number (`PRAGMA user_version`): an older file is upgraded in place, a newer one is refused rather than
risk damage. Catalog columns (title, artist, BPM, key) arrive with Phase 3 as schema 2.

## Track matching

`src/core/TrackMatcher.h` finds which library track an outside reference (a Rekordbox XML entry) means. It
tries, in order: the exact path; the same path ignoring case and accents (composed or decomposed, as macOS
file names often differ from other apps); the same file name elsewhere with the same length; the same name with
another extension (an MP3 that was converted to WAV) with the same length. Two equally good candidates are
reported as ambiguous, never guessed.

## Rekordbox XML

`src/core/RekordboxXml.h` reads and writes the format faithfully (tracks, `TEMPO` grid markers, `POSITION_MARK`s,
the playlist tree). TRACK attributes DSS1 doesn't use are kept, so a collection can be read and written back
without losing anything. `src/core/RekordboxMapping.h` converts to and from DSS1 track data:

| Rekordbox | DSS1 |
|---|---|
| `POSITION_MARK` Num 0–7, Type 0 | Hot cue A–H |
| `POSITION_MARK` Num 0–7, Type 4 (with End) | Hot loop in A–H |
| `POSITION_MARK` Num -1, Type 0 | Memory cue |
| `POSITION_MARK` Num -1, Type 4 | Saved loop (slots filled in track order; extras reported) |
| `POSITION_MARK` Type 1–3 (fades, load) | Not used; reported on import |
| `TEMPO` (Inizio, Bpm, Metro, Battito) | Beat-grid anchor; markers that repeat the grid are merged away |

Rekordbox stores positions to the millisecond, so a position that goes out and comes back lands within half a
millisecond. Positions are converted with the sample rate of the actual audio file, never the XML's hint.

## Serato tags

Serato keeps cues, loops and the grid inside the audio file as ID3 `GEOB` frames. `src/core/SeratoTags.h`
builds and reads the "Serato Markers2" payload (cues, saved loops, track colour, BPM lock; Serato's own base64
with a line feed every 72 characters, padded to 470 bytes) and the "Serato BeatGrid" payload, following the
community-documented layout ([Holzhaus/serato-tags](https://github.com/Holzhaus/serato-tags)).
`src/core/Id3.h` puts GEOB frames into WAV (`id3 ` chunk), AIFF (`ID3 ` chunk) and MP3 (tag at the front),
keeping every other frame and chunk.

| DSS1 | Serato |
|---|---|
| Hot cue A–H | Cue 1–8 (DSS1 colour, or Serato's default colour for that slot) |
| Hot loop in A–H | Plain cue (Serato's hot cues can't loop), reported |
| Memory cues | Not exported (Serato has none), reported |
| Saved loops 1–8 | Loops 1–8, with colour and lock |
| Beat grid | Off by default (many Serato DJs don't use grids); when switched on, BeatGrid markers from the first downbeat in the track; tempo changes rounded to whole beats, reported if that moves anything by more than 1 ms |

FLAC and M4A keep Serato data differently (Vorbis comments / MP4 atoms); for now those are exported as WAV.

## Decoding

Cue positions only line up across Rekordbox, Serato and other computers if every file decodes to the same
samples, so DSS1 bundles its own decoders rather than using the operating system's: `src/io/AudioDecoder.h`
reads WAV and AIFF (dr_wav), FLAC (dr_flac) and MP3 (minimp3), all public domain, pinned to commits.

MP3 encoders add silence at the start (encoder delay) and end (padding). When the file says how much (a
LAME/Xing header, which almost every MP3 has), DSS1 removes it, so frame 0 is the first real sample and the
file is exactly as long as the music that was encoded. The removed amount is kept (`encoderDelayFrames`) in
case a DJ app turns out to count from the undelayed start (still to be checked against Serato and Rekordbox
with real MP3s once DSS1 can play audio). `tests/fixtures` holds the same click track in every format, and the tests check
the clicks land on the same frames in each (exactly for lossless, within 4 frames for MP3).

AAC/M4A will be converted to WAV on import.

## Testing

- `tests/` holds Catch2 unit tests for `core`; CI runs them on Linux and macOS for every push.
- File formats will be checked against real Rekordbox, Serato and MPC behaviour with reference files.
- Things only a person can judge (sound, controllers, exports opened in other software) get a short manual
  checklist per phase.
