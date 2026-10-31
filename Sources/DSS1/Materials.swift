import CoreGraphics
import Foundation

/// Procedural, photo-like surface textures, each rendered once and cached:
/// scratched brushed aluminium, aged speckled paper, and hammertone painted steel.
enum Materials {
    /// Faceplate metal, sized for the radio deck at 2× resolution.
    static let aluminum = brushedAluminum(width: 2000, height: 790, seed: 7)
    /// A smaller piece of the same metal, for keys and caps.
    static let aluminumSmall = brushedAluminum(width: 240, height: 120, seed: 19)
    /// Brushed streaks as a see-through overlay (white and black with alpha), to lay over any solid colour.
    static let brushOverlay = brushedOverlay(size: 256, seed: 23)
    /// VU meter faces: yellowed paper with specks and stains.
    static let meterPaper = agedPaper(width: 300, height: 240, seed: 3, wear: 1)
    /// Cassette label stock: cleaner, lighter paper.
    static let labelPaper = agedPaper(width: 700, height: 100, seed: 11, wear: 0.35)
    /// Cabinet paint: grey-green hammertone, tileable.
    static let hammertone = hammertonePaint(size: 512, seed: 5)

    // MARK: - Recipes

    private static func brushedAluminum(width w: Int, height h: Int, seed: UInt64) -> CGImage {
        var rng = Seeded(seed)
        let W = Double(w), H = Double(h)
        // Everything scales with the size of the piece, so a small key cap isn't buried in marks.
        let area = W * H / (2000.0 * 790.0)
        let u = max(0.15, area.squareRoot())
        func count(_ base: Double, min lo: Int = 0) -> Int { max(lo, Int(base * area)) }
        let blotch = ValueNoise(cells: 14, rng: &rng)

        return render(width: w, height: h, fill: { px in
            // Long horizontal grain, uneven from row to row and mottled where it has been rubbed.
            var band = 0.0
            for y in 0..<h {
                band = band * 0.8 + rng.gauss() * 0.022
                let row = 0.76 + band + rng.gauss() * 0.014
                var v = row
                for x in 0..<w {
                    v += (row + (rng.next() - 0.5) * 0.15 - v) * 0.09
                    let m = blotch.at(Double(x) / W, Double(y) / H)
                    let p = v - (m - 0.5) * 0.10 + (rng.next() - 0.5) * 0.03
                    put(&px, (y * w + x) * 4, r: p * 0.985, g: p * 0.99, b: p)
                }
            }
        }, draw: { ctx in
            let cs = CGColorSpaceCreateDeviceRGB()

            // Years of grime, tarnish and polished patches where hands have rubbed.
            for _ in 0..<count(120, min: 5) {
                let c = CGPoint(x: rng.next() * W, y: rng.next() * H)
                blot(ctx, at: c, radius: (30 + rng.next() * 260) * u, color: (0.22, 0.19, 0.15), alpha: 0.05 + rng.next() * 0.11)
            }
            for _ in 0..<count(40, min: 2) {
                let c = CGPoint(x: rng.next() * W, y: rng.next() * H)
                blot(ctx, at: c, radius: (40 + rng.next() * 160) * u, color: (0.56, 0.46, 0.26), alpha: 0.04 + rng.next() * 0.06)
            }
            for _ in 0..<count(30, min: 2) {
                let c = CGPoint(x: rng.next() * W, y: rng.next() * H)
                blot(ctx, at: c, radius: (40 + rng.next() * 140) * u, color: (1, 1, 1), alpha: 0.05 + rng.next() * 0.08)
            }
            // Finger smudges around where the keys and knobs are worked.
            if w > 1000 {
                for _ in 0..<26 {
                    let c = CGPoint(x: (0.05 + rng.next() * 0.42) * W, y: (0.04 + rng.next() * 0.2) * H)
                    blot(ctx, at: c, radius: 24 + rng.next() * 60, color: (0.2, 0.17, 0.12), alpha: 0.05 + rng.next() * 0.07)
                }
            }

            // Darker, dirtier edges, where dust and grease collect.
            let edge = CGColor(red: 0.16, green: 0.13, blue: 0.09, alpha: 0.42)
            let clear = CGColor(red: 0.16, green: 0.13, blue: 0.09, alpha: 0)
            if let g = CGGradient(colorsSpace: cs, colors: [edge, clear] as CFArray, locations: [0, 1]) {
                let t = 80 * u
                ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: 0), end: CGPoint(x: t, y: 0), options: [])
                ctx.drawLinearGradient(g, start: CGPoint(x: W, y: 0), end: CGPoint(x: W - t, y: 0), options: [])
                ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 0, y: t), options: [])
                ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: H), end: CGPoint(x: 0, y: H - t), options: [])
            }

            // Fine scratches: a haze of them, mostly along the grain, some across.
            for _ in 0..<count(2600, min: 18) {
                let x = rng.next() * W, y = rng.next() * H
                let along = rng.next() < 0.72
                let angle = along ? (rng.next() - 0.5) * 0.3 : rng.next() * .pi
                let len = (8 + pow(rng.next(), 2) * (along ? 420 : 160)) * u
                let bend = (rng.next() - 0.5) * len * 0.18
                let light = rng.next() < 0.66
                ctx.setStrokeColor(red: light ? 1 : 0.12, green: light ? 1 : 0.12, blue: light ? 1 : 0.12,
                                   alpha: light ? 0.06 + rng.next() * 0.22 : 0.05 + rng.next() * 0.12)
                ctx.setLineWidth(0.5 + rng.next() * 1.2)
                ctx.move(to: CGPoint(x: x, y: y))
                ctx.addQuadCurve(to: CGPoint(x: x + cos(angle) * len, y: y + sin(angle) * len),
                                 control: CGPoint(x: x + cos(angle) * len / 2 - sin(angle) * bend, y: y + sin(angle) * len / 2 + cos(angle) * bend))
                ctx.strokePath()
            }

            // Deep gouges: long, dark in the groove with a bright burr beside it.
            for _ in 0..<count(70, min: 1) {
                let x = rng.next() * W, y = rng.next() * H
                let angle = rng.next() < 0.6 ? (rng.next() - 0.5) * 0.5 : rng.next() * .pi
                let len = (150 + rng.next() * 600) * u
                let bend = (rng.next() - 0.5) * len * 0.1
                let width = 1.0 + rng.next() * 1.8
                let end = CGPoint(x: x + cos(angle) * len, y: y + sin(angle) * len)
                let control = CGPoint(x: x + cos(angle) * len / 2 - sin(angle) * bend, y: y + sin(angle) * len / 2 + cos(angle) * bend)
                for (dy, red, alpha) in [(0.0, 0.1, 0.34), (1.4, 1.0, 0.55)] {
                    ctx.setStrokeColor(red: red, green: red, blue: red, alpha: alpha * (0.6 + rng.next() * 0.4))
                    ctx.setLineWidth(dy == 0 ? width : width * 0.6)
                    ctx.move(to: CGPoint(x: x, y: y + dy))
                    ctx.addQuadCurve(to: CGPoint(x: end.x, y: end.y + dy), control: CGPoint(x: control.x, y: control.y + dy))
                    ctx.strokePath()
                }
            }

            // Circular scuffs, from a cloth going round and round.
            for _ in 0..<count(30, min: 0) {
                let c = CGPoint(x: rng.next() * W, y: rng.next() * H)
                let r0 = (14 + rng.next() * 60) * u
                for k in 0..<7 {
                    let start = rng.next() * 6.28
                    ctx.setStrokeColor(red: 1, green: 1, blue: 1, alpha: 0.05 + rng.next() * 0.14)
                    ctx.setLineWidth(0.5 + rng.next() * 0.6)
                    ctx.addArc(center: c, radius: r0 + Double(k) * 3.2 * u, startAngle: start, endAngle: start + 0.8 + rng.next() * 2.2, clockwise: false)
                    ctx.strokePath()
                }
            }

            // Pitting and corrosion specks.
            for _ in 0..<count(4200, min: 50) {
                let s = 0.6 + rng.next() * 1.9
                ctx.setFillColor(red: 0.18, green: 0.18, blue: 0.17, alpha: 0.12 + rng.next() * 0.3)
                ctx.fillEllipse(in: CGRect(x: rng.next() * W, y: rng.next() * H, width: s, height: s))
            }
            for _ in 0..<count(160, min: 3) {
                let s = 0.8 + rng.next() * 2.2
                ctx.setFillColor(red: 0.42, green: 0.27, blue: 0.12, alpha: 0.25 + rng.next() * 0.3)
                ctx.fillEllipse(in: CGRect(x: rng.next() * W, y: rng.next() * H, width: s, height: s))
            }

            // Dings and dents: a dark hollow with a bright crescent on its lit side.
            for _ in 0..<count(16, min: 0) {
                let c = CGPoint(x: rng.next() * W, y: rng.next() * H)
                let r = (7 + rng.next() * 20) * max(u, 0.5)
                blot(ctx, at: CGPoint(x: c.x + r * 0.28, y: c.y - r * 0.28), radius: r, color: (1, 1, 1), alpha: 0.28)
                blot(ctx, at: c, radius: r, color: (0.1, 0.1, 0.1), alpha: 0.24)
            }
        })
    }

    private static func brushedOverlay(size n: Int, seed: UInt64) -> CGImage {
        var rng = Seeded(seed)
        return render(width: n, height: n, fill: { px in
            var band = 0.0
            for y in 0..<n {
                band = band * 0.7 + rng.gauss() * 0.05
                var v = band
                for x in 0..<n {
                    v += (band + (rng.next() - 0.5) * 0.5 - v) * 0.12
                    let a = min(0.55, abs(v) * 0.9)
                    let i = (y * n + x) * 4
                    let c = UInt8(a * 255)
                    if v > 0 {
                        px[i] = c; px[i + 1] = c; px[i + 2] = c; px[i + 3] = c      // white, premultiplied
                    } else {
                        px[i] = 0; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = c      // black
                    }
                }
            }
        }, draw: { _ in })
    }

    private static func agedPaper(width w: Int, height h: Int, seed: UInt64, wear: Double) -> CGImage {
        var rng = Seeded(seed)
        let blotch = ValueNoise(cells: 10, rng: &rng)
        let fine = ValueNoise(cells: 60, rng: &rng)
        return render(width: w, height: h, fill: { px in
            for y in 0..<h {
                for x in 0..<w {
                    let u = Double(x) / Double(w), v = Double(y) / Double(h)
                    let b = blotch.at(u, v), f = fine.at(u, v)
                    let shade = 1 - wear * (0.08 * b + 0.04 * f) + (rng.next() - 0.5) * 0.025
                    put(&px, (y * w + x) * 4, r: 0.97 * shade, g: (0.90 - 0.05 * wear * b) * shade, b: (0.72 - 0.1 * wear * b) * shade)
                }
            }
        }, draw: { ctx in
            // Tea-coloured stains.
            for _ in 0..<Int(14 * wear) {
                blot(ctx, at: CGPoint(x: rng.next() * Double(w), y: rng.next() * Double(h)),
                     radius: 15 + rng.next() * 70, color: (0.55, 0.4, 0.2), alpha: 0.05 + rng.next() * 0.08)
            }
            // Specks, a few larger flecks.
            for _ in 0..<Int(260 * wear) {
                let big = rng.next() < 0.08
                let s = big ? 2 + rng.next() * 4 : 0.5 + rng.next() * 1.5
                ctx.setFillColor(red: 0.18, green: 0.14, blue: 0.08, alpha: 0.25 + rng.next() * 0.5)
                ctx.fillEllipse(in: CGRect(x: rng.next() * Double(w), y: rng.next() * Double(h), width: s * (0.6 + rng.next()), height: s))
            }
            // Darkened edges, as old paper goes.
            let space = CGColorSpaceCreateDeviceRGB()
            let colors = [CGColor(red: 0.5, green: 0.38, blue: 0.2, alpha: 0), CGColor(red: 0.45, green: 0.33, blue: 0.16, alpha: 0.28 * wear)] as CFArray
            if let g = CGGradient(colorsSpace: space, colors: colors, locations: [0.55, 1]) {
                let c = CGPoint(x: Double(w) / 2, y: Double(h) / 2)
                ctx.drawRadialGradient(g, startCenter: c, startRadius: 0, endCenter: c, endRadius: Double(max(w, h)) * 0.72,
                                       options: [.drawsAfterEndLocation])
            }
        })
    }

    private static func hammertonePaint(size n: Int, seed: UInt64) -> CGImage {
        var rng = Seeded(seed)
        let mottle = ValueNoise(cells: 8, rng: &rng, wraps: true)
        return render(width: n, height: n, fill: { px in
            for y in 0..<n {
                for x in 0..<n {
                    let m = mottle.at(Double(x) / Double(n), Double(y) / Double(n))
                    let shade = 1 + (m - 0.5) * 0.08 + (rng.next() - 0.5) * 0.05
                    put(&px, (y * n + x) * 4, r: 0.34 * shade, g: 0.385 * shade, b: 0.365 * shade)
                }
            }
        }, draw: { ctx in
            // Hammered dimples, drawn wrapped so the tile repeats seamlessly.
            for _ in 0..<520 {
                let x = rng.next() * Double(n), y = rng.next() * Double(n)
                let r = 3 + rng.next() * 11
                let light = rng.next() < 0.5
                for dx in [-n, 0, n] {
                    for dy in [-n, 0, n] {
                        blot(ctx, at: CGPoint(x: x + Double(dx), y: y + Double(dy)), radius: r,
                             color: light ? (0.75, 0.8, 0.78) : (0.05, 0.06, 0.06), alpha: 0.05)
                    }
                }
            }
        })
    }

    // MARK: - Helpers

    private static func render(width w: Int, height h: Int, fill: (inout [UInt8]) -> Void, draw: (CGContext) -> Void) -> CGImage {
        var px = [UInt8](repeating: 255, count: w * h * 4)
        fill(&px)
        return px.withUnsafeMutableBytes { buf in
            let ctx = CGContext(data: buf.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            draw(ctx)
            return ctx.makeImage()!
        }
    }

    private static func put(_ px: inout [UInt8], _ i: Int, r: Double, g: Double, b: Double) {
        px[i] = UInt8(max(0, min(255, r * 255)))
        px[i + 1] = UInt8(max(0, min(255, g * 255)))
        px[i + 2] = UInt8(max(0, min(255, b * 255)))
        px[i + 3] = 255
    }

    /// A soft round blot that fades to nothing.
    private static func blot(_ ctx: CGContext, at c: CGPoint, radius: Double, color: (Double, Double, Double), alpha: Double) {
        let colors = [CGColor(red: color.0, green: color.1, blue: color.2, alpha: alpha),
                      CGColor(red: color.0, green: color.1, blue: color.2, alpha: 0)] as CFArray
        guard let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 1]) else { return }
        ctx.drawRadialGradient(g, startCenter: c, startRadius: 0, endCenter: c, endRadius: radius, options: [])
    }
}

