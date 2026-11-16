**This is an early test build of DSS1 V2, for checking cue positions in Serato and Rekordbox.** It doesn't play
audio yet. It runs on Apple Silicon and Intel Macs with macOS 13 or later.

## Install

1. Download the `.dmg` below and open it.
2. Drag **DSS1** onto **Applications**.
3. Open DSS1 from Applications. The build isn't notarised by Apple yet, so macOS will block it the first time:
   - **macOS 15 or later:** open **System Settings → Privacy & Security**, scroll down to the message about DSS1
     and click **Open Anyway**, then confirm.
   - **macOS 13–14:** right-click (or Control-click) DSS1 in Applications, choose **Open**, then **Open** again.

## Test

DSS1 opens on the **Deliver** tab. Drop a WAV, AIFF or MP3 onto the box (or click **Choose a track...**).
Use a track that's at least 70 seconds long. DSS1 leaves your original alone and writes two files to
**Music → DSS1 Test Exports**:

- a copy of the track with Serato cues, loops and a beat grid inside it
- `DSS1 test.xml`, with the same positions for Rekordbox

Each cue and loop starts at exactly these times. In Rekordbox they also sit on bar lines of a 120 BPM grid from 0:00; Serato gets no grid, because DSS1 leaves Serato grids off unless you ask:

| What | Where |
|---|---|
| Hot cues A–H (Serato 1–8) | 0:08, 0:16, 0:24, 0:32, 0:40, 0:48, 0:56, 1:04 |
| Memory cue (Rekordbox only) | 0:04 |
| Saved loop 1 | 0:32 – 0:40 |
| Saved loop 2 | 0:48 – 0:50 |

**Serato DJ:** drag the test copy into your library, load it, and check the times of the cues and loops in the cue panel.

**Rekordbox 6 or 7:** in Preferences → View → Layout, tick "rekordbox xml". In Preferences → Advanced → Database,
set the rekordbox xml Imported Library to `DSS1 test.xml`. In the side panel, open rekordbox xml → Playlists →
DSS1 test, right-click the track and choose **Import To Collection**, then load it.

Please report whether each position landed where the table says, and if not, roughly how far off it was. Trying a
WAV and an MP3 is most useful: MP3s are where DJ apps most often disagree about timing.
