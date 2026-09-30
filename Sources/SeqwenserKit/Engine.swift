import CSeqw
import Foundation

/// Swift wrapper around the C++ sampler engine. Threading follows the C API: setup on the main thread,
/// `render` on the audio thread only.
public final class SamplerEngine: @unchecked Sendable {
    private let handle: OpaquePointer
    public let sampleRate: Double

    public init(sampleRate: Double, maxBlock: Int = 1024) {
        self.sampleRate = sampleRate
        handle = sq_create(sampleRate, Int32(maxBlock))!
    }
    deinit { sq_destroy(handle) }

    // MARK: sample
    @discardableResult
    public func load(_ audio: AudioData) -> Bool {
        guard !audio.isEmpty else { return false }
        return audio.left.withUnsafeBufferPointer { l in
            audio.right.withUnsafeBufferPointer { r in
                sq_load_pcm(handle, l.baseAddress, r.baseAddress, audio.frames, audio.sampleRate) == 1
            }
        }
    }
    public func loadDemo() { _ = sq_load_demo(handle) }
    public func clearSample() { sq_clear_sample(handle) }
    public var sampleFrames: Int { Int(sq_sample_frames(handle)) }
    /// Copy of the loaded audio (used to store it inside a project).
    public func copyAudio() -> AudioData? {
        let n = sampleFrames
        guard n > 0 else { return nil }
        var l = [Float](repeating: 0, count: n), r = l
        let got = Int(sq_copy_pcm(handle, &l, &r))
        return got == n ? AudioData(left: l, right: r, sampleRate: loadedSampleRate) : nil
    }
    public var hasSample: Bool { sampleFrames > 0 }
    public var loadedSampleRate: Double { sq_sample_rate(handle) }

    /// Min/max columns for drawing the region [from, to] (fractions of the sample).
    public func peaks(from: Float = 0, to: Float = 1, columns: Int) -> (mins: [Float], maxs: [Float]) {
        var mins = [Float](repeating: 0, count: max(1, columns)), maxs = mins
        sq_peaks(handle, from, to, Int32(mins.count), &mins, &maxs)
        return (mins, maxs)
    }
    /// Rough tempo of the sample; nil when it does not look like a loop.
    public func detectBPM() -> (bpm: Double, confidence: Float)? {
        var c: Float = 0
        let bpm = sq_detect_bpm(handle, &c)
        return bpm > 0 ? (bpm, c) : nil
    }

    // MARK: state
    public func apply(_ project: Project) {
        var st = Self.makeState(project)
        sq_set_state(handle, &st)
    }

    /// Fills the plain-C state from the project model.
    public static func makeState(_ p: Project) -> SqState {
        var st = sq_default_state()
        st.bank = Int32(p.pattern.bank)
        st.length = Int32(max(1, min(16, p.pattern.length)))
        st.playMode = Int32(p.pattern.playMode.rawValue)
        st.rate = Int32(p.pattern.rate.rawValue)
        st.probability = p.pattern.probability
        st.swing = p.pattern.swing
        st.bpm = p.bpm
        st.loop = p.pattern.loop ? 1 : 0
        st.volume = p.volume
        st.pan = p.pan
        st.trimStart = p.trimStart
        st.trimEnd = p.trimEnd
        for b in 0..<2 {
            for i in 0..<16 {
                guard let ps = sq_state_step(&st, Int32(b), Int32(i)) else { continue }
                let s = p.pattern.banks[b][i]
                ps.pointee.active = s.active ? 1 : 0
                ps.pointee.slice = Int32(s.slice)
                ps.pointee.velocity = s.velocity
                ps.pointee.prob = s.probability
                ps.pointee.cond = Int32(s.condition.rawValue)
                ps.pointee.velCond = Int32(s.velocityCondition.rawValue)
                ps.pointee.reverse = s.reverse ? 1 : 0
                ps.pointee.lofi = s.lofi ? 1 : 0
                ps.pointee.stretch = s.stretch ? 1 : 0
                for f in 0..<6 {
                    if let lock = s.locks[f], let has = sq_step_has_lock(ps, Int32(f)), let vals = sq_step_lock(ps, Int32(f)) {
                        has.pointee = 1
                        vals[0] = lock.a
                        vals[1] = lock.b
                    }
                }
            }
        }
        for f in 0..<6 {
            if let v = sq_state_fx(&st, Int32(f)) {
                v[0] = p.fx[f].a
                v[1] = p.fx[f].b
            }
        }
        let edges = p.sliceEdges
        if edges.count >= 2, let dst = sq_state_slice_edges(&st) {
            let count = min(edges.count - 1, 16)
            for i in 0...count { dst[i] = edges[min(i, edges.count - 1)] }
            st.numSlices = Int32(count)
        } else {
            st.numSlices = 0
        }
        return st
    }

    /// Auto-slices the current sample inside the project's trim region; returns the slice edges.
    public func autoSlice(project: Project, sensitivity: Float) -> [Float] {
        var st = Self.makeState(project)
        sq_auto_slice(handle, &st, sensitivity)
        return Self.edges(of: &st)
    }
    public static func equalSlices(project: Project, count: Int) -> [Float] {
        var st = Self.makeState(project)
        sq_equal_slices(&st, Int32(count))
        return edges(of: &st)
    }
    private static func edges(of st: inout SqState) -> [Float] {
        guard st.numSlices > 0, let src = sq_state_slice_edges(&st) else { return [] }
        return (0...Int(st.numSlices)).map { src[$0] }
    }

    // MARK: transport
    public func setPlaying(_ on: Bool) { sq_set_playing(handle, on ? 1 : 0) }
    public func trigger(slice: Int, velocity: Float = 1) { sq_trigger_pad(handle, Int32(slice), velocity) }
    var status: SqStatus { sq_status(handle) }
    public func collectGarbage() { sq_collect_garbage(handle) }

    /// Audio thread only.
    public func render(left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>, frames: Int) {
        sq_render(handle, left, right, Int32(frames))
    }

    /// Offline bounce of the pattern (loops `passes` times, plus a reverb/delay tail).
    public func bounce(project: Project, passes: Int = 1, tailSeconds: Double = 1.5) -> AudioData? {
        var st = Self.makeState(project)
        var out: UnsafeMutablePointer<Float>?
        let n = Int(sq_bounce(handle, &st, Int32(passes), tailSeconds, &out))
        guard n > 0, let buf = out else { return nil }
        defer { sq_free(buf) }
        let l = Array(UnsafeBufferPointer(start: buf, count: n))
        let r = Array(UnsafeBufferPointer(start: buf + n, count: n))
        return AudioData(left: l, right: r, sampleRate: sampleRate)
    }
}

/// Plain-Swift copy of the engine status (so UI code does not need the C module).
public struct EngineStatus: Equatable, Sendable {
    public var playing = false
    public var step = 0
    public var playhead: Float = -1
    public var level: Float = 0
    public var voices = 0
    public var pass = 0
    public init() {}
}

extension SamplerEngine {
    public var engineStatus: EngineStatus {
        let s = status
        var e = EngineStatus()
        e.playing = s.playing != 0
        e.step = Int(s.step)
        e.playhead = s.playhead
        e.level = s.level
        e.voices = Int(s.voices)
        e.pass = Int(s.pass)
        return e
    }
}