/// Repeatable random numbers, so textures look the same every launch.
struct Seeded {
    private var state: UInt64

    init(_ seed: UInt64) { state = seed &* 0x9E3779B97F4A7C15 }

    mutating func next() -> Double {
        state &+= 0x9E3779B97F4A7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return Double((z ^ (z >> 31)) >> 11) / Double(1 << 53)
    }

    mutating func gauss() -> Double {
        let u = max(next(), 1e-9), v = next()
        return sqrt(-2 * log(u)) * cos(2 * .pi * v)
    }
}

/// Smooth 2D noise from a coarse random grid.
struct ValueNoise {
    private let cells: Int
    private let grid: [Double]
    private let wraps: Bool

    init(cells: Int, rng: inout Seeded, wraps: Bool = false) {
        self.cells = cells
        self.wraps = wraps
        grid = (0..<((cells + 1) * (cells + 1))).map { _ in rng.next() }
    }

    func at(_ u: Double, _ v: Double) -> Double {
        let x = u * Double(cells), y = v * Double(cells)
        let x0 = min(Int(x), cells - 1), y0 = min(Int(y), cells - 1)
        let fx = x - Double(x0), fy = y - Double(y0)
        func g(_ i: Int, _ j: Int) -> Double {
            let ii = wraps ? i % cells : i, jj = wraps ? j % cells : j
            return grid[jj * (cells + 1) + ii]
        }
        let sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy)
        let a = g(x0, y0) + (g(x0 + 1, y0) - g(x0, y0)) * sx
        let b = g(x0, y0 + 1) + (g(x0 + 1, y0 + 1) - g(x0, y0 + 1)) * sx
        return a + (b - a) * sy
    }
}
