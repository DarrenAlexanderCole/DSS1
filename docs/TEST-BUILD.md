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
   finds where the beats and bars fall; you'll see that grid in Prep, coming next.
7. **Play:** double-click a track (or select it and press Return). Space plays and pauses. Click or drag on the
   waveform to jump. The slider changes tempo by up to ±8% (double-click to reset); **Key lock** keeps the
   pitch.

The MP3 timing test has moved: **Deliver → Export Timing Check...** (the MP3 question is settled: cues land on
the same beat in Serato and Rekordbox for WAV and MP3).

## What to tell me

- Did titles and artists come through from your tags? Any garbled accents?
- Did a moved folder get found again?
- Do BPM and key look right for tracks you know? Where they're off, is the BPM half or double, or just wrong?
  How do they compare with Serato and Rekordbox?
- Does playback sound clean, with and without key lock, at 0% and at ±8%?
- Anything confusing, slow or broken, ideally with a screenshot.
