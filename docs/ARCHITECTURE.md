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
| `analysis` (in `core/Analysis.h`) | Tempo, key, loudness; downbeats next | `core` | Tempo, key, loudness done |
| `io` | Decoding, import, folder watching | `core`, dr_wav, dr_flac, minimp3 | Decoding done |
| `engine` | Decks with key lock (Signalsmith Stretch 1.4.0); effects and recorder later. No JUCE: the app's audio callback drives it | `io`, signalsmith-stretch | Deck done |
| `midi` | Controllers, MIDI learn, presets | `engine` | Phase 4 |
| `app` | The four stages and the visual style; audio device (JUCE), Crate (import + background analysis) | everything | Catalog playable |

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
risk damage. Schema 2 adds `watched_folders`. Catalog details (title, artist, album, genre, BPM, key,
loudness) live in the record JSON's `info` object, so adding one needs no schema change.

## Tags, watched folders and missing files

- **Tags:** `src/core/Tags.h` parses ID3v2.2–2.4 (Latin-1, UTF-16, UTF-8; several values joined; numeric
  genres resolved), ID3v1, FLAC Vorbis comments and WAV LIST/INFO. `src/io/TagReader.h` finds those parts in
  a file by seeking, never reading the audio. The file name ("Artist - Title") fills in a missing title or
  artist. Tracks added before tags were read get them on the next scan.
- **Watched folders:** a folder the user adds is watched. The Crate looks in watched folders at launch, every
  three minutes and on request, adding new music. A folder on an unplugged drive is skipped until it's back.
- **Missing files:** a track whose file is gone is marked missing (and not analysed). A new file found in a
  watched folder, or by **Find Missing** in a folder the user picks, relinks a missing track when the matcher
  says it's the same file moved: same file name (accents and case aside) and same length. A converted copy
  (MP3 → WAV) is not relinked automatically, because its frame positions differ.

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

## Analysis

`src/core/Analysis.h`, plain C++ so results match on every platform:

- **Tempo:** spectral-flux onset envelope, autocorrelated; a gentle preference for tempos near 120 BPM picks the
  danceable reading, a backbeat check stops 174 BPM drum & bass reading as 87, and the estimate is refined on
  the peak 16 beats away. Folded into 70–180 BPM. Steady sounds with no pulse give no tempo.
- **Key:** a chromagram (55–2000 Hz) correlated with Krumhansl–Kessler profiles; reported as a name and a
  Camelot code.
- **Beats and bars:** for the tempo found, the beat phase that lines up spectral-flux and bass onsets best
  across the track. Then a straight line is fitted (robustly) through the energy jumps nearest each beat: its
  slope corrects the tempo, so the grid still holds at the end of the track, and its start sets the phase
  (about 1 ms). A tempo within 0.04 BPM of a whole number becomes that number when it fits as well.
  Beat 1 of the bar (4/4 assumed) is the beat where bass power rises and the chroma changes most, measured over
  the half beats either side so claps and snares don't count. The result becomes the track's first grid,
  unless it already has one.
- **Loudness:** integrated LUFS per ITU-R BS.1770 / EBU R128 (K-weighting coefficients derived for any sample
  rate, as libebur128 does; 400 ms blocks, absolute and relative gates).

About 2 seconds per 5-minute track on a laptop (release build). The Crate runs analysis on a background
thread, one track at a time, and saves results with the track.

## Look

Since 2026-11-19: 60s-80s hi-fi, photoreal. The parts are rendered with Blender (Cycles) by
`tools/render/render_parts.py` under one light rig (knobs without pointers, since they look the same at any
angle, so the app draws the engraved pointer; keys per style and state, nine-sliced to any size; toggles,
screws, pads), and `tools/render/make_textures.py` makes the seamless brushed aluminium and walnut. All live in
`assets/render` and are compiled in. `hw::faceplate` is brushed aluminium with a light sheen and hairline scratches, the racks sit
between two full-height walnut boards (`hw::sideBoard`, drawn by MainComponent over its children;
`hw::rackEar` draws nothing now), `hw::wear` gives each key its own patch of scuffs and polish, `hw::recess`/`hw::glass` black glass with a filament mesh and reflections, `hw::readout` a small
lit window for knob values, `hw::bay` an engraved module outline; labels print in dark ink. `hw::light()` is
the lamps' level (warm-up with flicker, slow breathing, mains shimmer, a sag fed by the VU meters'
loudness), applied to every glow; the VU needles are a sprung, damped second-order system at 60 Hz.
The CHOP pad settings have backlit curve monitors drawn as smooth ink (`ChopPanel::paintMonitor`).
`Haptics` taps a Force Touch trackpad as knobs and faders cross their steps (`HapticsMac.mm`).

