**DSS1 V2 beta.** A place to get your music ready before the show or the session: sort it, prep it, listen
and shape it, chop it onto pads, and send it to Rekordbox, Serato or your MPC. It runs on Apple Silicon and
Intel Macs with macOS 13 or later. Your music files are never changed.

The **DSS1 V2 Beta Guide** (DSS1-V2-Beta-Guide.pdf, below) walks through every screen. For a quick look, there's a
32-second vertical tour (DSS1-V2-Beta-Slides.mp4) and the slides themselves (DSS1-V2-Beta-Slides-9x16.zip).

## Install

1. Download **DSS1-v2.0.0-beta.1.dmg** below and open it.
2. Drag **DSS1** onto **Applications**. (Have DSS1 1.5 installed and want to keep it? See the end of these
   notes first.)
3. Open DSS1 from Applications. The beta isn't notarised by Apple yet, so macOS blocks it the first time:
   - **macOS 15 or later:** open **System Settings → Privacy & Security**, scroll down to the message about DSS1
     and click **Open Anyway**, then confirm.
   - **macOS 13–14:** right-click (or Control-click) DSS1 in Applications, choose **Open**, then **Open** again.

   You only do this once.

## The five screens

DSS1 opens under its glass dust cover: click it to lift it.

- **Catalog:** drag in files, folders or record-pool zips. BPM, key, loudness and the beat grid are worked out
  for you. Sort by anything, and keep your own **NOTES** on each track.
- **Prep:** set hot cues A–H, memory cues and saved loops on a zoomable waveform, and fix the beat grid
  (including tracks whose tempo drifts).
- **Listen / Design:** play the track through FILTER, ECHO, REVERB and an opto COMP with its own EQ, with
  switches that change the sound dramatically (DRIVE, PUNCH, SQUASH, CRUSH, PING, TAPE, DUCK, FREEZE, GATE,
  HALO). Record what you hear, effects and all.
- **Chop:** slice any track, file or recording across 128 pads (banks A–H). Tap an empty pad to mark a slice,
  tap a marked pad to play it, shape each pad (GAIN, TUNE, ATTACK, FADE, REVERSE) and save the bank as WAVs.
  Plays from the keyboard or a MIDI pad controller (Akai MPC, MPD, MPK and others).
- **Deliver:** export your cues, loops and notes to Rekordbox (XML) or as copies for Serato.

## Keys worth knowing

| Key | Does |
|---|---|
| Space or P | Play / pause (every screen) |
| Q | CUE |
| A–H (Prep) | Set or jump to a hot cue; Shift deletes |
| M, L | Memory cue, save loop (Prep) |
| 1–4, Q–R, A–F, Z–V (Chop) | The 16 pads, laid out like a drum machine |
| Option + pad | Move that pad's start to the playhead (Chop) |
| 0 (Chop) | Stop everything |
| Cmd+Z (Chop) | Undo |

## Feedback

Tell us what works, what's confusing and what breaks: [open an issue](https://github.com/DarrenAlexanderCole/DSS1/issues),
ideally with a screenshot. Especially useful: whether BPM, key and the grid are right for tracks you know, and
whether your cues and loops arrive in Rekordbox and Serato.

## Looking for DSS1 1.5?

The earlier version is still available: [DSS1 1.5 (Beta)](https://github.com/DarrenAlexanderCole/DSS1/releases/tag/v1.5).
Both are called **DSS1** in Applications, so installing V2 replaces 1.5. To keep both, rename the old one first
(for example **DSS1 1.5**). Each keeps its own library, so nothing of yours is lost either way.
