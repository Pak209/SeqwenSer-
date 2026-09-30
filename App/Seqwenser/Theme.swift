import SwiftUI
import SeqwenserKit

extension Color {
    init(hex: UInt32, alpha: Double = 1) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 255) / 255,
                  green: Double((hex >> 8) & 255) / 255,
                  blue: Double(hex & 255) / 255,
                  opacity: alpha)
    }
}

/// Dark industrial hardware-sampler palette with neon accents.
enum Neon {
    static let bg = Color(hex: 0x0B0D10)
    static let panel = Color(hex: 0x15181D)
    static let panel2 = Color(hex: 0x1D2128)
    static let edge = Color(hex: 0x2B313B)
    static let track = Color(hex: 0x262B33)
    static let text = Color(hex: 0xE8ECF2)
    static let textDim = Color(hex: 0x8B94A3)
    static let lime = Color(hex: 0xA6FF2E)
    static let cyan = Color(hex: 0x21E6F2)
    static let magenta = Color(hex: 0xFF3FB4)
    static let orange = Color(hex: 0xFF8A1F)
    static let purple = Color(hex: 0xA35CFF)
    static let yellow = Color(hex: 0xFFD23F)
    static let red = Color(hex: 0xFF3B3B)
    static let lcd = Color(hex: 0x9CAE8B)
    static let lcdInk = Color(hex: 0x1F2B1C)
    static let lcdMid = Color(hex: 0x4E6448)

    /// Colour of a step by the slice it plays.
    static func stepColor(slice: Int) -> Color {
        [lime, magenta, orange, cyan, yellow, purple][((slice % 6) + 6) % 6]
    }
    static func fx(_ k: FxKind) -> Color { Color(hex: k.colorHex) }
    static func fxIcon(_ k: FxKind) -> String {
        ["waveform.path", "circle.grid.3x3.fill", "repeat", "chart.line.downtrend.xyaxis", "circle.circle", "bolt.fill"][k.rawValue]
    }
}

extension Font {
    static func chunky(_ size: CGFloat) -> Font { .system(size: size, weight: .heavy, design: .rounded) }
    static func lcd(_ size: CGFloat) -> Font { .system(size: size, weight: .bold, design: .monospaced) }
    static func label(_ size: CGFloat) -> Font { .system(size: size, weight: .bold, design: .rounded) }
}

extension View {
    func neonGlow(_ c: Color, radius: CGFloat = 6, opacity: Double = 0.7) -> some View {
        shadow(color: c.opacity(opacity), radius: radius)
    }
}

/// A raised hardware-style button body.
struct KeyCap: ViewModifier {
    var color: Color = Neon.panel2
    var lit = false
    var litColor: Color = Neon.lime
    var corner: CGFloat = 10
    func body(content: Content) -> some View {
        content
            .background(
                RoundedRectangle(cornerRadius: corner)
                    .fill(LinearGradient(colors: [color.opacity(1), color.opacity(0.75)], startPoint: .top, endPoint: .bottom))
            )
            .overlay(
                RoundedRectangle(cornerRadius: corner)
                    .stroke(lit ? litColor : Neon.edge, lineWidth: lit ? 1.5 : 1)
            )
            .shadow(color: lit ? litColor.opacity(0.45) : .clear, radius: 6)
    }
}

extension View {
    func keyCap(color: Color = Neon.panel2, lit: Bool = false, litColor: Color = Neon.lime, corner: CGFloat = 10) -> some View {
        modifier(KeyCap(color: color, lit: lit, litColor: litColor, corner: corner))
    }
}

// MARK: - Pixel art (original artwork, drawn from text bitmaps)

struct PixelSprite {
    let rows: [String]
    let palette: [Character: Color]

    var width: Int { rows.map { $0.count }.max() ?? 0 }
    var height: Int { rows.count }

    func draw(in ctx: GraphicsContext, at origin: CGPoint, unit: CGFloat) {
        for (y, row) in rows.enumerated() {
            for (x, ch) in row.enumerated() {
                guard let c = palette[ch] else { continue }
                let r = CGRect(x: origin.x + CGFloat(x) * unit, y: origin.y + CGFloat(y) * unit, width: unit, height: unit)
                ctx.fill(Path(r), with: .color(c))
            }
        }
    }
}

/// "Bit", the app's original mascot: a round little sampler critter with an antenna and headphones.
enum Mascot {
    static let rows = [
        "......yy........",
        ".......k........",
        "....hhhhhhhh....",
        "...hkkkkkkkkh...",
        "..hhkbbbbbbkhh..",
        ".hhhkbbbbbbkhhh.",
        ".hhhkbkbbkbkhhh.",
        ".hhhkbkbbkbkhhh.",
        ".hhhkbbbbbbkhhh.",
        ".hhhkbpbbpbkhhh.",
        "..hhkbbkkbbkhh..",
        "...hkbbbbbbkh...",
        "....kkkkkkkk....",
        "....kbdbbdbk....",
        ".....kk..kk.....",
    ]
    static let color = PixelSprite(rows: rows, palette: [
        "y": Neon.yellow, "k": Color(hex: 0x18241E), "h": Neon.magenta, "b": Color(hex: 0x78E6C8), "p": Color(hex: 0xFF8CAA), "d": Color(hex: 0x46AA96),
    ])
    static let lcd = PixelSprite(rows: rows, palette: [
        "y": Neon.lcdInk, "k": Neon.lcdInk, "h": Neon.lcdMid, "b": Neon.lcd.opacity(0.0), "p": Neon.lcdMid, "d": Neon.lcdMid,
    ])
}

struct MascotView: View {
    var color = true
    var body: some View {
        Canvas { ctx, size in
            let sprite = color ? Mascot.color : Mascot.lcd
            let unit = floor(min(size.width / CGFloat(sprite.width), size.height / CGFloat(sprite.height)))
            let origin = CGPoint(x: (size.width - unit * CGFloat(sprite.width)) / 2, y: (size.height - unit * CGFloat(sprite.height)) / 2)
            sprite.draw(in: ctx, at: origin, unit: unit)
        }
    }
}