(Earlier, 2026-11-18:)

DSS1 looks like worn 90s studio rack gear (decided with the user, 2026-11-18, from a design canvas). Every
screen is rack units: aluminium ears, a black anodised faceplate with scratches, grime and edge wear, Phillips
screws, silk-screened labels with worn ink, cream VU meters, fluted knobs, chrome toggles, square keys and an
orange vacuum-fluorescent-style display. `src/app/Hardware.h` draws those pieces; `src/app/Theme.h` is the
LookAndFeel that makes every JUCE control match (key styles: dark, cream, cue, play, pad). Fonts and textures
are compiled in (assets/README.md).

The window: the header unit (badge, VU meters fed by the engine's output RMS, the track display, stage keys,
transport keys) on top, the stage's units in the middle, the deck unit (overview, pitch fader, key lock) at the
bottom. Transport (CUE, play, beat jump) lives in Session, so it works on every stage.

## Prep

`src/app/Session.h` holds the track on the deck for every stage: Catalog loads it, Prep edits it, the
transport bar plays it. Edits replace the whole `TrackRecord` and are saved straight away (the Crate updates
that one track in its snapshot rather than re-reading the library). Analysis results that arrive while a track
is loaded are taken in, but never over the user's cues and loops, and a grid only when the track has none.

`src/app/WaveformView.h` draws min/max peaks per 64-frame block, the grid (beats hidden when closer than 5 px),
tempo changes, hot cues, memory cues, saved loops and the deck's active loop. `src/app/PrepPanel.h` holds the
controls and keys. Snapping is off by default; the setting and the chosen target are kept in DSS1.settings.

## Deliver

`src/core/Deliver.h` turns library tracks into exports, and never touches the user's music files (decided with
the user, 2026-11-17: copies only when exporting). Rekordbox gets one XML collection (Music/DSS1 Exports/DSS1
rekordbox.xml) with a DSS1 playlist, pointing at the originals. Serato reads cues from the files, so it gets
copies in Music/DSS1 for Serato with Markers2 written (and the grid only when asked), made fresh from the
originals each time. Copy names stay unique when two sources share a name. FLAC isn't written for Serato yet
(it needs Vorbis-comment tags, not ID3). Missing tracks are skipped.

## Listen & Design

`src/engine/Effects.h` is the effects chain on the output, after the deck: filter (a state-variable filter,
low-pass left of centre, high-pass right), crusher (sample hold and quantising), echo (tempo-synced delay with a
darkened feedback loop and a 150 ms glide when the time changes) and reverb (Freeverb). Parameters are atomics
set by the UI and smoothed per sample, so knob moves never click; with everything off audio passes untouched.
The XY pad drives the filter and echo mix and restores them on release unless HOLD is on. The recorder
(AudioEngine) writes what you hear, after effects, to a 24-bit WAV through a background writer thread, then the
take is added to the library. While recording, the audio thread also pushes one peak per 512 samples into a
lock-free ring that the Design screen drains 30 times a second to draw the take as it grows.

`src/engine/Sampler.h` is the pad sampler: 8 banks (A-H) of 16 pads, 128 slots, 16 voices, mixed in after the deck
and before the effects. A slot plays a region (a slice) of a sample that the pads of a bank share. Voices follow
MPC conventions (see the drum example in inMusic's MPC plugin SDK): a pad is monophonic (a press restarts it),
choke (on by default) cuts the other pads of the bank, and `release` ends a held pad. Every cut is an equal
crossfade (the new slice fades in as fast as the old one fades out, 4 ms), so pads never stack up or click.
The UI hands it samples and regions through atomics and presses/releases through a lock-free queue, so the
audio thread never locks or frees memory; a replaced sample is kept alive until no voice can be playing it.

Pads also have settings (`Sampler::PadSettings`: gain, tune by resampling, reverse, attack, fade), like an MPC or
Behringer KICK pad. `src/engine/Slicing.h` finds transients (rises in the log energy of the signal's changes,
strongest first, at least 70 ms apart, each placed just before its hit on a zero crossing) and zero crossings.

The CHOP stage (`ChopPanel`, `ChopView`) is the sampler, kept apart from Listen & Design so keys and playback
don't fight. Each bank chops one sample (the deck's track, a loaded file, or a take sent from the recorder) and
keeps up to 16 marks and 16 pad settings; a slice runs from its mark to the next, the last one SLICE beats (or
to where EQUAL 16 / TRANSIENTS ended). `ChopView` is a zoomable waveform like Prep's (smooth zoom around the
mouse, follow the playhead, overview strip; min/max peaks per 256 frames when zoomed out, the wave itself when
zoomed in) with markers that drag and snap (beat grid or zero crossing). Pads are keyed Z X C V / A S D F /
Q W E R / 1 2 3 4 from the bottom row up; MainComponent passes keys to CHOP first while it shows (pads, arrows
to nudge, Cmd+Z to undo, Space for a sound file, which plays through `AudioEngine::chopPreview`). Marks and pad
settings, not audio, are kept in settings ("sliceKit", one line per bank); SAVE BANK renders the slices with
their settings as WAVs into `<samples folder>/Bank X/`.

