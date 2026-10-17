import XCTest
@testable import Seqwenser
@testable import SeqwenserKit

@MainActor
final class AppModelTests: XCTestCase {
    func makeModel() -> AppModel {
        let root = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("seqw-app-\(UUID().uuidString)")
        return AppModel(storeRoot: root, startAudio: false)
    }

    func testDemoKitLoadsWithSlicesAndPattern() {
        let m = makeModel()
        XCTAssertTrue(m.sampler.hasSample)
        XCTAssertGreaterThanOrEqual(m.project.sliceCount, 4)
        XCTAssertTrue(m.project.pattern.banks[0].contains { $0.active })
        XCTAssertFalse(m.waveform.mins.isEmpty)
    }

    func testToggleStepAndLocksReachEngineState() {
        let m = makeModel()
        m.project.pattern.banks[0][1].active = false
        m.toggleStep(1)
        XCTAssertTrue(m.step(1).active)
        let b = m.lockBinding(step: 1, fx: .filter, param: 0)
        b.wrappedValue = 0.25
        XCTAssertEqual(m.step(1).locks[FxKind.filter.rawValue]?.a, 0.25)
        XCTAssertNil(m.step(1).locks[FxKind.pitch.rawValue])
    }

    func testSaveLoadProjectRoundTrip() {
        let m = makeModel()
        m.project.name = "Unit Test Kit"
        m.project.bpm = 97
        m.project.fx[FxKind.drive.rawValue].a = 0.7
        m.saveProject()
        XCTAssertTrue(m.savedProjects.contains("Unit Test Kit"))
        m.project.bpm = 140
        m.loadProject("Unit Test Kit")
        XCTAssertEqual(m.project.bpm, 97)
        XCTAssertEqual(m.project.fx[FxKind.drive.rawValue].a, 0.7, accuracy: 1e-6)
        XCTAssertTrue(m.sampler.hasSample)
    }

    func testInstallSampleSlicesAndFillsPattern() {
        let m = makeModel()
        let n = 44100 * 2
        let s = (0..<n).map { i -> Float in
            let t = Double(i) / 44100
            let hit = t.truncatingRemainder(dividingBy: 0.5)
            return Float(sin(2 * Double.pi * 200 * t) * exp(-hit * 14) * 0.8)
        }
        m.installSample(AudioData(left: s, right: s, sampleRate: 44100), name: "Clicks", source: "clicks.wav")
        XCTAssertEqual(m.project.kitName, "Clicks")
        XCTAssertGreaterThanOrEqual(m.project.sliceCount, 2)
        XCTAssertTrue(m.step(0).active)
    }

    func testTapTempo() {
        let m = makeModel()
        let now = Date()
        m.tapTimes = [now.addingTimeInterval(-1.0), now.addingTimeInterval(-0.5)]
        m.tapTempo()
        XCTAssertEqual(m.project.bpm, 120, accuracy: 6)
    }

    func testExportBounceWritesWav() throws {
        let m = makeModel()
        m.exportBounce(passes: 1, bits: 16)
        let url = try XCTUnwrap(m.exportURL)
        let audio = try WAV.read(url)
        XCTAssertGreaterThan(audio.seconds, 1.5)
        XCTAssertGreaterThan(audio.left.map { abs($0) }.max() ?? 0, 0.1)
    }

    func testBadFileGivesMessage() async {
        let m = makeModel()
        let bad = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("not-audio.wav")
        try? Data("hello".utf8).write(to: bad)
        m.importAudio(url: bad)
        for _ in 0..<50 where m.message == nil { try? await Task.sleep(nanoseconds: 100_000_000) }
        XCTAssertNotNil(m.message)
        XCTAssertTrue(m.sampler.hasSample)   // previous sample is kept
    }

    func testImportRealWavThroughAVFoundation() async throws {
        let m = makeModel()
        let n = 44100
        let s = (0..<n).map { Float(0.5 * sin(2 * Double.pi * 300 * Double($0) / 44100)) }
        let url = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("import test \(UUID().uuidString).wav")
        try WAV.write(AudioData(left: s, right: s, sampleRate: 44100), to: url)
        m.importAudio(url: url)
        for _ in 0..<50 where m.project.sampleSource != url.lastPathComponent { try await Task.sleep(nanoseconds: 100_000_000) }
        XCTAssertEqual(m.project.sampleSource, url.lastPathComponent)
        XCTAssertEqual(Double(m.sampler.sampleFrames), Double(n), accuracy: 2)
    }
}
