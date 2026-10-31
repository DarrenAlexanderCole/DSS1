# DSS1 on Windows

There's no Windows `.exe` yet, and one can't be built from this code. DSS1 is written in SwiftUI and uses
Apple's audio and MIDI frameworks (AVAudioEngine, Audio Units, CoreMIDI), which only exist on macOS. A Windows
version means a port: the same app rebuilt on a cross-platform toolkit.

## The plan: a JUCE port

[JUCE](https://juce.com) is the C++ framework most audio apps use to ship on both Windows and macOS. Its
open-source license (AGPLv3) works with DSS1's GPL, and one JUCE codebase builds for both Windows and macOS.

| DSS1 on macOS | JUCE equivalent |
|---|---|
| SwiftUI views (deck, pads, knobs) | `juce::Component` with custom painting |
| AVAudioEngine graph (player → varispeed → EQ → compressors) | `juce::AudioProcessorGraph` or one `AudioSource` chain |
| AVAudioFile / decoding | `juce::AudioFormatManager` (WAV, AIFF, FLAC, MP3, Ogg; M4A via Media Foundation) |
| Opto compressor Audio Unit | a `juce::AudioProcessor` — the DSP math moves over almost line for line |
| AudioKit effects | JUCE's `dsp` module (filters, delay, reverb) |
| Rubber Band | the same library — it builds on Windows |
| CoreMIDI | `juce::MidiInput` (MIDI Learn and the monitor port directly) |
| UserDefaults | `juce::PropertiesFile` |

What ports with almost no change, because it's plain byte-writing:
- the **Serato** cue export (`SeratoExport.swift`)
- the **MPC** `.xpj` export
- loop and slice maths, the BPM / key analysis logic

## Rough order of work
1. Audio core: load, play, varispeed, reverse, the two compressors.
2. Crate and import (folders, zips, WAV conversion).
3. Chop: pads, loops, memories, exports.
4. Radio deck and Performance screens.
5. MIDI Learn, monitor, MMC.
6. Windows installer (an `.exe` via Inno Setup or a `.msi`).

Help is welcome — open an issue if you'd like to work on it.
