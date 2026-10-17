import Foundation

/// The six effect macros, in the order of the knob row.
public enum FxKind: Int, CaseIterable, Codable, Sendable {
    case pitch = 0, grain, repeatFx, filter, space, drive

    public var title: String { ["PITCH", "GRAIN", "REPEAT", "FILTER", "SPACE", "DRIVE"][rawValue] }
    /// Names of the two parameters of the effect (the first one is the knob on the main screen).
    public var paramNames: [String] {
        [["SEMITONES", "FORMANT"], ["MIX", "SCATTER"], ["AMOUNT", "RATE"], ["CUTOFF", "RESO"], ["MIX", "SIZE"], ["DRIVE", "TONE"]][rawValue]
    }
    /// Neon colour as 0xRRGGBB.
    public var colorHex: UInt32 { [0x21E6F2, 0xFFD23F, 0xFF3FB4, 0xFF8A1F, 0xA35CFF, 0xA6FF2E][rawValue] }
    public var hint: String {
        [
            "PITCH transposes the slice. FORMANT brightens or darkens the voice-like tone without changing the pitch.",
            "GRAIN smears the slice into tiny overlapping grains. SCATTER makes them jump around.",
            "REPEAT stutters the slice that just played. RATE picks the repeat length.",
            "FILTER is a low-pass. Turn CUTOFF down to darken, RESO to make it ring.",
            "SPACE adds reverb. SIZE sets how long the room rings.",
            "DRIVE crunches and saturates. TONE softens the top end.",
        ][rawValue]
    }
}

public struct FxValue: Codable, Equatable, Sendable {
    public var a: Float
    public var b: Float
    public init(_ a: Float, _ b: Float) { self.a = a; self.b = b }
}

public enum StepCondition: Int, CaseIterable, Codable, Sendable {
    case always = 0, every2, every4, first, notFirst, half
    public var title: String { ["ALWAYS", "EVERY 2ND", "EVERY 4TH", "FIRST PASS", "NOT FIRST", "50/50"][rawValue] }
}

public enum VelocityCondition: Int, CaseIterable, Codable, Sendable {
    case any = 0, above50, below50
    public var title: String { ["ANY VEL", "VEL > 50", "VEL < 50"][rawValue] }
}

public enum PlayMode: Int, CaseIterable, Codable, Sendable {
    case forward = 0, reverse, pingPong, random
    public var title: String { ["FORWARD", "REVERSE", "PING PONG", "RANDOM"][rawValue] }
}

public enum StepRate: Int, CaseIterable, Codable, Sendable {
    case quarter = 0, eighth, sixteenth, thirtySecond, triplet
    public var title: String { ["1/4", "1/8", "1/16", "1/32", "TRIPLET"][rawValue] }
}

public struct Step: Codable, Equatable, Sendable {
    public var active = false
    public var slice = 0
    public var velocity: Float = 1
    public var probability: Float = 1
    public var condition: StepCondition = .always
    public var velocityCondition: VelocityCondition = .any
    public var reverse = false
    public var lofi = false
    public var stretch = false
    /// Parameter locks: a value here replaces the global value while this step is playing.
    public var locks: [FxValue?] = Array(repeating: nil, count: FxKind.allCases.count)

    public init(slice: Int = 0) { self.slice = slice }

    public var hasAnyLock: Bool { locks.contains { $0 != nil } }
}

public struct Pattern: Codable, Equatable, Sendable {
    public static let steps = 16
    public var banks: [[Step]] = (0..<2).map { _ in (0..<16).map { Step(slice: $0) } }
    public var bank = 0
    public var length = 16
    public var playMode: PlayMode = .forward
    public var rate: StepRate = .sixteenth
    public var probability: Float = 1
    public var swing: Float = 0
    public var loop = true

    public init() {}

    public var current: [Step] {
        get { banks[bank] }
        set { banks[bank] = newValue }
    }
}

public struct Project: Codable, Equatable, Sendable {
    public static let currentVersion = 1
    public var version = Project.currentVersion
    public var name = "KIT_01"
    public var kitName = "Neon Break"
    public var bpm: Float = 120
    public var pattern = Pattern()
    /// Global effect values (primary, secondary), normalised 0...1.
    public var fx: [FxValue] = Project.defaultFx
    public var trimStart: Float = 0
    public var trimEnd: Float = 1
    /// Slice boundaries as fractions of the whole sample (count + 1 entries), empty = one slice.
    public var sliceEdges: [Float] = []
    public var volume: Float = 0.8
    public var pan: Float = 0
    /// File name of the sample inside the project folder.
    public var sampleFile: String?
    public var sampleSource: String?
    public var autoSlice = true
    public var sensitivity: Float = 0.5

    public static let defaultFx: [FxValue] = [
        FxValue(0.5, 0.5), FxValue(0, 0.3), FxValue(0, 0.5), FxValue(1, 0.15), FxValue(0, 0.5), FxValue(0, 1),
    ]

    public init() {}

    public var sliceCount: Int { max(1, sliceEdges.count - 1) }

    public static func == (a: Project, b: Project) -> Bool {
        a.version == b.version && a.name == b.name && a.kitName == b.kitName && a.bpm == b.bpm && a.pattern == b.pattern
            && a.fx == b.fx && a.trimStart == b.trimStart && a.trimEnd == b.trimEnd && a.sliceEdges == b.sliceEdges
            && a.volume == b.volume && a.pan == b.pan && a.sampleFile == b.sampleFile && a.sampleSource == b.sampleSource
            && a.autoSlice == b.autoSlice && a.sensitivity == b.sensitivity
    }
}

/// Text helpers for showing parameter values.
public enum ParamText {
    public static func semitones(_ n: Float) -> String {
        let v = Int(((n.clamped(0, 1) - 0.5) * 24).rounded())
        return v > 0 ? "+\(v)" : "\(v)"
    }
    public static func percent(_ n: Float) -> String { "\(Int((n.clamped(0, 1) * 100).rounded()))%" }
    public static func cutoff(_ n: Float) -> String {
        let hz = 20 * powf(1000, n.clamped(0, 1))
        return hz >= 1000 ? String(format: "%.1fk", hz / 1000) : "\(Int(hz.rounded()))"
    }
    public static func formant(_ n: Float) -> String {
        let v = Int(((n.clamped(0, 1) - 0.5) * 200).rounded())
        return v > 0 ? "+\(v)" : "\(v)"
    }
    public static let repeatRates = ["1/4", "1/8", "1/16", "1/32"]
    public static func repeatRate(_ n: Float) -> String { repeatRates[Int((n.clamped(0, 1) * 3).rounded())] }

    /// Value text of an effect parameter (k = 0 or 1).
    public static func text(_ fx: FxKind, _ k: Int, _ v: Float) -> String {
        switch (fx, k) {
        case (.pitch, 0): return semitones(v)
        case (.pitch, _): return formant(v)
        case (.filter, 0): return cutoff(v)
        case (.repeatFx, 1): return repeatRate(v)
        default: return percent(v)
        }
    }
}

extension Float {
    public func clamped(_ lo: Float, _ hi: Float) -> Float { Swift.min(Swift.max(self, lo), hi) }
}
