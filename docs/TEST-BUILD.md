**This is an early test build of DSS1 V2.** It runs on Apple Silicon and Intel Macs
with macOS 13 or later.

## Install

1. Download the `.dmg` below and open it.
2. Drag **DSS1** onto **Applications** (replace the older test build if asked).
3. Open DSS1 from Applications. The build isn't notarised by Apple yet, so macOS will block it the first time:
   - **macOS 15 or later:** open **System Settings → Privacy & Security**, scroll down to the message about DSS1
     and click **Open Anyway**, then confirm.
   - **macOS 13–14:** right-click (or Control-click) DSS1 in Applications, choose **Open**, then **Open** again.

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
7. **Play:** double-click a track (or select it and press Return). The player along the bottom stays on every
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
- **Export Timing Check...** is still at the bottom.

## What to tell me

- In Prep: is the grid on the beat, and is bar 1 on the real beat 1? Was anything awkward when setting cues
  and loops?
- Deliver: do your cues and loops show up in Rekordbox and Serato? If a track was already in your Rekordbox
  collection, did Import To Collection update its cues?
- Did titles and artists come through from your tags? Any garbled accents?
- Did a moved folder get found again?
- Do BPM and key look right for tracks you know? Where they're off, is the BPM half or double, or just wrong?
  How do they compare with Serato and Rekordbox?
- Does playback sound clean, with and without key lock, at 0% and at ±8%?
- Anything confusing, slow or broken, ideally with a screenshot.
