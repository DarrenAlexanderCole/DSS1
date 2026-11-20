**This is an early test build of DSS1 V2.**  It runs on Apple Silicon and Intel Macs
with macOS 13 or later.

## Install

1. Download the `.dmg` below and open it.
2. Drag **DSS1** onto **Applications** (replace the older test build if asked).
3. Open DSS1 from Applications. The build isn't notarised by Apple yet, so macOS will block it the first time:
   - **macOS 15 or later:** open **System Settings → Privacy & Security**, scroll down to the message about DSS1
     and click **Open Anyway**, then confirm.
   - **macOS 13–14:** right-click (or Control-click) DSS1 in Applications, choose **Open**, then **Open** again.

## New look

DSS1 now looks like 60s-80s hi-fi: brushed aluminium faces, walnut cheeks, machined aluminium and brass
knobs, black and ivory keys, chrome toggles, silicone pads, black-glass windows lit amber from behind and
backlit VU meters. The parts are rendered in 3D. Things to notice:

- **The frame:** one board of matte burl walnut down each side holds all the racks; it sits back in the shadows.
- **Keys:** black phenolic with white legends, worn in their own ways (rubbed edges, a fingertip polish), and
  the faces carry hairline scratches along the grain: used, but looked after.
- **Pads:** treated rawhide dyed charcoal, with worn grooves and white legends (number and key), each with a ring
  that lights up when the pad plays or is marked. In CHOP the 16 sit centred in their space.
- **Tempo lights:** small squares step along with the music like a step sequencer's running light: four beats
  beside **BPM** on the track display, and sixteen steps (one bar) in Prep's grid window. The first square is
  the bar's downbeat when the track has a grid.
- **Displays:** everything behind the glass is drawn smooth now, lit warm like the VU meters: waveforms are
  mirrored glowing shapes with a brighter core, the spectrum and pad monitors are smooth curves, and readouts use
  a clean modern face (Rajdhani). The only old things are the materials.
- **Icon:** a speaker driver in walnut and brushed aluminium, with a small DSS1 on its dark cone.
- **Power:** a red jewel lamp in a knurled chrome bezel. The **PAD** switch beside it takes 20 dB off both VU
  meters so loud masters don't pin the needles (on by default).
- **Power-up:** the displays and meters warm up when DSS1 opens, with a flicker, and the needles kick across.
- **Living light:** the displays breathe slightly, and dip with loud music like a real amplifier's supply.
- **Haptics:** on a MacBook (Force Touch) trackpad, knobs and faders click into their steps under your finger.

Where things are:

- **Top unit (every screen):** the stage keys (Catalog, Prep, Listen / Design, Chop, Deliver), the track display
  (title, artist, BPM at the current pitch, key, time, bar), VU meters, and the transport: **|<**, **<<**,
  **>>**, **CUE** and **PLAY**.
- **Bottom unit (every screen):** the whole-track overview (click to jump), the **pitch fader** with its BPM
  readout, **RANGE** keys (8%, 16% or 50%), **RESET** and the **KEY LOCK** switch.
- **Prep:** the loop length and beat-jump size are now **knobs** (drag up and down, or scroll). Turning
  **LOOP BEATS** while a loop plays resizes it live, from the same start. Hot cues are
  backlit pads.

## Try it

DSS1 opens on **Catalog**.

1. **Add music:** drag files, a folder or a record-pool zip onto the window, or click **Add Music...**. WAV, AIFF,
   FLAC and MP3 work. Zips are unpacked into Music → DSS1 Imports. Your files aren't changed or moved.
2. **Tags:** title, artist, album and genre now come from the files' tags (the file name is used when a tag is
   empty). Tracks you added in earlier test builds pick up their tags a moment after DSS1 opens.
3. **Watched folders:** a folder you add is watched. Put a new track in it and DSS1 adds it within three
   minutes, or straight away with **Folders → Look for New Music Now**. **Folders** also lists them, and
   **Stop Watching** forgets one (its tracks stay).
