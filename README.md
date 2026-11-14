# DSS1 V2: Digital Sample Station

![Status: in development](https://img.shields.io/badge/status-in%20development-orange) ![Platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey) ![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)

**A DJ prep tool: get your music ready before the gig.**

> This is the `v2` branch, a new app built from scratch. It is in early development and not usable yet.
> The DSS1 v1.5 beta (SwiftUI) lives on the [`main` branch](../../tree/main) and the [`v1.5` tag](../../releases/tag/v1.5).

## The idea

DSS1 V2 is organised around four stages:

1. **Catalog** — bring music in (files, folders, record-pool zips) and keep it organised by BPM, key, loudness and tags.
2. **Prep** — the heart of the app. Beat grids (constant or variable tempo), hot cues A–H, memory cues and saved loops,
   set once and checked against what Rekordbox and Serato can hold.
3. **Listen & Design** — a listening deck with real-time effects, a recorder, an XY performance pad and a pad sampler.
4. **Deliver** — Rekordbox XML export and import, Serato-tagged copies, MPC projects and WAV slices.

Rekordbox, Serato and MPC are *export targets*, not separate modes: every track has one set of cues, loops and grid,
and each export takes what its software supports.

## Roadmap

| Phase | What | Status |
|---|---|---|
| 1 | Foundations: project, CI, test harness, app shell, beat-grid and track-record core | In progress |
| 2 | Track data & formats: library store, Rekordbox XML, Serato tags, track matching | |
| 3 | Audio engine + Catalog | |
| 4 | Prep | |
| 5 | Deliver → **DJ prep beta** | |
| 6 | Listen & Design → **1.0** | |
| Later | Windows | |

macOS comes first. The app is built on [JUCE](https://juce.com) so a Windows version can follow from the same code.

## Building

You need macOS 13 or later, the Xcode command line tools and CMake 3.22+ (`brew install cmake`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure      # run the tests
open build/src/app/DSS1_artefacts/Release/DSS1.app
```

The first configure downloads JUCE, Catch2 and nlohmann/json, so it takes a few minutes.

Every push to `v2` is built and tested on GitHub Actions; the macOS app is attached to each run as `DSS1-macOS`.
It isn't signed yet, so the first time you open a downloaded copy, right-click it and choose **Open**.

## Project layout

```
src/core/    Track data, beat grids, file formats — plain C++20, no UI or audio, fully unit-tested
src/app/     The JUCE desktop app
tests/       Unit tests (Catch2)
docs/        Architecture notes
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

## Contributing

Ideas, bug reports and pull requests are welcome. Open an issue first for anything large, so we can agree on the
approach before you start.

## License

DSS1 V2 is free software under the [GNU Affero General Public License v3.0](LICENSE). It uses JUCE under the AGPLv3.
