# DSS1 — Digital Sample Station 1

A macOS app for DJs and beatmakers: drop in your record-pool downloads, keep them in a crate, play them on a
tape-style deck, chop them onto pads, and send the results to an MPC or to Serato DJ.

## What's inside

- **Drop** — drop audio files, folders or record-pool zips; DSS1 pulls out the audio (MP3, WAV, FLAC, AIFF,
  M4A) and files it in your crate, as WAV by default.
- **Crate** — your library, sortable by BPM and Camelot key, with album art.
- **Radio** — a tape deck with working transport (REW plays backwards, FF at 1.5x), tempo and key, VU meters,
  a 3-knob compressor and a CL 1B-style tube opto compressor.
- **Chop** — 16 pads per bank, auto loops from 1/16 bar to 32 bars, five loop memories, pad FX, and exports:
  - **MPC** — an MPC project (`.xpj`) with your slices.
  - **Serato DJ** — a tagged copy of the song where pads 1–8 are hot cues and the loop memories are saved loops.
- **Performance** — an effects pad with latch, attack and release.
- **MIDI** — USB controllers work out of the box (Akai MPD / MPK / MPC); MIDI Learn on any control, MMC
  transport, and a MIDI Monitor window for seeing exactly what a controller sends.

## Download

Grab the latest `DSS1-x.y.dmg` from [Releases](../../releases), open it and drag DSS1 to Applications.

The app isn't notarized by Apple, so the first time macOS will say it can't verify DSS1. Click **Done**, then go
to **System Settings → Privacy & Security**, scroll to "DSS1 was blocked…" and click **Open Anyway**.

Needs macOS 14 (Sonoma) or newer, on Apple Silicon or Intel.

Windows: there's no Windows version yet — see [docs/WINDOWS.md](docs/WINDOWS.md).

## Build from source

Needs Xcode or the Command Line Tools (Swift 5.9+).

```bash
./build.sh        # builds DSS1.app in this folder
./make_dmg.sh     # builds a universal DSS1.app and packages dist/DSS1-<version>.dmg
```

`./build.sh test` also compiles the `CRATE_*` test hooks used for automated checks; never ship that build.

The version number lives in `Resources/Info.plist`.

## Saving progress (for the maintainer)

```bash
git add -A
git commit -m "What changed"
git push
```

For a new version: bump `Resources/Info.plist`, add it to [CHANGELOG.md](CHANGELOG.md), run `./make_dmg.sh`,
then `git tag v1.6 && git push --tags && gh release create v1.6 dist/DSS1-1.6.dmg`.

## Credits

- [Rubber Band Library](https://breakfastquay.com/rubberband/) by Particular Programs Ltd — time-stretching and
  pitch-shifting (GPL v2 or later), in `Vendor/RubberBand`.
- [AudioKit](https://github.com/AudioKit/AudioKit) and SoundpipeAudioKit (MIT).
- Serato tag layout from the community documentation at
  [Holzhaus/serato-tags](https://github.com/Holzhaus/serato-tags). DSS1 isn't affiliated with Serato or Akai.

## License

GPL-3.0 — see [LICENSE](LICENSE). Because DSS1 includes Rubber Band (GPL), the app as a whole is GPL too.