4. **Missing files:** move or rename a folder of tracks in Finder. Those tracks show as *Missing*. If they're in
   a watched folder DSS1 finds them by itself; otherwise click **Find Missing...** and pick where they are now.
5. **Remove:** select tracks (Shift or Command for several) and press Delete, or right-click → **Remove from
   DSS1**. Only DSS1's entry goes; the audio files stay. Right-click also has **Show in Finder**.
6. **Analysis:** BPM, key (Camelot and name) and loudness fill in, a couple of seconds per track. DSS1 also
   finds where the beats and bars fall, and that becomes the track's first grid. Tracks analysed by an
   earlier build: right-click → **Analyse Again (New Grid)** to get the improved grid.
7. **Notes:** the **NOTES** column is yours. Double-click a track's note to type (Return saves, Escape
   cancels). Notes stay with the track, sort like any column, and go to Rekordbox as the track's Comments when
   you export.
8. **Play:** double-click a track (or select it and press Return). The player along the bottom stays on every
   tab. Space plays and pauses. The slider changes tempo by up to ±8% (**Reset**, or double-click the slider, goes back to 0), and the BPM next to it shows the new tempo; **Key lock**
   keeps the pitch.

## Prep (new)

Double-click a track in Catalog, then open **Prep**.

- **Waveform:** the grid is drawn on it (bar numbers on beat 1). Scroll up and down (or pinch) to zoom
  smoothly around the mouse; swipe sideways or drag to move through the track. **-** / **+** (or the - and =
  keys) and **Whole Track** too. Click to jump; drag a cue to move it.
- **Transport:** **CUE** works like a CDJ: paused, it sets the cue point (or plays while held if you're on
  it); playing, it goes back to the cue and stops (Q taps it). **|<** goes to the start (Home). **<<** and
  **>>** beat-jump by the size you pick (16 beats to start; [ and ] keys). The time and bar.beat are shown.
- **Hot cues A-H:** click a letter (or press A-H) to set it at the playhead; click again to jump to it.
  Shift-click (or Shift + letter) deletes. Right-click a cue on the waveform to rename, recolour or delete.
- **Memory cues:** **+ Memory Cue** (or M). **<** and **>** (or Up and Down) jump between them.
- **Loops:** pick a length and **Save Loop** (or L), or **Loop In** then **Loop Out**. The numbers 1-8 play a
  saved loop; **Exit Loop** (or Escape) carries on. **Delete Loop** deletes the loop that's playing (or
  Shift-click its number) so you can set a better one. Drag a loop's blue bar to move it; the loop you hear
  moves with it.
- **Grid:** **Downbeat Here** makes the playhead beat 1. **Nudge** moves the grid 2 ms (Shift: 0.5 ms).
  **/2** and **x2** fix half or double tempo. Type a BPM and press Return. **Tempo Change Here** starts a new
  tempo for tracks that drift (live drums, edits); type its BPM with the playhead after it.
- **Snap to grid** is off unless you turn it on. **Left / Right** move a beat (Shift: four).
- **Prepare for Rekordbox / Serato:** the line under the title says what that app can't take (Serato has no
  memory cues, for instance).
- Everything saves as you go.

## Listen / Design (new)

Load a track (double-click it in Catalog), press PLAY, then open **Listen / Design**.

