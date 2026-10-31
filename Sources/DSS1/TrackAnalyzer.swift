import Accelerate
import AVFoundation

/// Reads a track once to find its length, tempo and key.
enum TrackAnalyzer {
    /// Bump when the analysis changes, so older cached results are redone.
    static let version = 2

    struct Result: Codable, Sendable {
        var duration: Double
        var bpm: Double?
        /// A competing 3:2 or 2:3 tempo (e.g. 80 vs 120) when the beat genuinely supports both.
        var altBPM: Double?
        /// e.g. "A min"
        var key: String?
        /// e.g. "8A", for harmonic mixing.
        var camelot: String?
        var version: Int?
        /// Integrated loudness in LUFS (K-weighted, gated) over the analysed slices.
        var loudness: Double?
        /// Sample peak in dBFS.
        var peak: Double?
    }

    private static let fftSize = 1024
    private static let hop = 256

    static func analyze(_ url: URL) -> Result? {
        guard let file = try? AVAudioFile(forReading: url) else { return nil }
        let format = file.processingFormat
        let sampleRate = format.sampleRate
        let duration = Double(file.length) / sampleRate

        // Three 30-second slices through the body of the track; their evidence is pooled.
        let slice = AVAudioFramePosition(sampleRate * 30)
        let starts: [AVAudioFramePosition] = file.length > slice * 3
            ? [0.15, 0.4, 0.65].map { AVAudioFramePosition(Double(file.length) * $0) }
            : [0]
        var onsets: [[Float]] = []
        var chroma = [Double](repeating: 0, count: 12)
        var meter = LoudnessMeter(sampleRate: sampleRate)
        for start in starts {
            let count = AVAudioFrameCount(min(file.length - start, slice))
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: count) else { continue }
            file.framePosition = start
            guard (try? file.read(into: buffer, frameCount: count)) != nil,
                  let channels = buffer.floatChannelData else { continue }
            let n = Int(buffer.frameLength)
            meter.add(buffer)
            var mono = Array(UnsafeBufferPointer(start: channels[0], count: n))
            for c in 1..<Int(format.channelCount) {
                vDSP.add(mono, UnsafeBufferPointer(start: channels[c], count: n), result: &mono)
            }
            onsets.append(onsetEnvelope(mono))
            let part = pitchClasses(mono, sampleRate: sampleRate)
            for i in 0..<12 { chroma[i] += part[i] }
        }

