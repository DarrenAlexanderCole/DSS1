DSS1 — Digital Sample Station 1 (test build)

INSTALL
1. Drag DSS1 onto the Applications folder in this window.
2. Open Applications and double-click DSS1.

FIRST OPEN: macOS WILL WARN YOU
This is a personal test build, not from the App Store, so macOS can't verify it.
The first time, it will say Apple could not verify DSS1. Click Done (not Move to Trash), then:
  • Open System Settings → Privacy & Security
  • Scroll down to "DSS1 was blocked…" and click Open Anyway, then confirm.
After that it opens normally.

(Older macOS: right-click DSS1 in Applications → Open → Open.)

GETTING STARTED
• Drop audio files, folders or record-pool zips on the window. DSS1 asks where your crate
  should live the first time, then pulls out just the audio (mp3, wav, flac, aiff, m4a)
  and saves it as WAV (turn that off in DSS1 → Settings).
• ⌘1 Drop screen   ⌘2 The Crate   ⌘3 Radio   ⌘4 Performance pad   ⌘K Chop
• Space plays/pauses.
• Radio: REW plays the tape backwards, FF forwards at 1.5x; prev / next track keys sit under the transport.
• Export for Serato (Chop → EXPORT → SERATO DJ): pads 1–8 of the bank you're on (outlined red) become
  Serato hot cues 1–8 and the loop memories (outlined red) become saved loops. A tagged copy lands in
  the "Serato" folder inside your crate; drag it into Serato DJ. The crate's own file isn't changed.
• MIDI Monitor (MIDI menu, ⌥⌘M): shows every message your controller sends, with a Copy Log button.
• MIDI: the chip shows the last message your controller sends. Right-click a control (or open the chip's MIDI Learn menu) to map the big encoder (twists the FX knob you last clicked, click toggles it), a slider to pitch, and transport buttons. MMC transport works automatically.
• Radio: a CL 1B-style opto compressor sits bottom right of the deck; eject goes back to the Crate.
• Chop: the pads show their computer key until you first press them; loops go down to 1/16 bar and up to 32.
• Perform: ATTACK and RELEASE knobs under LATCH set how the effects fade in and out.
• USB MIDI: plug in (Akai MPD/MPK/MPC work out of the box). Pick the device from the MIDI
  menu; right-click any pad or knob → MIDI Learn to map anything else.

Needs macOS 14 (Sonoma) or newer. Works on Apple Silicon and Intel Macs.

DSS1 uses Rubber Band Library (GPL v2 or later) by Particular Programs Ltd.
and AudioKit (MIT). Source code: https://github.com/DarrenAlexanderCole/DSS1
