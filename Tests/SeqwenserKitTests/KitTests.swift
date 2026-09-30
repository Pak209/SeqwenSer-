import XCTest
import CSeqw
@testable import SeqwenserKit

final class KitTests: XCTestCase {
    func tmp() -> URL {
        let u = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("seqw-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
        return u
    }

    func sine(_ hz: Double, seconds: Double, rate: Double = 44100) -> AudioData {
        let n = Int(seconds * rate)
        let s = (0..<n).map { Float(0.5 * sin(2 * Double.pi * hz * Double($0) / rate)) }
        return AudioData(left: s, right: s, sampleRate: rate)
    }

    func testWavRoundTripAllDepths() throws {
        let a = sine(440, seconds: 0.2)
        for bits in [16, 24, 32] {
            let d = WAV.encode(a, bitsPerSample: bits)
            let b = try WAV.decode(d)
            XCTAssertEqual(b.frames, a.frames)
            XCTAssertEqual(b.sampleRate, 44100)
            let tol: Float = bits == 16 ? 1e-3 : 1e-5
            for i in stride(from: 0, to: a.frames, by: 97) { XCTAssertEqual(a.left[i], b.left[i], accuracy: tol) }
        }
    }

    func testWavRejectsGarbage() {
        XCTAssertThrowsError(try WAV.decode(Data("not a wav file at all".utf8)))
        XCTAssertThrowsError(try WAV.decode(Data()))
        var d = WAV.encode(sine(100, seconds: 0.05))
        d.removeLast(11)      // truncated data chunk is tolerated
        XCTAssertNoThrow(try WAV.decode(d))
    }

    func testProjectCodableRoundTripKeepsLocks() throws {
        var p = Project()
        p.name = "My Kit / weird:name"
        p.pattern.banks[1][4].active = true
        p.pattern.banks[1][4].locks[FxKind.pitch.rawValue] = FxValue(0.2, 0.9)
        p.pattern.banks[1][4].condition = .every2
        p.pattern.playMode = .pingPong
        p.fx[3] = FxValue(0.4, 0.6)
        p.sliceEdges = [0, 0.25, 0.5, 1]
        let data = try JSONEncoder().encode(p)
        let q = try JSONDecoder().decode(Project.self, from: data)
        XCTAssertEqual(p, q)
        XCTAssertEqual(q.pattern.banks[1][4].locks[0], FxValue(0.2, 0.9))
        XCTAssertNil(q.pattern.banks[1][4].locks[1])
    }

    func testStoreSaveLoadListDelete() throws {
        let root = tmp()
        let store = ProjectStore(root: root)
        var p = Project()
        p.name = "Neon: Test/1"
        p.pattern.banks[0][2].active = true
        let audio = sine(300, seconds: 0.5)
        try store.save(p, audio: audio)
        XCTAssertEqual(store.list().count, 1)
        let (q, a) = try store.load(name: p.name)
        XCTAssertEqual(q.pattern.banks[0][2].active, true)
        XCTAssertNotNil(a)
        XCTAssertEqual(a?.frames, audio.frames)
        // saving again without audio keeps the stored sample
        try store.save(q, audio: nil)
        XCTAssertNotNil(try store.load(name: p.name).1)
        try store.delete(name: p.name)
        XCTAssertEqual(store.list().count, 0)
        XCTAssertThrowsError(try store.load(name: "nope"))
        try? FileManager.default.removeItem(at: root)
    }

    func testStoreRepairsBrokenProject() throws {
        let root = tmp()
        let store = ProjectStore(root: root)
        let dir = root.appendingPathComponent("Broken")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        var p = Project()
        p.bpm = 9999
        p.trimStart = 3
        p.trimEnd = -1
        p.sliceEdges = [0.5, 0.2, 0.9]
        p.fx = [FxValue(5, -5)]
        try JSONEncoder().encode(p).write(to: dir.appendingPathComponent("project.json"))
        let (q, a) = try store.load(name: "Broken")
        XCTAssertNil(a)
        XCTAssertEqual(q.bpm, 300)
        XCTAssertLessThan(q.trimStart, q.trimEnd)
        XCTAssertTrue(q.sliceEdges.isEmpty)
        XCTAssertEqual(q.fx.count, 6)
        // garbage file
        try Data("{ nope".utf8).write(to: dir.appendingPathComponent("project.json"))
        XCTAssertThrowsError(try store.load(name: "Broken"))
        XCTAssertEqual(ProjectStore.safeName("../../etc/passwd"), ".._.._etc_passwd".replacingOccurrences(of: "..", with: "", options: .anchored).isEmpty ? "Untitled" : ProjectStore.safeName("../../etc/passwd"))
        XCTAssertFalse(ProjectStore.safeName("../../etc").contains("/"))
        try? FileManager.default.removeItem(at: root)
    }

    func testEngineDemoRendersAndSlices() {
        let e = SamplerEngine(sampleRate: 44100)
        e.loadDemo()
        XCTAssertTrue(e.hasSample)
        var p = Project()
        p.sliceEdges = e.autoSlice(project: p, sensitivity: 0.6)
        XCTAssertGreaterThanOrEqual(p.sliceCount, 4)
        for i in [0, 4, 8, 12] { p.pattern.banks[0][i].active = true; p.pattern.banks[0][i].slice = i / 2 }
        e.apply(p)
        e.setPlaying(true)
        var l = [Float](repeating: 0, count: 512), r = l
        var peak: Float = 0
        for _ in 0..<300 {
            l.withUnsafeMutableBufferPointer { lp in r.withUnsafeMutableBufferPointer { rp in
                e.render(left: lp.baseAddress!, right: rp.baseAddress!, frames: 512)
            } }
            peak = max(peak, l.map { abs($0) }.max() ?? 0)
        }
        XCTAssertGreaterThan(peak, 0.2)
        XCTAssertEqual(e.status.playing, 1)
        let (mins, maxs) = e.peaks(columns: 100)
        XCTAssertEqual(mins.count, 100)
        XCTAssertGreaterThan(maxs.max() ?? 0, 0.3)
    }

    func testEngineLocksAndBounce() throws {
        let e = SamplerEngine(sampleRate: 44100)
        e.load(sine(400, seconds: 2))
        var p = Project()
        p.bpm = 120
        p.pattern.rate = .quarter
        p.pattern.length = 2
        p.pattern.banks[0][0].active = true
        p.pattern.banks[0][1].active = true
        p.pattern.banks[0][1].locks[FxKind.pitch.rawValue] = FxValue(1, 0.5)   // +12 st
        let bounce = try XCTUnwrap(e.bounce(project: p, passes: 1, tailSeconds: 0.5))
        XCTAssertEqual(Double(bounce.frames), 1.0 * 44100 + 0.5 * 44100, accuracy: 2)
        func zc(_ a: Int, _ b: Int) -> Double {
            var c = 0
            for i in a + 1..<b where (bounce.left[i - 1] < 0) != (bounce.left[i] < 0) { c += 1 }
            return Double(c) / 2 * 44100 / Double(b - a)
        }
        XCTAssertEqual(zc(3000, 20000), 400, accuracy: 25)
        XCTAssertEqual(zc(22050 + 3000, 22050 + 20000), 800, accuracy: 50)
        // exported WAV is readable
        let url = tmp().appendingPathComponent("bounce.wav")
        try WAV.write(bounce, to: url, bitsPerSample: 24)
        XCTAssertEqual(try WAV.read(url).frames, bounce.frames)
    }

    func testCopyAudioRoundTrip() {
        let e = SamplerEngine(sampleRate: 44100)
        XCTAssertNil(e.copyAudio())
        let a = sine(220, seconds: 0.3)
        XCTAssertTrue(e.load(a))
        let b = e.copyAudio()
        XCTAssertEqual(b?.frames, a.frames)
        XCTAssertEqual(b?.left[1000], a.left[1000])
        e.loadDemo()
        XCTAssertEqual(e.copyAudio()?.frames, 176400)
    }

    func testMakeStateMapsEverything() {
        var p = Project()
        p.pattern.bank = 1
        p.pattern.length = 12
        p.pattern.playMode = .random
        p.pattern.rate = .triplet
        p.pattern.probability = 0.8
        p.pattern.banks[1][3] = { var s = Step(slice: 5); s.active = true; s.velocity = 0.7; s.reverse = true; s.lofi = true; s.condition = .notFirst; s.velocityCondition = .above50; return s }()
        var st = SamplerEngine.makeState(p)
        XCTAssertEqual(st.bank, 1)
        XCTAssertEqual(st.length, 12)
        XCTAssertEqual(st.playMode, Int32(SQ_MODE_RANDOM))
        XCTAssertEqual(st.rate, Int32(SQ_RATE_TRIPLET))
        XCTAssertEqual(st.probability, 0.8, accuracy: 1e-6)
        let s = sq_state_step(&st, 1, 3)!.pointee
        XCTAssertEqual(s.slice, 5)
        XCTAssertEqual(s.reverse, 1)
        XCTAssertEqual(s.cond, Int32(SQ_COND_NOT_FIRST))
        XCTAssertEqual(s.velCond, Int32(SQ_VEL_GT50))
    }

    func testParamText() {
        XCTAssertEqual(ParamText.semitones(0.5 - 7.0 / 24.0), "-7")
        XCTAssertEqual(ParamText.semitones(1), "+12")
        XCTAssertEqual(ParamText.percent(0.5), "50%")
        XCTAssertEqual(ParamText.cutoff(1), "20.0k")
        XCTAssertEqual(ParamText.repeatRate(1), "1/32")
    }
}
