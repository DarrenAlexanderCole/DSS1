#if TESTHOOKS
import AppKit
import AVFoundation
import CoreMIDI
import SwiftUI

/// Environment-variable test hooks (CRATE_*) for measuring the app without listening.
/// Compiled only into `./build.sh test` builds; shared builds don't contain them.
enum TestHooks {
    static func install() {
        NSLog("CRATETEST hooks installed")
        // CRATE_HEARTBEAT=1: every second, how long the main queue and the main actor take to answer.
        if ProcessInfo.processInfo.environment["CRATE_HEARTBEAT"] != nil {
            Thread.detachNewThread {
                for _ in 0..<30 {
                    Thread.sleep(forTimeInterval: 1)
                    let t0 = Date()
                    let sem = DispatchSemaphore(value: 0)
                    DispatchQueue.main.async { sem.signal() }
                    let q = sem.wait(timeout: .now() + 3) == .timedOut ? -1 : Date().timeIntervalSince(t0) * 1000
                    let t1 = Date()
                    let sem2 = DispatchSemaphore(value: 0)
                    Task { @MainActor in sem2.signal() }
                    let a = sem2.wait(timeout: .now() + 3) == .timedOut ? -1 : Date().timeIntervalSince(t1) * 1000
                    let t2 = Date()
                    let sem3 = DispatchSemaphore(value: 0)
                    Task.detached { try? await Task.sleep(for: .milliseconds(10)); sem3.signal() }
                    let z = sem3.wait(timeout: .now() + 3) == .timedOut ? -1 : Date().timeIntervalSince(t2) * 1000
                    DispatchQueue.main.async {
                        MainActor.assumeIsolated {
                            let p = AppModel.shared.player
                            NSLog("CRATETEST state playing %d time %.1f url %@", p.isPlaying ? 1 : 0, p.currentTime, p.currentURL?.lastPathComponent ?? "-")
                        }
                    }
                    NSLog("CRATETEST heartbeat main queue %.0f ms · main actor %.0f ms · 10 ms sleep took %.0f ms · player %.1f s",
                          q, a, z, 0.0)
                }
            }
        }
        // Debug aid: CRATE_SNAPSHOT=/path/out writes the window to out-1.png, out-2.png…
        // at each of the comma-separated CRATE_SNAPSHOT_DELAY seconds (default 2.5).
        let env = ProcessInfo.processInfo.environment
        // Debug aid: CRATE_AUTOPLAY=1 opens the player and starts the cued track.
        if env["CRATE_AUTOPLAY"] != nil {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
                if env["CRATE_NOPLAY"] == nil { AppModel.shared.showPlayer() }
                if env["CRATE_NOSHOW"] == nil, !AppModel.shared.player.isPlaying || env["CRATE_NOPLAY"] != nil {}
                if env["CRATE_NOPLAY"] == nil && env["CRATE_SHOWONLY"] == nil, !AppModel.shared.player.isPlaying { AppModel.shared.player.toggle() }
                if env["CRATE_NOPLAY"] != nil, !AppModel.shared.player.isPlaying { AppModel.shared.player.toggle() }
            }
        }
        let launched = Date()
        if env["CRATE_AK_OFFLINE"] != nil { AudioKitCheck.run() }
        // Debug aid: CRATE_PERFORM=1 opens the performance pad and measures what the pad does to the sound,
        // then holds the pad (latched) for a snapshot and puts everything back.
        if env["CRATE_PERFORM"] != nil {
            Task { @MainActor in
                let model = AppModel.shared, deck = model.performance
                try? await Task.sleep(for: .seconds(2.5))
                model.showPlayer()
                try? await Task.sleep(for: .seconds(0.5))
                model.showPerformance()
                while deck.duration == 0 { try? await Task.sleep(for: .milliseconds(100)) }
                let savedFX = deck.fx, savedLatch = deck.latch
                deck.latch = false
                for kind in FXKind.allCases { deck.fx[kind] = FXState(on: kind == .lpf, depth: 1) }
                deck.seek(to: 30 / deck.duration)
                if !deck.isPlaying { deck.togglePlay() }
                @MainActor func measure() async -> (all: Double, high: Double) {
                    try? await Task.sleep(for: .milliseconds(400))
                    var acc = [Double](repeating: 0, count: 20); var n = 0.0
                    let end = Date().addingTimeInterval(1.6)
                    while Date() < end {
                        let sp = deck.sampler.meter.spectrum()
                        for i in 0..<20 { acc[i] += Double(sp[i]) }
                        n += 1
                        try? await Task.sleep(for: .milliseconds(30))
                    }
                    let a = acc.map { $0 / max(n, 1) * 54 }
                    func avg(_ r: Range<Int>) -> Double { r.map { a[$0] }.reduce(0, +) / Double(r.count) }
                    return (avg(0..<20), avg(14..<20))
                }
                let dry = await measure()
                NSLog("CRATETEST dry highs %.3f all %.3f playing %d", dry.high, dry.all, deck.isPlaying ? 1 : 0)
                deck.padDown(CGPoint(x: 0.05, y: 0.5))
                try? await Task.sleep(for: .milliseconds(300))
                NSLog("CRATETEST wet after 300ms %.2f engaged %d attack %.2f", deck.wet, deck.engaged ? 1 : 0, deck.attack)
                let closed = await measure()
                deck.padMove(CGPoint(x: 0.95, y: 0.5))
                let open = await measure()
                deck.padUp()
                let released = await measure()
                NSLog("CRATETEST pad LPF armed: held left highs %+.1f dB, dragged right highs %+.1f dB, released highs %+.1f dB (vs untouched)",
                      closed.high - dry.high, open.high - dry.high, released.high - dry.high)
                deck.latch = true
                deck.padDown(CGPoint(x: 0.05, y: 0.5)); deck.padUp()
                let latched = await measure()
                deck.latch = false
                let unlatched = await measure()
                NSLog("CRATETEST latch: after letting go highs %+.1f dB, latch off highs %+.1f dB, engaged %d",
                      latched.high - dry.high, unlatched.high - dry.high, deck.engaged ? 1 : 0)
                for kind in FXKind.allCases { deck.fx[kind] = FXState(on: kind == .lpf || kind == .reverb || kind == .spaceEcho, depth: 1) }
                deck.padDown(CGPoint(x: 0.5, y: 0.8))
                let multi = await measure()
                deck.padUp()
                NSLog("CRATETEST three armed (LPF+REV+TAPE) at center-top: all %+.1f highs %+.1f, make-up %+.1f dB", multi.all - dry.all, multi.high - dry.high, deck.sampler.makeupDB)
                // Hold a spot for the snapshot.
                deck.latch = true
                deck.padDown(CGPoint(x: 0.68, y: 0.62))
                try? await Task.sleep(for: .seconds(5))
                deck.padUp()
                deck.latch = savedLatch
                deck.fx = savedFX
                let at = deck.seconds
                model.closePerformance()
                NSLog("CRATETEST hand back: deck at %.2f s, player at %.2f s, player playing %d, screen %@",
                      at, model.player.currentTime, model.player.isPlaying ? 1 : 0, "\(model.screen)")
                if model.player.isPlaying { model.player.toggle() }
                NSLog("CRATETEST perform done")
            }
        }
        // Debug aid: CRATE_RADIO=1 plays the cued track and opens the radio page.
        if env["CRATE_RADIO"] != nil {
            Task { @MainActor in
                let model = AppModel.shared
                NSLog("CRATETEST radio hook waiting for a cued track")
                while model.player.currentURL == nil { try? await Task.sleep(for: .milliseconds(200)) }
                NSLog("CRATETEST radio hook: cued, playing")
                if !model.player.isPlaying { model.player.toggle() }
                // (Opening the page during launch leaves the snapshot hook capturing stale content.)
                try? await Task.sleep(for: .seconds(2))
                model.showRadio()
                // CRATE_RADIO_TAKE=/folder: record a take (into a scratch folder), check the waveform,
                // scrub and playback meters, and leave it mid-recording then mid-playback for snapshots.
                if let folder = env["CRATE_RADIO_TAKE"] {
                    let deck = LiveDeck.shared, saved = deck.destination
                    deck.destination = URL(fileURLWithPath: folder)
                    try? await Task.sleep(for: .seconds(1))
                    deck.record()
                    try? await Task.sleep(for: .seconds(max(0, 9 - Date().timeIntervalSince(launched))))   // snapshot 1 mid-recording
                    let midWave = LiveRecorder.shared.waveform().count
                    try? await Task.sleep(for: .seconds(1))
                    deck.stop()
                    NSLog("CRATETEST take %.2f s, waveform %d points (mid-recording %d), expected ~%d",
                          deck.takeSeconds, deck.takeWave.count, midWave, Int(deck.takeSeconds * 44100 / 1024))
                    deck.scrub(to: 0.5)
                    NSLog("CRATETEST scrub to 0.5 while stopped: playhead %.2f s", deck.playheadSeconds)
                    deck.play()
                    try? await Task.sleep(for: .milliseconds(500))
                    NSLog("CRATETEST playing from scrub point: playhead %.2f s (take %.2f)", deck.playheadSeconds, deck.takeSeconds)
                    var peak: Float = 0
                    for _ in 0..<10 { peak = max(peak, deck.playbackLevels().left); try? await Task.sleep(for: .milliseconds(50)) }
                    NSLog("CRATETEST playback meter peak %.2f", peak)
                    deck.scrub(to: 0.2)
                    try? await Task.sleep(for: .milliseconds(200))
                    NSLog("CRATETEST scrub to 0.2 while playing: playhead %.2f s", deck.playheadSeconds)
                    deck.play()   // pause
                    let pausedAt = deck.playheadSeconds
                    try? await Task.sleep(for: .milliseconds(400))
                    NSLog("CRATETEST pause holds position: %.2f -> %.2f, state %@", pausedAt, deck.playheadSeconds, "\(deck.state)")
                    deck.play()   // resume, for snapshot 2
                    try? await Task.sleep(for: .seconds(max(0, 14 - Date().timeIntervalSince(launched))))
                    deck.stop()
                    deck.destination = saved
                    NSLog("CRATETEST take done")
                }
                // CRATE_RADIO_PAD=1: measure the sampler's input level at each pad setting, leave −10 up for a snapshot.
                if env["CRATE_RADIO_PAD"] != nil {
                    let deck = LiveDeck.shared, saved = deck.pad
                    try? await Task.sleep(for: .seconds(1))
                    var base = 0.0
                    for step in LiveDeck.padSteps {
                        deck.pad = step
                        try? await Task.sleep(for: .milliseconds(300))
                        var sum = 0.0, n = 0.0
                        let end = Date().addingTimeInterval(1.5)
                        while Date() < end {
                            let l = LiveRecorder.shared.levels()
                            sum += Double(l.left + l.right) / 2; n += 1
                            try? await Task.sleep(for: .milliseconds(40))
                        }
                        let avg = sum / max(n, 1)
                        if step == 0 { base = avg }
                        NSLog("CRATETEST pad -%d dB: input %.1f dB relative to no pad", step, 20 * log10(avg / base))
                    }
                    deck.pad = 10
                    NSLog("CRATETEST screen %@", "\(model.screen)")
                    if model.screen != .radio { model.showRadio() }
                    try? await Task.sleep(for: .seconds(max(0, 16 - Date().timeIntervalSince(launched))))
                    deck.pad = saved
                    NSLog("CRATETEST pad restored to %d", saved)
                }
                // CRATE_RADIO_STUDIO=/folder: test the EQ and the live recorder (saving takes into /folder),
                // then leave a demo EQ curve up for a snapshot and put everything back.
                if let testFolder = env["CRATE_RADIO_STUDIO"] {
                    let eq = MasterEQ.shared, deck = LiveDeck.shared, player = model.player
                    let savedVolume = player.volume, savedDestination = deck.destination
                    @MainActor func averageRMS(_ seconds: Double) async -> Double {
                        var sum = 0.0, n = 0.0
                        let end = Date().addingTimeInterval(seconds)
                        while Date() < end {
                            let st = player.meter.stereo()
                            sum += Double(st.left + st.right) / 2; n += 1
                            try? await Task.sleep(for: .milliseconds(50))
                        }
                        return sum / max(n, 1)
                    }
                    try? await Task.sleep(for: .seconds(1))
                    eq.flatten()
                    try? await Task.sleep(for: .milliseconds(300))
                    let flat = await averageRMS(2)
                    for b in 0..<20 { eq.setGain(b, -12) }
                    try? await Task.sleep(for: .milliseconds(300))
                    let cut = await averageRMS(2)
                    NSLog("CRATETEST EQ all bands -12 dB: level %.1f dB vs flat", 20 * log10(cut / flat))
                    eq.flatten()
                    player.volume = 0.9
                    try? await Task.sleep(for: .milliseconds(300))
                    let loud = await averageRMS(2)
                    player.volume = 0.2
                    try? await Task.sleep(for: .milliseconds(300))
                    let quiet = await averageRMS(2)
                    NSLog("CRATETEST recorder tap level at volume 0.2 vs 0.9: %.1f dB (should be ~0)", 20 * log10(quiet / loud))
                    player.volume = savedVolume
                    deck.destination = URL(fileURLWithPath: testFolder)
                    deck.record()
                    try? await Task.sleep(for: .seconds(3))
                    deck.stop()
                    NSLog("CRATETEST take length %.2f s, state %@", deck.takeSeconds, "\(deck.state)")
                    deck.play()
                    try? await Task.sleep(for: .milliseconds(600))
                    NSLog("CRATETEST playback state %@", "\(deck.state)")
                    deck.stop()
                    deck.save()
                    NSLog("CRATETEST saved: %@ | %@", deck.lastSaved?.lastPathComponent ?? "nothing", deck.status)
                    deck.save()
                    NSLog("CRATETEST second save: %@", deck.status)
                    // Demo curve for the snapshot, then put the user's settings back.
                    let smile: [Float] = [8, 7, 5.5, 4, 2.5, 1, 0, -1.5, -3, -4, -4, -3, -1.5, 0, 1.5, 3, 4.5, 6, 7, 7.5]
                    for (b, g) in smile.enumerated() { eq.setGain(b, g) }
                    deck.record()
                    let wait = 30.0 - Date().timeIntervalSince(launched)
                    try? await Task.sleep(for: .seconds(max(0, wait)))
                    deck.stop()
                    eq.flatten()
                    deck.destination = savedDestination
                    NSLog("CRATETEST restored EQ flat, volume %.2f, destination %@", player.volume, deck.destination.lastPathComponent)
                }
                // CRATE_RADIO_NEXT=1: skip to the next song at 11 s and 15 s after launch, to see the colourways change.
                if env["CRATE_RADIO_NEXT"] != nil {
                    for at in [11.0, 15.0] {
                        try? await Task.sleep(for: .seconds(max(0, at - Date().timeIntervalSince(launched))))
                        model.player.next()
                    }
                }
            }
        }
        // Debug aid: CRATE_CHOP=1 opens the current track in the chop station, auto-chops and maps it;
        // CRATE_CHOP_EXPORT=/folder then exports there without asking.
        if env["CRATE_CHOP"] != nil {
            Task { @MainActor in
                let model = AppModel.shared
                while model.player.currentURL == nil { try? await Task.sleep(for: .milliseconds(200)) }
                model.openChopForCurrent()
                guard let chop = model.chop else { return }
                while chop.buffer == nil { try? await Task.sleep(for: .milliseconds(200)) }
                if env["CRATE_CHOP_KEYLOCK"] != nil {
                    let savedLock = chop.keyLock
                    chop.seek(to: Int(30 * chop.sampleRate))
                    chop.keyLock = true
                    chop.pitch = 8
                    chop.togglePlay()
                    try? await Task.sleep(for: .milliseconds(500))
                    let a = chop.playhead
                    try? await Task.sleep(for: .seconds(2))
                    let speed = Double(chop.playhead - a) / chop.sampleRate / 2
                    let st = chop.sampler.meter.stereo()
                    NSLog("CRATETEST key lock on real music at +8%%: tape speed %.3f, output level L %.3f R %.3f, playing %d",
                          speed, st.left, st.right, chop.isPlaying ? 1 : 0)
                    try? await Task.sleep(for: .seconds(max(0, 11 - Date().timeIntervalSince(launched))))
                    chop.togglePlay()
                    chop.pitch = 0
                    chop.keyLock = savedLock
                    NSLog("CRATETEST key lock restored to %d", savedLock ? 1 : 0)
                    return
                }
                if env["CRATE_CHOP_AK"] != nil {
                    // Loop one bar, then compare the spectrum with each AudioKit effect off and fully on.
                    let saved = chop.fx
                    chop.seek(to: Int(30 * chop.sampleRate)); chop.loopBars = 1; chop.toggleLoop()
                    let bar = Double(chop.barFrames) / chop.sampleRate
                    @MainActor func measure() async -> (all: Double, low: Double, high: Double) {
                        try? await Task.sleep(for: .milliseconds(400))
                        var acc = [Double](repeating: 0, count: 20); var n = 0.0
                        let end = Date().addingTimeInterval(bar * 2)
                        while Date() < end {
                            let sp = chop.sampler.meter.spectrum()
                            for i in 0..<20 { acc[i] += Double(sp[i]) }
                            n += 1
                            try? await Task.sleep(for: .milliseconds(30))
                        }
                        let a = acc.map { $0 / max(n, 1) * 54 }   // back to dB-ish units
                        func avg(_ r: Range<Int>) -> Double { r.map { a[$0] }.reduce(0, +) / Double(r.count) }
                        return (avg(0..<20), avg(0..<5), avg(14..<20))
                    }
                    let dry = await measure()
                    for kind in [FXKind.hpf, .lpf, .lofi, .spaceEcho, .reverb] {
                        chop.fx[kind] = FXState(on: true, depth: 1)
                        let wet = await measure()
                        chop.fx[kind] = FXState(on: false, depth: 1)
                        NSLog("CRATETEST %@ full: all %+.1f  lows %+.1f  highs %+.1f (dB vs dry)", kind.label, wet.all - dry.all, wet.low - dry.low, wet.high - dry.high)
                    }
                    let back = await measure()
                    NSLog("CRATETEST all off again: all %+.1f lows %+.1f highs %+.1f", back.all - dry.all, back.low - dry.low, back.high - dry.high)
                    chop.fx = saved
                    chop.toggleLoop()
                    NSLog("CRATETEST done")
                    return
                }
                if env["CRATE_CHOP_LOOPCHECK"] != nil {
                    let m = chop.loopMemories.enumerated().map { "\($0.offset + 1)=" + ($0.element.map { "\($0.start)/\($0.length)/\($0.bars)b" } ?? "-") }
                    NSLog("CRATETEST restored memories: %@", m.joined(separator: " "))
                    return
                }
                if env["CRATE_CHOP_LOOPS"] != nil {
                    let sr = chop.sampleRate
                    @MainActor func line(_ label: String, _ ok: Bool, _ detail: String = "") { NSLog("CRATETEST %@ %@ %@", ok ? "PASS" : "FAIL", label, detail) }
                    // Loop A: 2 bars from 20 s, saved to memory 1.
                    chop.seek(to: Int(20 * sr)); chop.loopBars = 2; chop.toggleLoop()
                    let a = chop.loopRange!
                    line("loop A is 2 bars", abs(Double(a.count) - 2 * Double(chop.barFrames)) < 2, "\(a.count) frames")
                    chop.armLoopSave()
                    line("SAVE arms", chop.savingLoop)
                    chop.pressLoopMemory(0)
                    line("saved to memory 1, disarmed", chop.loopMemories[0]?.start == a.lowerBound && chop.loopMemories[0]?.bars == 2 && !chop.savingLoop)
                    // Loop B: 4 bars from 60 s, saved to memory 3.
                    chop.seek(to: Int(60 * sr)); chop.loopBars = 4; chop.toggleLoop()
                    let b = chop.loopRange!
                    line("loop B is 4 bars", abs(Double(b.count) - 4 * Double(chop.barFrames)) < 2)
                    chop.armLoopSave(); chop.pressLoopMemory(2)
                    line("saved to memory 3", chop.loopMemories[2]?.start == b.lowerBound && chop.loopMemories[2]?.bars == 4)
                    // Recall memory 1 while loop B plays.
                    chop.pressLoopMemory(0)
                    line("recall memory 1", chop.loopRange == a && chop.activeMemory == 0 && chop.loopBars == 2 && chop.isPlaying)
                    // Retrigger: let it run, then jump back to the top of the loop.
                    try? await Task.sleep(for: .seconds(1.5))
                    let before = chop.playhead
                    chop.retriggerLoop()
                    let after = chop.playhead
                    line("retrigger returns to loop start", before - a.lowerBound > Int(sr) && abs(after - a.lowerBound) < Int(0.2 * sr), "was +\(Double(before - a.lowerBound) / sr)s, now +\(Double(after - a.lowerBound) / sr)s")
                    line("retrigger keeps memory 1 active", chop.activeMemory == 0 && chop.loopRange == a)
                    // Empty slot recall does nothing; arming twice cancels.
                    chop.pressLoopMemory(4)
                    line("empty memory 5 leaves loop alone", chop.loopRange == a)
                    chop.armLoopSave(); chop.armLoopSave()
                    line("SAVE twice cancels", !chop.savingLoop)
                    // Changing the bars while looping drops the memory link.
                    chop.loopBars = 4
                    line("changing bars leaves memory", chop.activeMemory == nil && chop.loopRange?.lowerBound == a.lowerBound)
                    // Loop off, then retrigger restarts the last loop.
                    chop.toggleLoop()
                    line("loop off", !chop.isLooping)
                    chop.retriggerLoop()
                    line("retrigger after loop off restarts last loop", chop.isLooping)
                    chop.pressLoopMemory(2)
                    NSLog("CRATETEST memories: %@", chop.loopMemories.map { $0.map { "\($0.start)/\($0.bars)b" } ?? "-" }.joined(separator: " "))
                    let wait = 12.5 - Date().timeIntervalSince(launched)
                    try? await Task.sleep(for: .seconds(max(0, wait)))
                    chop.armLoopSave()   // so the second snapshot shows the armed keys
                    return
                }
                if env["CRATE_CHOP_TAPE"] != nil {
                    let sr = chop.sampleRate
                    @MainActor func speed(_ label: String) async {
                        let a = chop.playhead
                        try? await Task.sleep(for: .seconds(1))
                        NSLog("CRATETEST %@: %.2f s of audio per second", label, Double(chop.playhead - a) / sr)
                    }
                    chop.seek(to: Int(30 * sr))
                    chop.togglePlay()
                    try? await Task.sleep(for: .milliseconds(300))
                    await speed("forward")
                    chop.direction = -1
                    try? await Task.sleep(for: .milliseconds(100))
                    await speed("reverse")
                    chop.direction = 0
                    try? await Task.sleep(for: .milliseconds(100))
                    await speed("stopped (lever centre)")
                    chop.direction = 0.5
                    try? await Task.sleep(for: .milliseconds(100))
                    await speed("half forward")
                    chop.direction = 1
                    chop.pitchRange = 50
                    chop.pitch = 50
                    try? await Task.sleep(for: .milliseconds(100))
                    await speed("±50 range at +50%")
                    chop.pitch = 0
                    chop.fx[.stutter] = FXState(on: true, depth: 0.6)
                    await speed("tape under stutter (should still be ~1)")
                    chop.fx[.stutter] = FXState(on: false, depth: 0.6)
                    chop.loopBars = 1
                    chop.toggleLoop()
                    chop.direction = -1
                    let loop = chop.loopRange!
                    try? await Task.sleep(for: .seconds(Double(loop.count) / sr * 1.6))
                    NSLog("CRATETEST reverse loop: playhead inside loop %d", loop.contains(chop.playhead) ? 1 : 0)
                    chop.direction = -1
                    chop.togglePlay()
                    chop.seek(to: Int(0.3 * sr))
                    chop.togglePlay()
                    try? await Task.sleep(for: .seconds(1))
                    NSLog("CRATETEST reverse into the start: playing %d, playhead %d", chop.isPlaying ? 1 : 0, chop.playhead)
                    chop.direction = 1
                    chop.fx[.stutter] = FXState(on: true, depth: 0.6)
                    chop.direction = 0.6
                    return
                }
                if env["CRATE_CHOP_RACK"] != nil {
                    // Looper, pitch, skip and effects.
                    let bar = chop.barFrames
                    chop.seek(to: 10 * Int(chop.sampleRate))
                    chop.loopBars = 2
                    chop.toggleLoop()
                    let loop = chop.loopRange!
                    try? await Task.sleep(for: .seconds(Double(loop.count) / chop.sampleRate + 0.7))
                    let p = chop.playhead
                    NSLog("CRATETEST loop %d..<%d (%.2fs = 2 bars of %d) playhead after one pass %d inside %d", loop.lowerBound, loop.upperBound, Double(loop.count) / chop.sampleRate, bar, p, loop.contains(p) ? 1 : 0)
                    chop.toggleLoop()
                    chop.pitch = 4
                    let a = chop.playhead
                    try? await Task.sleep(for: .seconds(2))
                    let b = chop.playhead
                    NSLog("CRATETEST pitch +4%%: 2.0s wall clock advanced %.3fs of audio (expect ~2.08)", Double(b - a) / chop.sampleRate)
                    let before = chop.playhead
                    chop.skip(bars: 1)
                    NSLog("CRATETEST skip +1 bar moved %d (bar %d)", chop.playhead - before, bar)
                    chop.fx[.delay] = FXState(on: true, depth: 0.4)
                    chop.fx[.reverb] = FXState(on: true, depth: 0.7)
                    chop.fx[.lpf] = FXState(on: true, depth: 0.3)
                    chop.loopBars = 4
                    chop.toggleLoop()
                    return
                }
                if env["CRATE_CHOP_FLAGS"] != nil {
                    // A01 at the very start, A02 at 1 s, A03 at 2 s; then move A01's flag and A02's marker.
                    chop.pads = [:]
                    chop.markers = []
                    let sec = Int(chop.sampleRate)
                    chop.setStart(of: 0, at: 0)
                    chop.setStart(of: 1, at: sec)
                    chop.setStart(of: 2, at: 2 * sec)
                    @MainActor func dump(_ label: String) {
                        let p = chop.pads.sorted { $0.key < $1.key }.map { "\(chop.padName($0.key)) \($0.value.start)-\($0.value.end)" }.joined(separator: ", ")
                        NSLog("CRATETEST %@ | markers %@ | first slice starts %d | %@", label, chop.markers.map(String.init).joined(separator: ","), chop.slices.first?.lowerBound ?? -1, p)
                    }
                    dump("set")
                    chop.movePadStart(0, to: 4800)
                    dump("A01 flag dragged to 4800")
                    chop.movePadStart(0, to: 0)
                    dump("A01 flag dragged back to 0")
                    if let m = chop.markers.firstIndex(of: sec) { chop.moveMarker(m, to: sec + 2400) }
                    dump("marker 2 dragged +2400")
                    chop.zoomToFit()
                    chop.setView(width: 0.02, keeping: 0, at: 0)
                    return
                }
                if env["CRATE_CHOP_TAPS"] != nil {
                    // Simulates tap-to-chop: play, tap A01–A03 a second apart, then retrigger A01.
                    chop.pads = [:]
                    chop.markers = []
                    chop.togglePlay()
                    for pad in [0, 1, 2] {
                        try? await Task.sleep(for: .seconds(1))
                        chop.hitPad(pad)
                    }
                    try? await Task.sleep(for: .seconds(1))
                    chop.hitPad(0)
                    let before = chop.playhead
                    try? await Task.sleep(for: .milliseconds(600))
                    for (pad, slot) in chop.pads.sorted(by: { $0.key < $1.key }) {
                        NSLog("CRATETEST %@ start=%d end=%d", chop.padName(pad), slot.start, slot.end)
                    }
                    // Zoom and nudge: one pixel should shrink as we zoom in; A01 must keep ending where A02 starts.
                    chop.togglePlay()
                    chop.selectedPad = 1
                    chop.zoomToFit()
                    let fitStep = chop.framesPerPixel
                    var s0 = chop.pads[1]!.start
                    chop.nudge(pixels: 1)
                    NSLog("CRATETEST fit: frames/px=%.1f nudge+1px moved %d, A01.end==A02.start %d", fitStep, chop.pads[1]!.start - s0, chop.pads[0]!.end == chop.pads[1]!.start ? 1 : 0)
                    chop.setView(width: 0.002, keeping: Double(chop.pads[1]!.start) / Double(chop.frameCount), at: 0.5)
                    let zoomStep = chop.framesPerPixel
                    s0 = chop.pads[1]!.start
                    chop.nudge(pixels: -1)
                    NSLog("CRATETEST zoomed: frames/px=%.1f nudge-1px moved %d, A01.end==A02.start %d", zoomStep, chop.pads[1]!.start - s0, chop.pads[0]!.end == chop.pads[1]!.start ? 1 : 0)
                    s0 = chop.pads[1]!.start
                    chop.nudge(samples: 1)
                    NSLog("CRATETEST one sample: moved %d, A01.end==A02.start %d, step label %@", chop.pads[1]!.start - s0, chop.pads[0]!.end == chop.pads[1]!.start ? 1 : 0, chop.nudgeStepLabel)
                    chop.selection = chop.pads[1]!.start..<(chop.pads[1]!.start + 20000)
                    NSLog("CRATETEST retrigger playhead=%d now=%d playing=%d", before, chop.playhead, chop.isPlaying ? 1 : 0)
                    return
                }
                chop.autoChop()
                while chop.isBusy { try? await Task.sleep(for: .milliseconds(200)) }
                chop.autoMap()
                if let out = env["CRATE_CHOP_EXPORT"] { chop.exportSlices(to: URL(fileURLWithPath: out)) }
                // CRATE_CHOP_XPJ=/folder with CRATE_CHOP_TEMPLATE=/file.xpj builds an MPC project there.
                if let out = env["CRATE_CHOP_XPJ"], let template = env["CRATE_CHOP_TEMPLATE"] {
                    while chop.isBusy { try? await Task.sleep(for: .milliseconds(200)) }
                    chop.chooseMPCTemplate(from: URL(fileURLWithPath: template))
                    chop.exportMPCProject(to: URL(fileURLWithPath: out))
                }
            }
        }
        // Debug aid: CRATE_IMPORT=/x.zip CRATE_IMPORT_DEST=/folder imports into a scratch crate and reports
        // timing, the worst main-thread stall, damaged files, conversions and analysis.
        if let zip = env["CRATE_IMPORT"], let dest = env["CRATE_IMPORT_DEST"] {
            Task { @MainActor in
                let model = AppModel.shared, library = model.library
                try? await Task.sleep(for: .seconds(2))
                model.destination = URL(fileURLWithPath: dest)
                try? await Task.sleep(for: .seconds(1))
                var worst = 0.0
                var last = Date()
                let monitor = Task { @MainActor in
                    while !Task.isCancelled {
                        try? await Task.sleep(for: .milliseconds(10))
                        let now = Date()
                        worst = max(worst, now.timeIntervalSince(last) - 0.01)
                        last = now
                    }
                }
                @MainActor func importOnce() async -> Double {
                    let t0 = Date()
                    model.importItems([URL(fileURLWithPath: zip)])
                    while !model.phase.isBusy { try? await Task.sleep(for: .milliseconds(20)) }
                    var lastPhase = ""
                    while model.phase.isBusy {
                        let now = "\(model.phase)"
                        if now.prefix(12) != lastPhase.prefix(12) { NSLog("CRATETEST phase %@", now); lastPhase = now }
                        try? await Task.sleep(for: .milliseconds(50))
                    }
                    return Date().timeIntervalSince(t0)
                }
                let took = await importOnce()
                NSLog("CRATETEST import took %.1f s · %@ · damaged: %@ · worst main-thread stall %.0f ms",
                      took, model.note ?? "-", model.damaged.joined(separator: ", "), worst * 1000)
                let files = (try? FileManager.default.contentsOfDirectory(atPath: dest)) ?? []
                let exts = Dictionary(grouping: files, by: { ($0 as NSString).pathExtension.lowercased() }).mapValues(\.count)
                NSLog("CRATETEST landed %d files: %@", files.count, exts.map { "\($0.key)=\($0.value)" }.sorted().joined(separator: " "))
                // A converted file should be as long as its source.
                if let m4a = files.first(where: { $0.hasSuffix(".wav") && !$0.contains("[") }),
                   let f = try? AVAudioFile(forReading: URL(fileURLWithPath: dest).appendingPathComponent(m4a)) {
                    NSLog("CRATETEST sample %@: %.0f Hz, %d ch, %.1f s", m4a, f.fileFormat.sampleRate, f.fileFormat.channelCount,
                          Double(f.length) / f.fileFormat.sampleRate)
                }
                worst = 0
                let a0 = Date()
                while library.tracks.isEmpty || library.analysisProgress != nil
                        || library.tracks.contains(where: { library.result(for: $0.url)?.loudness == nil && !library.isUnreadable($0.url) }) {
                    if Date().timeIntervalSince(a0) > 300 { break }
                    try? await Task.sleep(for: .milliseconds(200))
                }
                NSLog("CRATETEST analysed %d tracks in %.1f s on %d cores · worst stall %.0f ms · unreadable %d",
                      library.tracks.count, Date().timeIntervalSince(a0), ProcessInfo.processInfo.activeProcessorCount,
                      worst * 1000, library.unreadable.count)
                let levels = library.tracks.compactMap { t -> (String, Double, Float)? in
                    guard let r = library.result(for: t.url), let l = r.loudness, let g = LevelMatch.gainDB(r) else { return nil }
                    return (t.info.title, l, g)
                }.sorted { $0.1 < $1.1 }
                if let quiet = levels.first, let loud = levels.last {
                    NSLog("CRATETEST quietest %@ %.1f LUFS → %+.1f dB · loudest %@ %.1f LUFS → %+.1f dB",
                          quiet.0, quiet.1, quiet.2, loud.0, loud.1, loud.2)
                }
                worst = 0
                let again = await importOnce()
                NSLog("CRATETEST re-import took %.1f s · %@ · worst stall %.0f ms", again, model.note ?? "-", worst * 1000)
                monitor.cancel()
                if model.player.isPlaying { model.player.toggle() }
                NSLog("CRATETEST import done")
            }
        }
        // Debug aid: CRATE_OPTO=1 (with CRATE_RADIO) measures the opto compressor off vs on, then restores it.
        // CRATE_OPTO=show leaves it on for a snapshot. Also checks loop fractions, eject and the pad overlay state.
        if let mode = env["CRATE_OPTO"] {
            Task { @MainActor in
                let model = AppModel.shared, opto = OptoComp.shared
                while !model.player.isPlaying { try? await Task.sleep(for: .milliseconds(200)) }
                try? await Task.sleep(for: .seconds(2))
                let saved = (opto.compress, opto.gain, opto.attack, opto.release, opto.ratio, opto.warm, opto.enabled)
                @MainActor func level() async -> (Double, Float) {
                    var sum = 0.0, n = 0.0
                    var maxGR: Float = 0
                    let end = Date().addingTimeInterval(1.5)
                    while Date() < end {
                        let s = model.player.meter.stereo()
                        sum += Double(s.left + s.right) / 2
                        n += 1
                        maxGR = max(maxGR, opto.reduction)
                        try? await Task.sleep(for: .milliseconds(30))
                    }
                    return (20 * log10(max(sum / n, 1e-6)), maxGR)
                }
                opto.enabled = false
                let off = await level()
                opto.compress = 0.8; opto.gain = 0; opto.attack = 5; opto.ratio = 10; opto.warm = true; opto.enabled = true
                let on = await level()
                opto.gain = 12
                let made = await level()
                NSLog("CRATETEST opto 80%% compress 10:1: level %+.1f dB, max reduction %.1f dB; +12 make-up %+.1f dB (vs off)",
                      on.0 - off.0, on.1, made.0 - off.0)
                if mode == "show" { return }
                (opto.compress, opto.gain, opto.attack, opto.release, opto.ratio, opto.warm, opto.enabled) = saved
                NSLog("CRATETEST opto restored")
            }
        }
        // Debug aid: CRATE_CHOP_FRAC=1 (with CRATE_CHOP) checks fractional-bar loops and the pad key overlay state.
        if env["CRATE_CHOP_FRAC"] != nil {
            Task { @MainActor in
                let model = AppModel.shared
                while model.player.currentURL == nil { try? await Task.sleep(for: .milliseconds(200)) }
                while model.chop?.buffer == nil { try? await Task.sleep(for: .milliseconds(200)) }
                guard let chop = model.chop else { return }
                try? await Task.sleep(for: .seconds(1.5))
                if env["CRATE_CHOP_FRAC"] == "keep" { model.screen = .chop; chop.loopBars = 4 }
                NSLog("CRATETEST overlay before: revealed %d", chop.revealedPads.count)
                chop.seek(to: Int(20 * chop.sampleRate))
                chop.loopBars = 0.25; chop.toggleLoop()
                let want = Int(Double(chop.barFrames) * 0.25)
                NSLog("CRATETEST 1/4 loop length %d (want %d) looping %d", chop.loopRange?.count ?? -1, want, chop.isLooping ? 1 : 0)
                chop.armLoopSave(); chop.pressLoopMemory(0)
                chop.loopBars = 2
                chop.pressLoopMemory(0)
                NSLog("CRATETEST recalled memory: %@ bars %.4f len %d", barLabel(chop.loopMemories[0]?.bars ?? 0), chop.loopMemories[0]?.bars ?? 0, chop.loopRange?.count ?? -1)
                if env["CRATE_CHOP_FRAC"] != "keep" { chop.hitPad(3) }
                NSLog("CRATETEST overlay after pad 4: revealed %@", chop.revealedPads.sorted().description)
                // Back-compat: an old save has integer bars.
                let old = try? JSONDecoder().decode(LoopMemory.self, from: Data(#"{"start":10,"length":20,"bars":4}"#.utf8))
                NSLog("CRATETEST old save decodes: %d", old?.bars == 4 ? 1 : 0)
                if env["CRATE_CHOP_FRAC"] == "keep" { return }
                model.showRadio()
                model.ejectToCrate()
                NSLog("CRATETEST eject → screen %@", String(describing: model.screen))
            }
        }
        // Debug aid: CRATE_V13=1 checks REW / FF on the library player, VU needle range at two volumes, MMC parsing,
        // and the encoder / pitch / transport MIDI targets on Chop. Restores volume and mappings.
        if env["CRATE_V13"] != nil {
            Task { @MainActor in
                let model = AppModel.shared, player = model.player, hub = MIDIHub.shared
                while !player.isPlaying { try? await Task.sleep(for: .milliseconds(200)) }
                player.seek(to: 60 / max(player.duration, 1))
                try? await Task.sleep(for: .seconds(2))
                // REW
                let t0 = player.currentTime
                model.transport(.rewind)
                try? await Task.sleep(for: .seconds(2.5))
                let t1 = player.currentTime
                try? await Task.sleep(for: .seconds(1))
                let t2 = player.currentTime
                NSLog("CRATETEST rewind: direction %.0f, time %.1f → %.1f → %.1f (%.2f s/s)", player.direction, t0, t1, t2, t1 - t2)
                model.transport(.forward)
                try? await Task.sleep(for: .seconds(1))
                let f1 = player.currentTime
                try? await Task.sleep(for: .seconds(1))
                NSLog("CRATETEST forward: direction %.0f, time %.1f → %.1f (%.2f s/s)", player.direction, f1, player.currentTime, player.currentTime - f1)
                // VU needles at two volumes
                let savedVolume = player.volume
                for v in [0.8, 0.3] {
                    player.volume = v
                    var arcs: [Double] = []
                    let end = Date().addingTimeInterval(4)
                    while Date() < end {
                        let rms = player.meter.stereo()
                        let g = v
                        arcs.append(DeckMotion.arc(dB: 20 * log10(max(Double(max(rms.left, rms.right)) * g, 1e-6)) - DeckMotion.referenceDB))
                        try? await Task.sleep(for: .milliseconds(100))
                    }
                    NSLog("CRATETEST VU at volume %.1f: needle min %.2f mean %.2f max %.2f (1.06 = pinned)", v, arcs.min() ?? 0, arcs.reduce(0, +) / Double(max(arcs.count, 1)), arcs.max() ?? 0)
                }
                player.volume = savedVolume
                // MMC bytes through the parser
                NSLog("CRATETEST MMC self-check follows from the live parse path")
                // Chop MIDI
                model.openChopForCurrent()
                guard let chop = model.chop else { return }
                while chop.buffer == nil { try? await Task.sleep(for: .milliseconds(200)) }
                model.screen = .chop
                let savedFX = chop.fx
                chop.selectedFX = .delay
                chop.fx[.delay] = FXState(on: true, depth: 0.5)
                hub.learning = .encoder; hub.inject(.cc(number: 20, value: 1))
                hub.inject(.cc(number: 20, value: 5))
                NSLog("CRATETEST encoder +5: delay depth %.2f (want 0.55), reverb untouched %.2f", chop.fx[.delay]?.depth ?? -1, chop.fx[.reverb]?.depth ?? -1)
                hub.inject(.cc(number: 20, value: 125))
                NSLog("CRATETEST encoder −3: delay depth %.2f (want 0.52)", chop.fx[.delay]?.depth ?? -1)
                hub.learning = .encoderPress; hub.inject(.noteOn(note: 60, velocity: 100))
                let before = chop.fx[.delay]?.on ?? false
                hub.inject(.noteOn(note: 60, velocity: 100))
                NSLog("CRATETEST encoder click: delay on %d → %d", before ? 1 : 0, chop.fx[.delay]?.on == true ? 1 : 0)
                chop.selectedFX = .echo
                hub.inject(.cc(number: 20, value: 10))
                NSLog("CRATETEST selection moved: echo depth %.2f, delay depth %.2f", chop.fx[.echo]?.depth ?? -1, chop.fx[.delay]?.depth ?? -1)
                hub.learning = .pitch; hub.inject(.cc(number: 21, value: 64))
                hub.inject(.cc(number: 21, value: 127))
                let top = chop.pitch
                hub.inject(.cc(number: 21, value: 0))
                let bottom = chop.pitch
                hub.inject(.cc(number: 21, value: 64))
                NSLog("CRATETEST pitch slider: 127 → %+.1f, 0 → %+.1f, 64 → %+.1f (range ±%d)", top, bottom, chop.pitch, chop.pitchRange)
                hub.inject(.transport(.play))
                try? await Task.sleep(for: .milliseconds(300))
                let playing = chop.isPlaying
                hub.inject(.transport(.rewind))
                try? await Task.sleep(for: .milliseconds(300))
                NSLog("CRATETEST MMC play → %d, rewind direction %.0f", playing ? 1 : 0, chop.direction)
                hub.inject(.transport(.stop))
                NSLog("CRATETEST MMC stop → playing %d; last message '%@'", chop.isPlaying ? 1 : 0, hub.lastMessage)
                chop.fx = savedFX; chop.direction = 1; chop.pitch = 0
                hub.clearLearned()
                NSLog("CRATETEST v13 done")
            }
        }
        // Debug aid: CRATE_V14=1: FF at 1.5x, then raw MIDI over a virtual CoreMIDI port (CC 96/97, NRPN, program change,
        // SysEx, aftertouch) learned onto the encoder / encoder click / stop, and the monitor log.
        if env["CRATE_V14"] != nil {
            Task { @MainActor in
                let model = AppModel.shared, player = model.player, hub = MIDIHub.shared
                func wait(_ ms: Int = 200) async { try? await Task.sleep(for: .milliseconds(ms)) }
                while !player.isPlaying { await wait() }
                player.seek(to: 40 / max(player.duration, 1))
                await wait(1500)
                model.transport(.forward)
                await wait(500)
                let a = player.currentTime
                await wait(2000)
                NSLog("CRATETEST FF: speed %.1f, %.2f s/s", player.speed, (player.currentTime - a) / 2)
                model.transport(.play)
                await wait(500)
                let b = player.currentTime
                await wait(2000)
                NSLog("CRATETEST PLAY after FF: speed %.1f, %.2f s/s", player.speed, (player.currentTime - b) / 2)

                var client = MIDIClientRef()
                MIDIClientCreateWithBlock("DSS1 Test" as CFString, &client, nil)
                var source = MIDIEndpointRef()
                MIDISourceCreateWithProtocol(client, "DSS1 Test MPC" as CFString, ._1_0, &source)
                func sendWords(_ words: [UInt32]) {
                    var list = MIDIEventList()
                    let packet = MIDIEventListInit(&list, ._1_0)
                    var w = words
                    _ = MIDIEventListAdd(&list, MemoryLayout<MIDIEventList>.size, packet, 0, w.count, &w)
                    MIDIReceivedEventList(source, &list)
                }
                func send(_ status: UInt32, _ d1: UInt32, _ d2: UInt32 = 0) { sendWords([(0x2 << 28) | (status << 16) | (d1 << 8) | d2]) }
                /// A complete 7-bit SysEx of up to 6 bytes (without F0 / F7).
                func sendSysex(_ bytes: [UInt32]) {
                    var b = bytes + Array(repeating: 0, count: 6 - bytes.count)
                    let w0: UInt32 = (0x3 << 28) | (0 << 20) | (UInt32(bytes.count) << 16) | (b[0] << 8) | b[1]
                    let w1: UInt32 = (b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5]
                    b = []
                    sendWords([w0, w1])
                }
                hub.refresh()
                await wait(500)
                let savedChoice = hub.choice
                if let mine = hub.sources.first(where: { $0.name.contains("DSS1 Test MPC") }) { hub.choice = .device(mine.id) }
                await wait(300)
                NSLog("CRATETEST devices: %@ · listening to %@", hub.sources.map { "\($0.name)[\($0.endpoints.count)]" }.joined(separator: ", "), hub.connected?.name ?? "nothing")

                model.openChopForCurrent()
                guard let chop = model.chop else { return }
                while chop.buffer == nil { await wait(100) }
                model.screen = .chop
                let savedFX = chop.fx
                chop.selectedFX = .delay
                chop.fx[.delay] = FXState(on: true, depth: 0.5)
                // Data increment / decrement.
                hub.learning = .encoder
                send(0xB0, 96, 1); await wait()
                for _ in 0..<5 { send(0xB0, 96, 1) }
                await wait()
                for _ in 0..<2 { send(0xB0, 97, 1) }
                await wait()
                NSLog("CRATETEST CC96 ×5, CC97 ×2: delay depth %.2f (want 0.53)", chop.fx[.delay]?.depth ?? -1)
                // NRPN.
                hub.clearLearned()
                hub.learning = .encoder
                send(0xB0, 99, 0); send(0xB0, 98, 5); send(0xB0, 96, 1); await wait()
                for _ in 0..<3 { send(0xB0, 96, 1) }
                await wait()
                NSLog("CRATETEST NRPN 5 +3: delay depth %.2f (want 0.56) learned %@", chop.fx[.delay]?.depth ?? -1,
                      hub.learned.keys.map { "\($0.kind.rawValue) \($0.number)" }.joined(separator: ","))
                // Program change as the encoder click.
                hub.learning = .encoderPress
                send(0xC0, 5); await wait()
                let wasOn = chop.fx[.delay]?.on ?? false
                send(0xC0, 5); await wait()
                NSLog("CRATETEST program 5 click: delay on %d → %d", wasOn ? 1 : 0, chop.fx[.delay]?.on == true ? 1 : 0)
                // SysEx button learned as STOP; MMC play works unmapped.
                sendSysex([0x7F, 0x7F, 0x06, 0x02]); await wait(400)
                let playing = chop.isPlaying
                hub.learning = .transport(.stop)
                sendSysex([0x47, 0x7F, 0x01, 0x10]); await wait()
                sendSysex([0x47, 0x7F, 0x01, 0x10]); await wait()
                NSLog("CRATETEST MMC play → %d; learned SysEx stop → playing %d", playing ? 1 : 0, chop.isPlaying ? 1 : 0)
                send(0xD0, 64); send(0xA0, 36, 20); await wait()
                let tail = hub.log.suffix(4).map { "\($0.hex) | \($0.meaning) | \($0.action)" }.joined(separator: " ; ")
                NSLog("CRATETEST log %d entries, last: %@", hub.log.count, tail)
                NSLog("CRATETEST log text starts: %@", String(hub.logText.prefix(160)))
                chop.fx = savedFX
                hub.clearLearned()
                hub.choice = savedChoice
                MIDIEndpointDispose(source)
                NSLog("CRATETEST v14 done")
            }
        }
        // Debug aid: CRATE_SERATO=1 sets pads 1–8 and two loop memories on the cued track, exports for Serato,
        // and logs what it expects so a separate decoder can check the file.
        if env["CRATE_SERATO"] != nil {
            Task { @MainActor in
                let model = AppModel.shared
                while model.player.currentURL == nil { try? await Task.sleep(for: .milliseconds(200)) }
                model.openChopForCurrent()
                guard let chop = model.chop else { return }
                while chop.buffer == nil { try? await Task.sleep(for: .milliseconds(200)) }
                model.screen = .chop
                let rate = chop.sampleRate
                chop.bank = 0
                for p in 0..<8 { chop.setStart(of: p, at: Int(Double(10 + p * 7) * rate) + 123) }
                for (slot, sec, bars) in [(0, 20.0, 2.0), (1, 45.5, 0.25)] {
                    if chop.isLooping { chop.toggleLoop() }
                    chop.seek(to: Int(sec * rate)); chop.loopBars = bars; chop.toggleLoop()
                    chop.armLoopSave(); chop.pressLoopMemory(slot)
                }
                if chop.isLooping { chop.toggleLoop() }
                if chop.isPlaying { chop.togglePlay() }
                for p in 0..<8 {
                    let f = chop.pads[p]!.start
                    NSLog("CRATETEST expect cue %d %d %@", p, Int((Double(f) / rate * 1000).rounded()), chop.padName(p))
                }
                for (i, m) in chop.loopMemories.enumerated() {
                    guard let m else { continue }
                    NSLog("CRATETEST expect loop %d %d %d %@", i, Int((Double(m.start) / rate * 1000).rounded()),
                          Int((Double(m.start + m.length) / rate * 1000).rounded()), barLabel(m.bars))
                }
                chop.exportSerato()
                while chop.lastSeratoExport == nil { try? await Task.sleep(for: .milliseconds(200)) }
                NSLog("CRATETEST source %@", chop.track.url.path)
                NSLog("CRATETEST exported %@", chop.lastSeratoExport!.path)
                NSLog("CRATETEST status %@", chop.status ?? "")
                try? await Task.sleep(for: .seconds(1.5))
                NSLog("CRATETEST serato done")
            }
        }
        // Debug aid: CRATE_COMP=1 (with CRATE_RADIO) measures the library compressor off vs on, then leaves
        // it on with a heavy setting for a snapshot and puts the settings back.
        if env["CRATE_COMP"] != nil {
            Task { @MainActor in
                let model = AppModel.shared, comp = LibraryCompressor.shared
                while !model.player.isPlaying { try? await Task.sleep(for: .milliseconds(200)) }
                try? await Task.sleep(for: .seconds(2))
                let saved = (comp.threshold, comp.gain, comp.attack, comp.enabled)
                @MainActor func level() async -> (Double, Float) {
                    var sum = 0.0, n = 0.0
                    var maxGR: Float = 0
                    let end = Date().addingTimeInterval(1.5)
                    while Date() < end {
                        let s = model.player.meter.stereo()
                        sum += Double(s.left + s.right) / 2
                        n += 1
                        maxGR = max(maxGR, comp.reduction)
                        try? await Task.sleep(for: .milliseconds(30))
                    }
                    return (20 * log10(max(sum / n, 1e-6)), maxGR)
                }
                comp.enabled = false
                let off = await level()
                comp.threshold = -30; comp.gain = 0; comp.attack = 5; comp.enabled = true
                let on = await level()
                comp.gain = 12
                let made = await level()
                NSLog("CRATETEST compressor −30 dB: level %+.1f dB, max reduction %.1f dB; with +12 make-up %+.1f dB (vs off)",
                      on.0 - off.0, on.1, made.0 - off.0)
                try? await Task.sleep(for: .seconds(4))
                (comp.threshold, comp.gain, comp.attack, comp.enabled) = saved
                NSLog("CRATETEST compressor restored")
            }
        }
        // Debug aid: CRATE_MIDI=1 plays a virtual "DSS1 Test MPK" into the app over CoreMIDI and checks
        // pads, knobs, MIDI Learn and the joystick on the chop and performance pages.
        if env["CRATE_MIDI"] != nil {
            Task { @MainActor in
                let model = AppModel.shared, hub = MIDIHub.shared, deck = model.performance
                var client = MIDIClientRef()
                MIDIClientCreateWithBlock("DSS1 Test" as CFString, &client, nil)
                var source = MIDIEndpointRef()
                MIDISourceCreateWithProtocol(client, "DSS1 Test MPK" as CFString, ._1_0, &source)
                func send(_ status: UInt32, _ d1: UInt32, _ d2: UInt32) {
                    var list = MIDIEventList()
                    let packet = MIDIEventListInit(&list, ._1_0)
                    let word: UInt32 = (0x2 << 28) | (status << 16) | (d1 << 8) | d2
                    var w = word
                    _ = MIDIEventListAdd(&list, MemoryLayout<MIDIEventList>.size, packet, 0, 1, &w)
                    MIDIReceivedEventList(source, &list)
                }
                func wait(_ ms: Int = 250) async { try? await Task.sleep(for: .milliseconds(ms)) }
                while model.player.currentURL == nil { await wait(200) }
                await wait(1000)
                hub.refresh()
                await wait(500)
                let savedChoice = hub.choice
                if let mine = hub.sources.first(where: { $0.name.contains("DSS1 Test MPK") }) { hub.choice = .device(mine.id) }
                await wait(300)
                NSLog("CRATETEST midi sources: %@ · listening to %@", hub.sources.map(\.name).joined(separator: ", "), hub.connected?.name ?? "nothing")

                model.openChopForCurrent()
                guard let chop = model.chop else { return }
                while chop.buffer == nil { await wait(100) }
                let hits = chop.hit.count
                send(0x90, 36, 100); await wait(); send(0x80, 36, 0)
                NSLog("CRATETEST note 36 → pad hit %@ (%d new hit)", chop.padName(chop.hit.pad), chop.hit.count - hits)
                send(0x90, 53, 100); await wait()
                NSLog("CRATETEST note 53 → pad %@, bank %d", chop.padName(chop.hit.pad), chop.bank)
                send(0xB0, 70, 100); await wait()
                NSLog("CRATETEST CC 70 = 100 → DELAY on %d depth %.2f", chop.fx[.delay]?.on == true ? 1 : 0, chop.fx[.delay]?.depth ?? -1)
                hub.learning = .fxDepth(.reverb)
                send(0xB0, 20, 10); await wait()
                send(0xB0, 20, 64); await wait()
                NSLog("CRATETEST learned CC 20 → %@ · reverb depth %.2f", hub.learned[MIDIControl(kind: .cc, number: 20)]?.label ?? "nothing", chop.fx[.reverb]?.depth ?? -1)
                if chop.isPlaying { chop.togglePlay() }

                model.closeChop()
                await wait(400)
                let savedFX = deck.fx, savedLatch = deck.latch
                model.showPerformance()
                while deck.duration == 0 { await wait(100) }
                let before = deck.fx[.delay]?.on ?? false
                send(0x90, 36, 90); await wait()
                NSLog("CRATETEST perform note 36 → DELAY armed %d → %d", before ? 1 : 0, deck.fx[.delay]?.on == true ? 1 : 0)
                send(0xE0, 0x7F, 0x7F); send(0xB0, 1, 100); await wait()
                NSLog("CRATETEST joystick right/up → pad (%.2f, %.2f) engaged %d", deck.point?.x ?? -1, deck.point?.y ?? -1, deck.engaged ? 1 : 0)
                send(0xE0, 0, 0x40); send(0xB0, 1, 0); await wait()
                NSLog("CRATETEST joystick centred → engaged %d", deck.engaged ? 1 : 0)

                hub.clearLearned()
                deck.fx = savedFX
                deck.latch = savedLatch
                hub.choice = savedChoice
                model.closePerformance()
                if model.player.isPlaying { model.player.toggle() }
                MIDIEndpointDispose(source)
                NSLog("CRATETEST midi done")
            }
        }
        // Debug aid: CRATE_SPEED=1 reports the hardware buffer and how long opening Chop takes, cold and cached.
        if env["CRATE_SPEED"] != nil {
            Task { @MainActor in
                let model = AppModel.shared
                while model.player.currentURL == nil { try? await Task.sleep(for: .milliseconds(200)) }
                if !model.player.isPlaying { model.player.toggle() }
                try? await Task.sleep(for: .seconds(1))
                NSLog("CRATETEST output buffer %d frames", model.player.bufferFrames)
                if model.player.isPlaying { model.player.toggle() }
                var times: [Double] = []
                for _ in 0..<2 {
                    let t0 = Date()
                    model.openChopForCurrent()
                    while model.chop?.buffer == nil { try? await Task.sleep(for: .milliseconds(10)) }
                    times.append(Date().timeIntervalSince(t0) * 1000)
                    NSLog("CRATETEST chop opened in %.0f ms", times.last!)
                    model.closeChop()
                    try? await Task.sleep(for: .milliseconds(500))
                }
                NSLog("CRATETEST open chop: cold %.0f ms, cached %.0f ms", times[0], times[1])
                NSLog("CRATETEST speed done")
            }
        }
        guard let path = env["CRATE_SNAPSHOT"] else { return }
        let delays = (env["CRATE_SNAPSHOT_DELAY"] ?? "2.5").split(separator: ",").compactMap { Double($0) }
        for (n, delay) in delays.enumerated() {
            Task { @MainActor in
                try? await Task.sleep(for: .seconds(delay))
                guard let window = NSApp.windows.first(where: { $0.isVisible && $0.frame.width > 600 }),
                      let image = CGWindowListCreateImage(.null, .optionIncludingWindow, CGWindowID(window.windowNumber),
                                                          [.boundsIgnoreFraming, .bestResolution]) else {
                    NSLog("CRATETEST snapshot: no window")
                    return
                }
                let rep = NSBitmapImageRep(cgImage: image)
                // Also the view's own rendering (works with the screen asleep; misses Metal-drawn layers).
                if let view = window.contentView, let own = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                    view.cacheDisplay(in: view.bounds, to: own)
                    try? own.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: "\(path)-\(n + 1)-view.png"))
                }
                do {
                    try rep.representation(using: .png, properties: [:])?
                        .write(to: URL(fileURLWithPath: "\(path)-\(n + 1).png"))
                    NSLog("CRATETEST snapshot %d written (%d×%d)", n + 1, image.width, image.height)
                } catch {
                    NSLog("CRATETEST snapshot failed: %@", "\(error)")
                }
            }
        }
    }
}
#endif