- **Effects** play over everything you hear. Flip a module's switch to turn it on:
  - **FILTER:** one knob, low-pass to the left, high-pass to the right, open in the middle.
  - **ECHO:** TIME in beats (synced to the track's BPM at your pitch), FEEDBACK and MIX.
  - **REVERB:** SIZE and MIX.
  - **COMP:** an opto compressor with a 3-band EQ in front. **LOW** (120 Hz shelf), **MID** (1 kHz) and **HIGH**
    (6 kHz shelf) shape what gets squeezed, ±12 dB each. **AMOUNT** sets how hard it works (its readout shows
    the gain reduction while it's on), **ATTACK** (1-200 ms) and **RELEASE** (40 ms-3 s) set how fast it grabs
    and lets go, and **COMP MIX** blends in the dry sound (parallel compression). A compressor has no decay or
    sustain stage, so attack and release are its whole envelope. Four backlit monitors along the top show what
    the knobs do: the **EQ** curve, the **transfer curve** (with a dot riding it and a bar for the live gain
    reduction while it's on), the **attack/release** envelope on a hit, and how the **mix** shapes a drum hit.
  - Double-click a knob to reset it. **ALL OFF** turns every effect off.
- **XY pad:** hold the mouse down on it and move: left and right sweep the filter, up adds echo. Let go and it
  springs back. With **HOLD** on, it stays where you leave it.
- **Input pad:** the **-10**, **-20** and **-50** switches after TO CHOP lower what the recorder takes in
  (one at a time; recording only, you still hear the full level).
- **Spectrum:** under the recorder display, a glowing curve of everything you hear (30 Hz to 16 kHz): ghosts
  of the last moments fade behind it, a thin peak line drifts down, and the window blooms on every kick.
- **Recorder:** **REC** records what you hear, effects included, and draws the waveform as it goes. Press
  **STOP**, then **PLAY** to hear the take on its own (the deck pauses; click the waveform to play from there).
  **SAVE** keeps it (Music > DSS1 Recordings, and into Catalog); **DISCARD** throws it away; pressing **REC**
  again records another and throws away the one you didn't save. A take left unsaved when you quit is kept in
  DSS1 Recordings > Unsaved Takes.
- **TO CHOP** saves the take and opens it on the **CHOP** screen, ready to slice.
- **Space** or **P** plays and pauses on every screen (except while typing in a search box).

## Chop (new)

The sampler has its own screen now, built for producers like Prep is for DJs. Each bank (**A-H**) chops one
sample into 16 slices, one per pad: the track on the deck, a sound file (**LOAD...**), or a take from the
recorder (**TO CHOP**). Pads are laid out and keyed like a drum machine:

| | | | |
|---|---|---|---|
| 13 `1` | 14 `2` | 15 `3` | 16 `4` |
| 9 `Q` | 10 `W` | 11 `E` | 12 `R` |
| 5 `A` | 6 `S` | 7 `D` | 8 `F` |
| 1 `Z` | 2 `X` | 3 `C` | 4 `V` |

- **The waveform** zooms like Prep's: scroll (or pinch) to zoom around the mouse, swipe or drag to move, the
  strip below shows the whole sample (click it to jump). **SLICE** zooms to the chosen pad's slice, **WHOLE**
  shows everything; double-click a slice to zoom to it. Zoomed right in you see the wave itself.
- **Chop it:** press **Space** to play, then tap empty pads on the hits you want. Or **TRANSIENTS** puts a slice
  at each of the 16 strongest hits (in the loop that's playing, or what's on screen), or **EQUAL 16** cuts the
  loop (or 16 x **SLICE** beats from the playhead) into 16 equal slices. In **CHOP** mode a pad press moves its
  mark to the playhead and a click on the waveform puts the chosen pad's mark there.
- **Fine-tune:** drag a marker, or nudge the chosen one with the **Left / Right** arrows (Shift: further) or
  **< >**. **SNAP** sets where markers land: **ZERO** (the nearest zero crossing, so slices don't click),
  **BEAT** (the grid) or **OFF**. **UNDO** (Cmd+Z) takes back marks and pad settings.
- **Play:** in **PLAY** mode pads play their slices; click a slice on the waveform to hear it. **CHOKE** (on) lets
  one slice play at a time; **HOLD** plays a slice only while held. Nothing clicks: cuts are crossfaded.
- **Pad settings** (right): **GAIN**, **TUNE** (semitones), **ATTACK**, **FADE** and **REVERSE** for the chosen
  pad. Above each knob a backlit monitor shows what it does: GAIN the slice's waveform at its level (dashes
  mark clipping), TUNE a wave that tightens as you tune up, ATTACK and FADE the envelope over the slice's real
  length. A cursor runs through them while the pad plays. Double-click a knob to reset it. Pads with settings show a small light.
- **Space** plays and pauses the deck, or the sound file you're chopping (from where you last clicked).
- **SLICE** sets how long the last slice plays, in beats, and the slice size for EQUAL 16. At **MM** (6 o'clock,
  manual) you chop by tapping along with the music: MODE switches to **CHOP**, and whichever pad you hit, the
  marks go in order, A1 to A16, then on into B1 and through to H16, without stopping the music. Each slice ends
  where the next one starts (A16 ends at B1); drag the **END** marker for the very last. Switch MODE to **PLAY**
  to play the pads. EQUAL 16 in MM chops what's on screen.
- **MIDI:** plug in a pad controller (an Akai MPD, MPK, MPC, or anything that sends notes): its 16 pads play
  DSS1's pads (Akai pads work out of the box, harder hits play louder), and MIDI Start/Stop or MMC plays and
  pauses. **MIDI LEARN** to map your own: hit a controller pad for each DSS1 pad in turn (click a DSS1 pad to
  pick which), press a button on the controller to make it play/pause, then press **MIDI LEARN** again. Right-
  click it for maps: **Follow the controller** (the default: an MPC gets the MPC pad layout, other controllers
  36-51, and a note outside the map switches to the layout it belongs to), **Akai MPC**, **MPD/MPK 36-51** or
  **Chromatic 37-52**. If an MPC Sample pad still plays the wrong DSS1 pad, tell me which pad played which. The light by **MIDI** blinks with what arrives.
- **SHOW SAVED** opens the samples folder in Finder.
- **SAVE BANK** writes the slices, with their settings, as WAVs to the **SAVE TO** folder (Bank A … Bank H).
  **CLEAR** clears the chosen pad, **CLEAR BANK** the whole bank. Your chops are still there next time.

## Deliver (new)

Your music files are never changed. Choose **Tracks you've prepared** (any cue, loop or grid), **All tracks**,
or **Tracks selected in Catalog**. To export one track or a handful: select them in Catalog (Shift or Command
for several), right-click → **Export This Track...** (or **Export N Tracks...**), and Deliver opens set to
just those. The line next to the menu says which tracks will go.

- **Export for Rekordbox** writes Music → DSS1 Exports → DSS1 rekordbox.xml, pointing at your original files.
  In Rekordbox: Preferences → Advanced → Database → rekordbox xml, choose that file. In the sidebar open
  **rekordbox xml → Playlists → DSS1**, select the tracks, right-click → **Import To Collection**.
- **Export Copies for Serato** copies the tracks into Music → DSS1 for Serato with your cues and loops inside.
  Drag that folder into Serato. Exporting again replaces the copies. **Include beat grid** is off unless you
  turn it on. FLAC isn't supported for Serato yet.
- **Destination:** each export shows where it goes. **CHANGE...** picks another file (Rekordbox) or folder
  (Serato). DSS1 won't put Serato copies inside a watched folder, and never replaces an original.
- **Export Timing Check...** is still at the bottom.

## What to tell me

- In Prep: is the grid on the beat, and is bar 1 on the real beat 1? Was anything awkward when setting cues
  and loops?
- Deliver: do your cues and loops show up in Rekordbox and Serato? If a track was already in your Rekordbox
  collection, did Import To Collection update its cues?
- Did titles and artists come through from your tags? Any garbled accents?
- Did a moved folder get found again?
- Listen / Design: do the effects sound right, and do they ever click or crackle? Is the echo in time?
- Do BPM and key look right for tracks you know? Where they're off, is the BPM half or double, or just wrong?
  How do they compare with Serato and Rekordbox?
- Does playback sound clean, with and without key lock, at 0% and at ±8%?
- Anything confusing, slow or broken, ideally with a screenshot.
