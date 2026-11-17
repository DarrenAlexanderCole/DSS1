**This is an early test build of DSS1 V2: the first one that plays music.** It runs on Apple Silicon and Intel Macs
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
2. **Wait for analysis:** BPM, key (Camelot and name) and loudness fill in, a couple of seconds per track. The
   crate is saved, so it's still there next time you open DSS1.
3. **Play:** double-click a track (or select it and press Return). Space plays and pauses. Click or drag on the
   waveform to jump.
4. **Tempo and key lock:** the slider changes tempo by up to ±8% (double-click it to reset). With **Key lock**
   on, the pitch stays the same; turn it off to hear it like a turntable.
5. **Sort:** click a column header (click again to reverse).

The **Deliver** tab still has the MP3 timing test from the last build.

## What to tell me

- Do BPM and key look right for tracks you know? Where they're off, is the BPM half or double, or just wrong?
  How do they compare with Serato and Rekordbox?
- Does playback sound clean, with and without key lock, at 0% and at ±8%?
- Anything confusing, slow or broken, ideally with a screenshot.
