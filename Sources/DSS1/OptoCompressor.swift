import AVFoundation
import AudioToolbox
import SwiftUI

/// A tube-style opto leveling amplifier in the spirit of the Tube-Tech CL 1B: a soft-knee compressor whose
/// release depends on what it has just been doing (fast for a short touch, slow after a long squeeze, like a
/// photocell), followed by gentle even-harmonic tube warmth.
final class OptoUnit: AUAudioUnit {
    /// Settings read by the render thread. Plain floats: a torn read is harmless.
    struct Settings {
        var enabled: Float = 0
        var thresholdDB: Float = -18
        var ratio: Float = 4
        var attackMS: Float = 10
        var releaseMS: Float = 500
        var makeupDB: Float = 0
        var warmth: Float = 0
    }

    final class Shared {
        var settings = Settings()
        var reductionDB: Float = 0
        var rate: Float = 48000
    }

    let shared = Shared()
    private var inputBus: AUAudioUnitBus!
    private var outputBus: AUAudioUnitBus!
    private var inputBusArray: AUAudioUnitBusArray!
    private var outputBusArray: AUAudioUnitBusArray!
    private var sampleRate: Double = 48000

    static let description = AudioComponentDescription(componentType: kAudioUnitType_Effect,
                                                       componentSubType: 0x6F707461, // 'opta'
                                                       componentManufacturer: 0x44535331, // 'DSS1'
                                                       componentFlags: 0, componentFlagsMask: 0)
    static let register: Void = {
        AUAudioUnit.registerSubclass(OptoUnit.self, as: description, name: "DSS1: Opto Compressor", version: 1)
    }()

    override init(componentDescription: AudioComponentDescription, options: AudioComponentInstantiationOptions = []) throws {
        let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
        try super.init(componentDescription: componentDescription, options: options)
        inputBus = try AUAudioUnitBus(format: format)
        outputBus = try AUAudioUnitBus(format: format)
        inputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .input, busses: [inputBus])
        outputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
        maximumFramesToRender = 4096
    }

    override var inputBusses: AUAudioUnitBusArray { inputBusArray }
    override var outputBusses: AUAudioUnitBusArray { outputBusArray }

    override func allocateRenderResources() throws {
        sampleRate = outputBus.format.sampleRate
        shared.rate = Float(sampleRate)
        try super.allocateRenderResources()
    }

    override var internalRenderBlock: AUInternalRenderBlock {
        let shared = self.shared
        // Detector state: the dB level followed, and two release memories.
        var env: Float = 0          // smoothed gain reduction in dB (positive)
        var squeeze: Float = 0      // how long and how hard it has been compressing (0…1)

        return { flags, stamp, frames, _, output, _, pull in
            guard let pull else { return kAudioUnitErr_NoConnection }
            let status = pull(flags, stamp, frames, 0, output)
            if status != noErr { return status }
            let s = shared.settings
            let rate = shared.rate
            let list = UnsafeMutableAudioBufferListPointer(output)
            guard s.enabled > 0.5, let first = list.first, first.mData != nil else {
                shared.reductionDB = 0
                return noErr
            }
            let n = Int(frames)
            let attack = exp(-1 / (max(s.attackMS, 0.05) * 0.001 * rate))
            let makeup = powf(10, s.makeupDB / 20)
            let drive = 1 + 1.5 * s.warmth
            let knee: Float = 6
            let slope = 1 - 1 / s.ratio
            var peakReduction: Float = 0
            for i in 0..<n {
                var level: Float = 0
                for b in list { level = max(level, abs(b.mData!.assumingMemoryBound(to: Float.self)[i])) }
                let db = 20 * log10f(max(level, 1e-6))
                // Soft knee: how far over the threshold, eased in across the knee.
                let over = db - s.thresholdDB
                let want: Float
                if over <= -knee / 2 { want = 0 }
                else if over >= knee / 2 { want = over * slope }
                else { let t = over + knee / 2; want = slope * t * t / (2 * knee) }
                // Opto release: the longer it has been squeezing, the slower it lets go.
                let releaseTime = s.releaseMS * 0.001 * (0.15 + 1.6 * squeeze)
                let release = exp(-1 / (max(releaseTime, 0.005) * rate))
                env = want > env ? attack * env + (1 - attack) * want : release * env + (1 - release) * want
                squeeze += ((want > 0.5 ? 1 : 0) - squeeze) * (want > 0.5 ? 1 : 0.1) / (rate * 1.2)
                squeeze = min(max(squeeze, 0), 1)
                let gain = powf(10, -env / 20)
                for b in list {
                    let p = b.mData!.assumingMemoryBound(to: Float.self)
                    var v = p[i] * gain
                    if s.warmth > 0 {
                        // Asymmetric soft clip: adds second harmonic, tames peaks.
                        let x = v * drive + 0.08 * s.warmth * v * v
                        v = tanhf(x) / tanhf(drive)
                    }
                    p[i] = v * makeup
                }
                peakReduction = max(peakReduction, env)
            }
            shared.reductionDB = peakReduction
            return noErr
        }
    }
}