        let tempo = tempo(from: onsets, sampleRate: sampleRate)
        let key = key(from: chroma)
        let level = meter.result
        return Result(duration: duration, bpm: tempo?.bpm, altBPM: tempo?.alt,
                      key: key?.name, camelot: key?.camelot, version: version,
                      loudness: level?.lufs, peak: level?.peak)
    }

    /// Loudness only, for tracks analysed before loudness was measured.
    static func loudness(_ url: URL) -> (lufs: Double, peak: Double)? {
        guard let file = try? AVAudioFile(forReading: url) else { return nil }
        let format = file.processingFormat
        let slice = AVAudioFramePosition(format.sampleRate * 30)
        let starts: [AVAudioFramePosition] = file.length > slice * 3
            ? [0.15, 0.4, 0.65].map { AVAudioFramePosition(Double(file.length) * $0) }
            : [0]
        var meter = LoudnessMeter(sampleRate: format.sampleRate)
        for start in starts {
            let count = AVAudioFrameCount(min(file.length - start, slice))
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: count) else { continue }
            file.framePosition = start
            guard (try? file.read(into: buffer, frameCount: count)) != nil else { return nil }
            meter.add(buffer)
        }
        return meter.result
    }

    // MARK: - Tempo

    /// Spectral flux: how much new energy appears in each short frame.
    static func onsetEnvelope(_ samples: [Float]) -> [Float] {
        let frames = (samples.count - fftSize) / hop
        guard frames > 0, let fft = vDSP.FFT(log2n: 10, radix: .radix2, ofType: DSPSplitComplex.self) else { return [] }
        let window = vDSP.window(ofType: Float.self, usingSequence: .hanningDenormalized, count: fftSize, isHalfWindow: false)
        let bins = fftSize / 4   // up to ~11 kHz
        var previous = [Float](repeating: 0, count: bins)
        var flux = [Float](repeating: 0, count: frames)

        samples.withUnsafeBufferPointer { p in
            for f in 0..<frames {
                let mags = magnitudes(UnsafeBufferPointer(rebasing: p[(f * hop)..<(f * hop + fftSize)]), window: window, fft: fft)
                var sum: Float = 0
                for b in 1..<bins {
                    let m = log(1 + mags[b])
                    sum += max(0, m - previous[b])
                    previous[b] = m
                }
                flux[f] = sum
            }
        }

        // Keep only peaks that rise above the local average (~0.4 s).
        let radius = 35
        var cleaned = [Float](repeating: 0, count: frames)
        for i in 0..<frames {
            let lo = max(0, i - radius), hi = min(frames, i + radius + 1)
            let local = flux[lo..<hi].reduce(0, +) / Float(hi - lo)
            cleaned[i] = max(0, flux[i] - local)
        }
        return cleaned
    }

    static func tempo(from onsets: [[Float]], sampleRate: Double) -> (bpm: Double, alt: Double?)? {
        let fps = sampleRate / Double(hop)
        let minLag = Int(fps * 60 / 200)
        let maxLag = Int(fps * 60 / 60)
        var scores = [Double](repeating: 0, count: maxLag * 2 + 1)
        var used = 0

        for onset in onsets where onset.count > maxLag * 3 {
            var local = [Double](repeating: 0, count: maxLag * 2 + 1)
            onset.withUnsafeBufferPointer { p in
                var energy: Float = 0
                vDSP_dotpr(p.baseAddress!, 1, p.baseAddress!, 1, &energy, vDSP_Length(onset.count))
                guard energy > 0 else { return }
                for lag in minLag...(maxLag * 2) {
                    var sum: Float = 0
                    vDSP_dotpr(p.baseAddress!, 1, p.baseAddress! + lag, 1, &sum, vDSP_Length(onset.count - lag))
                    local[lag] = Double(sum / energy) * Double(onset.count) / Double(onset.count - lag)
                }
            }
            for lag in minLag...(maxLag * 2) { scores[lag] += local[lag] }
            used += 1
        }
        guard used > 0 else { return nil }

        // How well a beat spacing lines up with itself (and with the bar above it).
        func support(_ lag: Int) -> Double {
            guard lag >= minLag, lag <= maxLag else { return 0 }
            return max(0, scores[lag] + 0.5 * scores[lag * 2])
        }

        var best = minLag
        var bestScore = -Double.infinity
        for lag in minLag...maxLag {
            let bpm = 60 * fps / Double(lag)
            // Gentle preference for 75–150 BPM to avoid half/double-time mistakes.
            let prior = exp(-0.5 * pow(log2(bpm / 105) / 0.9, 2))
            let score = support(lag) * prior
            if score > bestScore {
                bestScore = score
                best = lag
            }
        }

        var lag = Double(best)
        if best > minLag && best < maxLag {
            let a = scores[best - 1], b = scores[best], c = scores[best + 1]
            let denom = a - 2 * b + c
            if denom != 0 { lag += 0.5 * (a - c) / denom }
        }
        var bpm = 60 * fps / lag
        while bpm < 70 { bpm *= 2 }
        while bpm > 160 { bpm /= 2 }

        // Is there a related tempo the beat supports nearly as well?
        let mine = support(Int((60 * fps / bpm).rounded()))
        var alt: Double?
        var altStrength = -1.0
        if mine > 0 {
            // Half and double time are always shown alongside, so only 3:2 readings count as a real alternative.
            for ratio in [1.5, 2.0 / 3.0] {
                let candidate = bpm * ratio
                guard (60...200).contains(candidate) else { continue }
                let bar = 0.85
                let strength = support(Int((60 * fps / candidate).rounded())) / mine - bar
                if strength > altStrength {
                    altStrength = strength
                    alt = candidate
                }
            }
        }
        return (bpm, altStrength > 0 ? alt : nil)
    }

    // MARK: - Key

    private static let chromaSize = 8192

    /// How much energy falls on each of the 12 notes, summed over the slice.
    static func pitchClasses(_ samples: [Float], sampleRate: Double) -> [Double] {
        var chroma = [Double](repeating: 0, count: 12)
        let n = chromaSize
        guard samples.count > n, let fft = vDSP.FFT(log2n: 13, radix: .radix2, ofType: DSPSplitComplex.self) else { return chroma }
        let window = vDSP.window(ofType: Float.self, usingSequence: .hanningDenormalized, count: n, isHalfWindow: false)

        // Map each FFT bin between ~55 Hz and ~4 kHz to its note.
        let binHz = sampleRate / Double(n)
        let bins: [(index: Int, pitch: Int)] = (1..<(n / 2)).compactMap { b in
            let f = Double(b) * binHz
            guard f >= 55, f <= 4200 else { return nil }
            let midi = Int((12 * log2(f / 440)).rounded()) + 69
            return (b, ((midi % 12) + 12) % 12)
        }

        samples.withUnsafeBufferPointer { p in
            var start = 0
            while start + n <= samples.count {
                let mags = magnitudes(UnsafeBufferPointer(rebasing: p[start..<(start + n)]), window: window, fft: fft)
                var frame = [Double](repeating: 0, count: 12)
                for bin in bins { frame[bin.pitch] += Double(mags[bin.index]).squareRoot() }
                let total = frame.reduce(0, +)
                if total > 0 { for i in 0..<12 { chroma[i] += frame[i] / total } }
                start += n / 2
            }
        }
        return chroma
    }

    private static let noteNames = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"]
    private static let camelotMajor = ["8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B"]
    private static let camelotMinor = ["5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A"]
    // Krumhansl–Kessler key profiles.
    private static let majorProfile = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
    private static let minorProfile = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]

    /// Matches the note balance against every major and minor key and keeps the best fit.
    static func key(from chroma: [Double]) -> (name: String, camelot: String)? {
        guard chroma.reduce(0, +) > 0 else { return nil }
        func correlation(_ a: [Double], _ b: [Double]) -> Double {
            let ma = a.reduce(0, +) / 12, mb = b.reduce(0, +) / 12
            var num = 0.0, da = 0.0, db = 0.0
            for i in 0..<12 {
                num += (a[i] - ma) * (b[i] - mb)
                da += (a[i] - ma) * (a[i] - ma)
                db += (b[i] - mb) * (b[i] - mb)
            }
            return da > 0 && db > 0 ? num / (da * db).squareRoot() : 0
        }

        var best: (score: Double, tonic: Int, minor: Bool) = (-2, 0, false)
        for tonic in 0..<12 {
            let rotated = (0..<12).map { chroma[($0 + tonic) % 12] }
            let major = correlation(rotated, majorProfile)
            let minor = correlation(rotated, minorProfile)
            if major > best.score { best = (major, tonic, false) }
            if minor > best.score { best = (minor, tonic, true) }
        }
        let name = "\(noteNames[best.tonic]) \(best.minor ? "min" : "maj")"
        return (name, best.minor ? camelotMinor[best.tonic] : camelotMajor[best.tonic])
    }

    // MARK: - Shared

    private static func magnitudes(_ frame: UnsafeBufferPointer<Float>, window: [Float], fft: vDSP.FFT<DSPSplitComplex>) -> [Float] {
        let size = frame.count
        let half = size / 2
        let windowed = vDSP.multiply(frame, window)
        var real = [Float](repeating: 0, count: half)
        var imag = [Float](repeating: 0, count: half)
        var mags = [Float](repeating: 0, count: half)
        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: half) {
                        vDSP_ctoz($0, 2, &split, 1, vDSP_Length(half))
                    }
                }
                fft.forward(input: split, output: &split)
                mags.withUnsafeMutableBufferPointer { mp in
                    vDSP_zvabs(&split, 1, mp.baseAddress!, 1, vDSP_Length(half))
                }
            }
        }
        return mags
    }
}