`MidiPads` listens to every MIDI input (re-scanning when devices come and go) and hands notes, controls and
transport (Start/Stop/Continue, MMC) to the message thread; `ChopPanel` maps notes to pads (default 36-51, the
Akai MPD/MPK layout; MIDI LEARN; velocity sets the pad's level) and a learned control or note to play/pause.

The effects' compressor runs as an OPTO in the UI (`Effects::CompType` still holds VCA, TUBE and FET for later):
program-dependent release and a soft knee, with attack and release set by `compAttackMs` / `compReleaseMs`. A
3-band EQ (RBJ biquads: 120 Hz low shelf, 1 kHz bell, 6 kHz high shelf) sits in the wet path before detection,
so it shapes both what is heard and what drives the squeeze; gain reduction is published for the readout.
`BeatScope` reads the engine's output ring (`AudioEngine::recentOutput`), runs a 4096-point FFT into 72 log
bands (30 Hz-16 kHz) and draws a smooth glowing curve with fading ghosts, a falling peak line and a kick bloom.

Everything drawn behind glass goes through `Scope` (`scope::wave` for mirrored luminous waveforms with a
smoothed core, `scope::trace` for glowing lines, `scope::fillUnder`, `scope::graticule`): the Prep waveform,
the overview, the CHOP editor and its overview, and the recorder's take. Readouts use Rajdhani SemiBold with
digits respaced to one width (`assets/fonts/RajdhaniTab-SemiBold.ttf`).

Track notes live in `TrackInfo::notes` (JSON "notes", omitted when empty). Only `Crate::setNotes` changes them:
`Crate::save` keeps the stored notes, since a deck's copy of a record can be older than an edit in Catalog.
Rekordbox export writes them as the TRACK's Comments attribute. The app icon is rendered by
`tools/render/render_icon.py` (print from `make_icon_print.py`) into `assets/icon/dss1_icon.png`.

`src/engine/TakePlayer.h` plays the last take back, mixed in after the effects. Takes record into the temp
folder and move to Music/DSS1 Recordings (and into the library) only when saved.

## Playback

`src/engine/Deck.h`: plays a decoded track at 0.5–1.5x. With key lock, Signalsmith Stretch time-stretches
(same pitch); without, cubic resampling behaves like a turntable. Either way it converts the track's sample rate
to the device's. The message thread loads, seeks and sets tempo; the audio callback only renders, and old audio
is always freed on the message thread. A loop wraps the read position inside [start, end) before samples
reach the stretcher, so key-locked loops are seamless.

## Decoding

Cue positions only line up across Rekordbox, Serato and other computers if every file decodes to the same
samples, so DSS1 bundles its own decoders rather than using the operating system's: `src/io/AudioDecoder.h`
reads WAV and AIFF (dr_wav), FLAC (dr_flac) and MP3 (minimp3), all public domain, pinned to commits.

MP3 encoders add silence at the start (encoder delay) and end (padding). When the file says how much (a
LAME/Xing header, which almost every MP3 has), DSS1 removes it, so frame 0 is the first real sample and the
file is exactly as long as the music that was encoded. Serato and Rekordbox don't: checked by hand with the
timing test (v2.0.0-test.2), MP3 cues landed about 25 ms early in both, matching the usual 1105-frame delay
(576 encoder + 529 decoder). So each track keeps `exportOffsetFrames` (the removed delay, 0 for lossless
formats); Serato and Rekordbox exports add it to every position and imports subtract it. `tests/fixtures` holds the same click track in every format, and the tests check
the clicks land on the same frames in each (exactly for lossless, within 4 frames for MP3).

AAC/M4A will be converted to WAV on import.

## Testing

- `tests/` holds Catch2 unit tests for `core`; CI runs them on Linux and macOS for every push.
- File formats will be checked against real Rekordbox, Serato and MPC behaviour with reference files.
- Things only a person can judge (sound, controllers, exports opened in other software) get a short manual
  checklist per phase.