@MainActor
final class OptoComp: ObservableObject {
    static let shared = OptoComp()

    /// 0…1 ("compress"): how far the threshold sinks, −6 … −40 dB.
    @Published var compress: Float { didSet { save(); apply() } }
    /// Make-up gain in dB, 0…+24.
    @Published var gain: Float { didSet { save(); apply() } }
    /// Milliseconds, 0.1…100.
    @Published var attack: Float { didSet { save(); apply() } }
    /// Milliseconds, 50…5000.
    @Published var release: Float { didSet { save(); apply() } }
    @Published var ratio: Float { didSet { save(); apply() } }
    @Published var warm: Bool { didSet { save(); apply() } }
    @Published var enabled: Bool { didSet { save(); apply() } }

    static let attackRange: ClosedRange<Float> = 0.1...100
    static let releaseRange: ClosedRange<Float> = 50...5000
    static let gainRange: ClosedRange<Float> = 0...24
    static let ratios: [Float] = [2, 4, 10]

    private weak var optoUnit: OptoUnit?
    private let defaults = UserDefaults.standard

    private init() {
        compress = defaults.object(forKey: "optoCompress") as? Float ?? 0.4
        gain = defaults.object(forKey: "optoGain") as? Float ?? 3
        attack = defaults.object(forKey: "optoAttack") as? Float ?? 8
        release = defaults.object(forKey: "optoRelease") as? Float ?? 600
        ratio = defaults.object(forKey: "optoRatio") as? Float ?? 4
        warm = defaults.object(forKey: "optoWarm") as? Bool ?? true
        enabled = defaults.object(forKey: "optoEnabled") as? Bool ?? false
    }

    /// A fresh unit for an engine, as an AVAudioUnitEffect. The component is instantiated off the main thread
    /// (the synchronous initialiser can deadlock waiting for it) and waited for here.
    func makeEffect() -> AVAudioUnit {
        _ = OptoUnit.register
        let gate = DispatchSemaphore(value: 0)
        nonisolated(unsafe) var made: AVAudioUnit?
        DispatchQueue.global(qos: .userInitiated).async {
            AVAudioUnit.instantiate(with: OptoUnit.description, options: .loadInProcess) { unit, _ in
                made = unit
                gate.signal()
            }
        }
        _ = gate.wait(timeout: .now() + 5)
        let effect: AVAudioUnit = made ?? AVAudioUnitEffect(audioComponentDescription: AudioComponentDescription(
            componentType: kAudioUnitType_Effect, componentSubType: kAudioUnitSubType_Delay,
            componentManufacturer: kAudioUnitManufacturer_Apple, componentFlags: 0, componentFlagsMask: 0))
        optoUnit = effect.auAudioUnit as? OptoUnit
        if optoUnit == nil, let e = effect as? AVAudioUnitEffect { e.bypass = true }
        apply()
        return effect
    }

    /// Gain reduction in dB right now.
    var reduction: Float { enabled ? (optoUnit?.shared.reductionDB ?? 0) : 0 }

    private func apply() {
        guard let s = optoUnit?.shared else { return }
        s.settings = OptoUnit.Settings(enabled: enabled ? 1 : 0,
                                       thresholdDB: -6 - 34 * compress, ratio: ratio,
                                       attackMS: attack, releaseMS: release,
                                       makeupDB: gain, warmth: warm ? 0.5 : 0)
    }

    private func save() {
        defaults.set(compress, forKey: "optoCompress")
        defaults.set(gain, forKey: "optoGain")
        defaults.set(attack, forKey: "optoAttack")
        defaults.set(release, forKey: "optoRelease")
        defaults.set(ratio, forKey: "optoRatio")
        defaults.set(warm, forKey: "optoWarm")
        defaults.set(enabled, forKey: "optoEnabled")
    }
}