/// ITU BS.1770-style loudness: K-weighting (high shelf + high pass), 400 ms blocks,
/// an absolute gate at −70 LUFS and a relative gate 10 LU below the ungated level.
struct LoudnessMeter {
    private let sampleRate: Double
    private var blocks: [Double] = []
    private(set) var peak: Float = 0

    init(sampleRate: Double) { self.sampleRate = sampleRate }

    mutating func add(_ buffer: AVAudioPCMBuffer) {
        guard let data = buffer.floatChannelData else { return }
        let n = Int(buffer.frameLength), channels = min(2, Int(buffer.format.channelCount))
        guard n > 0 else { return }
        let block = Int(sampleRate * 0.4)
        var energy = [Double](repeating: 0, count: n / block)
        for c in 0..<channels {
            let samples = UnsafeBufferPointer(start: data[c], count: n)
            peak = max(peak, vDSP.maximumMagnitude(samples))
            let weighted = Self.kWeight(Array(samples), rate: sampleRate)
            for b in 0..<energy.count {
                let part = weighted[(b * block)..<((b + 1) * block)]
                energy[b] += Double(vDSP.sumOfSquares(part)) / Double(block)
            }
        }
        // A mono file counts as both channels, as BS.1770 does for a centre signal.
        if channels == 1 { energy = energy.map { $0 * 2 } }
        blocks += energy
    }

    var result: (lufs: Double, peak: Double)? {
        func lufs(_ z: Double) -> Double { -0.691 + 10 * log10(max(z, 1e-12)) }
        let loud = blocks.filter { lufs($0) > -70 }
        guard !loud.isEmpty else { return nil }
        let threshold = lufs(loud.reduce(0, +) / Double(loud.count)) - 10
        let gated = loud.filter { lufs($0) > threshold }
        let z = gated.reduce(0, +) / Double(max(gated.count, 1))
        return (lufs(z), 20 * log10(Double(max(peak, 1e-6))))
    }

    /// The two K-weighting biquads, with coefficients worked out for any sample rate.
    private static func kWeight(_ x: [Float], rate: Double) -> [Float] {
        // Stage 1: high shelf (+4 dB above ~1.7 kHz).
        var f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196
        var k = tan(Double.pi * f0 / rate)
        let vh = pow(10, g / 20), vb = pow(vh, 0.4996667741545416)
        var a0 = 1 + k / q + k * k
        let shelf = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                     2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
        // Stage 2: high pass (~38 Hz).
        f0 = 38.13547087602444; q = 0.5003270373238773
        k = tan(Double.pi * f0 / rate)
        a0 = 1 + k / q + k * k
        let pass = [1.0, -2.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
        guard var biquad = vDSP.Biquad(coefficients: shelf + pass, channelCount: 1, sectionCount: 2, ofType: Float.self) else { return x }
        return biquad.apply(input: x)
    }
}
