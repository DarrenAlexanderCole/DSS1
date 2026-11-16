**This is an early test build of DSS1 V2: the MP3 timing test.** It doesn't play audio yet. It runs on Apple
Silicon and Intel Macs with macOS 13 or later.

## Install

1. Download the `.dmg` below and open it.
2. Drag **DSS1** onto **Applications** (replace the older test build if asked).
3. Open DSS1 from Applications. The build isn't notarised by Apple yet, so macOS will block it the first time:
   - **macOS 15 or later:** open **System Settings → Privacy & Security**, scroll down to the message about DSS1
     and click **Open Anyway**, then confirm.
   - **macOS 13–14:** right-click (or Control-click) DSS1 in Applications, choose **Open**, then **Open** again.

## Test

DSS1 opens on the **Deliver** tab. Drop a track onto the box (or click **Choose a track...**). An **MP3 with a
strong kick or snare**, at least 30 seconds long, is the most useful. DSS1 finds 8 sharp hits in the music and
puts hot cues A–H exactly on them. It leaves your original alone and writes to **Music → DSS1 Test Exports**:

- a copy of the track with the cues inside it, for Serato
- for an MP3, a second copy saved as WAV by DSS1 itself, with the same cues
- `DSS1 timing.xml`, with the same cues for Rekordbox

**What to look for:** zoom the waveform in as far as it goes. Each cue should sit at the very start of a hit,
where the waveform jumps up. Load the MP3 copy and the WAV copy side by side: if the WAV's cues sit on the hits
but the MP3's are a little early or late, that's the timing difference this test is looking for.

**Serato DJ:** drag the copies into your library and load them.

**Rekordbox 6 or 7:** in Preferences → Advanced → Database, set the rekordbox xml Imported Library to
`DSS1 timing.xml`. In the side panel, open rekordbox xml → Playlists → DSS1 timing, right-click each track and
choose **Import To Collection**, then load them.

Please report what you saw, ideally with a zoomed-in screenshot of one cue in each app.
