// Renders the Crate app icon (the stamped sun-and-record mark on moose-print red) to a 1024px PNG.
// Usage: swift make_icon.swift out.png
import AppKit

let size: CGFloat = 1024
let image = NSImage(size: NSSize(width: size, height: size))
image.lockFocus()

func color(_ hex: UInt32) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: 1)
}
let red = color(0xE8472B), ink = color(0x161414), cream = color(0xFBF1DC)

let inset: CGFloat = 100
let tile = NSRect(x: inset, y: inset, width: size - inset * 2, height: size - inset * 2)
let squircle = NSBezierPath(roundedRect: tile, xRadius: 185, yRadius: 185)

NSGraphicsContext.saveGraphicsState()
let shadow = NSShadow()
shadow.shadowColor = NSColor.black.withAlphaComponent(0.25)
shadow.shadowBlurRadius = 30
shadow.shadowOffset = NSSize(width: 0, height: -12)
shadow.set()
red.setFill()
squircle.fill()
NSGraphicsContext.restoreGraphicsState()
squircle.addClip()

let d: CGFloat = 400
let cx = size / 2
let shift = d * 0.33
let topY = size / 2 + shift     // AppKit's y axis points up
let bottomY = size / 2 - shift

func circle(_ x: CGFloat, _ y: CGFloat, _ diameter: CGFloat) -> NSBezierPath {
    NSBezierPath(ovalIn: NSRect(x: x - diameter / 2, y: y - diameter / 2, width: diameter, height: diameter))
}
func ring(_ diameter: CGFloat, _ width: CGFloat) {
    let p = circle(cx, bottomY, diameter - width)
    p.lineWidth = width
    red.setStroke()
    p.stroke()
}

ink.setFill(); circle(cx, bottomY, d).fill()
ring(d * 0.8, d * 0.03)
ring(d * 0.62, d * 0.018)
cream.setFill(); circle(cx, bottomY, d * 0.3).fill()
ink.setFill(); circle(cx, bottomY, d * 0.045).fill()
ink.setFill(); circle(cx, topY, d).fill()

NSGraphicsContext.saveGraphicsState()
circle(cx, topY, d).addClip()
cream.setFill(); circle(cx, bottomY, d).fill()
NSGraphicsContext.restoreGraphicsState()
ink.setFill(); circle(cx, size / 2, d * 0.12).fill()

// Stamp wear: red specks over the ink.
srand48(7)
for _ in 0..<9000 {
    let x = drand48() * size, y = drand48() * size
    red.withAlphaComponent(CGFloat(0.35 + drand48() * 0.65)).setFill()
    NSBezierPath(rect: NSRect(x: x, y: y, width: 2.4, height: 2.4)).fill()
}

image.unlockFocus()

let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
