import SwiftUI

extension Color {
    init(hex: UInt32, opacity: Double = 1) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255,
                  opacity: opacity)
    }
}

/// Moose-print red, stamp ink, poster cream.
enum Palette {
    static let red = Color(hex: 0xE8472B)
    static let ink = Color(hex: 0x161414)
    static let cream = Color(hex: 0xFBF1DC)
    static let glow = Color(hex: 0xF7853A)      // the warm inner circle
    static let ember = Color(hex: 0xB8321E)     // where circles overlap
    static let label = Color(hex: 0xE2B33B)     // yellow record label
    static let caseGrey = Color(hex: 0x3A3C3F)  // flight-case panel
    static let caseEdge = Color(hex: 0x111112)
}

/// Brushed aluminium, like the faceplate of a turntable's start · stop key.
struct BrushedSteel: View {
    var body: some View {
        ZStack {
            LinearGradient(colors: [Color(hex: 0xF2F2F0), Color(hex: 0xC9C9C6), Color(hex: 0xE4E4E1), Color(hex: 0xB4B4B0)],
                           startPoint: .top, endPoint: .bottom)
            Canvas { ctx, size in
                var y: CGFloat = 0
                var i = 0.0
                while y < size.height {
                    let a = 0.04 + 0.05 * abs(sin(i * 12.9898))
                    ctx.fill(Path(CGRect(x: 0, y: y, width: size.width, height: 0.6)),
                             with: .color((Int(i) % 2 == 0 ? Color.white : Color.black).opacity(Int(i) % 2 == 0 ? a * 2.5 : a * 0.6)))
                    y += 1.3
                    i += 1
                }
            }
        }
    }
}

enum Motion {
    static let snappy = Animation.spring(response: 0.3, dampingFraction: 0.72)
    static let bouncy = Animation.spring(response: 0.45, dampingFraction: 0.6)
    static let jelly = Animation.spring(response: 0.55, dampingFraction: 0.45)
    static let reveal = Animation.spring(response: 1.0, dampingFraction: 0.85)
}

struct BouncyButtonStyle: ButtonStyle {
    var pressedScale: CGFloat = 0.9

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .contentShape(Rectangle())
            .scaleEffect(configuration.isPressed ? pressedScale : 1)
            .animation(.spring(response: 0.25, dampingFraction: 0.45), value: configuration.isPressed)
    }
}

// MARK: - Risograph grain

/// Pre-rendered noise tiles. Laid over flat colour they give the stamped, printed texture
/// of the moose print without any per-frame cost.
enum Grain {
    /// Dark specks for the red paper.
    static let paper = make(rgb: (0x16, 0x14, 0x14), density: 0.2, alpha: 25...80)
    /// Red specks: invisible on red, but they knock holes in the ink like a worn stamp.
    static let speckle = make(rgb: (0xE8, 0x47, 0x2B), density: 0.16, alpha: 90...255)
    /// Light dust for the space view.
    static let dust = make(rgb: (0xFB, 0xF1, 0xDC), density: 0.08, alpha: 15...60)

    private static func make(rgb: (Int, Int, Int), density: Double, alpha: ClosedRange<Int>, size: Int = 256) -> CGImage {
        var bytes = [UInt8](repeating: 0, count: size * size * 4)
        for i in 0..<(size * size) where Double.random(in: 0..<1) < density {
            let a = Int.random(in: alpha)
            bytes[i * 4] = UInt8(rgb.0 * a / 255)
            bytes[i * 4 + 1] = UInt8(rgb.1 * a / 255)
            bytes[i * 4 + 2] = UInt8(rgb.2 * a / 255)
            bytes[i * 4 + 3] = UInt8(a)
        }
        return bytes.withUnsafeMutableBytes { buffer in
            let ctx = CGContext(data: buffer.baseAddress, width: size, height: size, bitsPerComponent: 8,
                                bytesPerRow: size * 4, space: CGColorSpaceCreateDeviceRGB(),
                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            return ctx.makeImage()!
        }
    }
}

struct GrainOverlay: View {
    let image: CGImage

    var body: some View {
        Image(decorative: image, scale: 2)
            .resizable(resizingMode: .tile)
            .allowsHitTesting(false)
    }
}

// MARK: - Shapes

/// One blade of the moose's grass.
struct Spike: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: r.midX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
        p.closeSubpath()
        return p
    }
}

/// The poster's two circles, re-cut as a stamp: a sun on top, a record below,
/// and a cream lens between them with the moose's eye in it.
struct CrateMark: View {
    var size: CGFloat
    var spin: Angle = .zero
    var eye: CGFloat = 1

    var body: some View {
        let d = size * 0.6
        let shift = d * 0.33

        ZStack {
            ZStack {
                Circle().fill(Palette.ink)
                Circle().strokeBorder(Palette.red, lineWidth: d * 0.03).padding(d * 0.1)
                Circle().strokeBorder(Palette.red, lineWidth: d * 0.018).padding(d * 0.19)
                Circle().fill(Palette.cream).frame(width: d * 0.3)
                Circle().fill(Palette.red).frame(width: d * 0.045).offset(x: d * 0.09, y: -d * 0.03)
                Circle().fill(Palette.ink).frame(width: d * 0.045)
            }
            .frame(width: d, height: d)
            .rotationEffect(spin)
            .offset(y: shift)

            Circle().fill(Palette.ink)
                .frame(width: d, height: d)
                .offset(y: -shift)

            Circle().fill(Palette.cream)
                .frame(width: d, height: d)
                .mask(Circle().frame(width: d, height: d).offset(y: -shift * 2))
                .offset(y: shift)

            Circle().fill(Palette.ink)
                .frame(width: d * 0.12, height: d * 0.12)
                .scaleEffect(x: max(eye, 1), y: eye)
        }
        .frame(width: size, height: size)
    }
}

struct Wordmark: View {
    var color: Color

    var body: some View {
        Text("DSS1")
            .font(.system(size: 13, weight: .black))
            .fontWidth(.expanded)
            .tracking(4)
            .foregroundStyle(color)
    }
}

/// The one back button every page uses: a dark capsule naming where it goes.
struct BackButton: View {
    var label: String
    var help: String? = nil
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: "chevron.left")
                Text(label.uppercased()).tracking(2)
            }
            .font(.system(size: 11, weight: .black))
            .foregroundStyle(Palette.cream)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(Capsule().fill(Color.black.opacity(0.4)))
            .overlay(Capsule().strokeBorder(Color.white.opacity(0.12), lineWidth: 1))
        }
        .buttonStyle(BouncyButtonStyle())
        .help(help ?? "Back to \(label.lowercased())")
    }
}

extension Screen {
    /// What a back button says when it returns here.
    var backLabel: String {
        switch self {
        case .home: return "Drop"
        case .player: return "Crate"
        case .chop: return "Chop"
        case .radio: return "Radio"
        case .perform: return "Perform"
        }
    }
}
