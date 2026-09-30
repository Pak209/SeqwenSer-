import AVFoundation
import Combine
import Foundation
import SwiftUI
import SeqwenserKit

enum MainMode: String, CaseIterable, Identifiable {
    case sample = "SAMPLE", sequence = "SEQUENCE", perform = "PERFORM", mix = "MIX"
    var id: String { rawValue }
    var icon: String { ["waveform", "square.grid.4x3.fill", "hand.tap.fill", "slider.vertical.3"][Self.allCases.firstIndex(of: self)!] }
}

enum ActiveSheet: Identifiable, Equatable {
    case sample, pattern, stepFx(Int), projects, export, settings
    var id: String {
        switch self {
        case .sample: return "sample"
        case .pattern: return "pattern"
        case .stepFx(let i): return "step\(i)"
        case .projects: return "projects"
        case .export: return "export"
        case .settings: return "settings"
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    @Published var project = Project() { didSet { projectChanged(oldValue) } }
    @Published var mode: MainMode = .sample
    @Published var sheet: ActiveSheet?
    @Published var selectedStep = 0
    @Published var fxTab: FxKind = .pitch
    @Published var status = EngineStatus()
    @Published var waveform: (mins: [Float], maxs: [Float]) = ([], [])
    @Published var message: String?
    @Published var isRecording = false
    @Published var recordLevel: Float = 0
    @Published var recordSeconds: Double = 0
    @Published var sampleInfo = "No sample"
    @Published var exportURL: URL?
    @Published var savedProjects: [String] = []
    @Published var micDenied = false
    @Published var tapTimes: [Date] = []

    let sampler: SamplerEngine
    private let output: AudioOutput
    private let recorder = MicRecorder()
    private(set) var audio: AudioData?
    let store: ProjectStore
    private var timer: Timer?
    private var recordStart = Date()
    private var suppressPush = false
    private var dirtySinceSave = false

    init(storeRoot: URL = ProjectStore.defaultRoot(), startAudio: Bool = true) {
        try? FileManager.default.createDirectory(at: storeRoot, withIntermediateDirectories: true)
        store = ProjectStore(root: storeRoot)
        if startAudio { AudioOutput.configureSession() }
        let hw = AVAudioSession.sharedInstance().sampleRate
        let rate = startAudio && hw >= 8000 ? hw : 48000
        sampler = SamplerEngine(sampleRate: rate, maxBlock: 4096)
        output = AudioOutput(sampler: sampler)
        if startAudio { output.start() }
        recorder.onLimit = { [weak self] in self?.finishRecording() }
        loadDemoKit()
        refreshProjects()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
        NotificationCenter.default.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] n in
            let type = (n.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt).flatMap(AVAudioSession.InterruptionType.init(rawValue:))
            Task { @MainActor in
                if type == .ended { AudioOutput.configureSession(); self?.output.restartIfNeeded() }
                else if type == .began { self?.stop() }
            }
        }
    }

    // MARK: engine sync
    private func projectChanged(_ old: Project) {
        if suppressPush { return }
        dirtySinceSave = true
        sampler.apply(project)
    }

    private func tick() {
        let s = sampler.engineStatus
        if s != status { status = s }
        if isRecording {
            recordLevel = recorder.level
            recordSeconds = Date().timeIntervalSince(recordStart)
        }
        sampler.collectGarbage()
    }

    var isPlaying: Bool { status.playing }

    // MARK: transport
    func play() { sampler.apply(project); output.restartIfNeeded(); sampler.setPlaying(true) }
    func stop() { sampler.setPlaying(false) }
    func togglePlay() { isPlaying ? stop() : play() }
    func tapTempo() {
        let now = Date()
        tapTimes = tapTimes.filter { now.timeIntervalSince($0) < 2.5 } + [now]
        if tapTimes.count >= 2 {
            let gaps = zip(tapTimes.dropFirst(), tapTimes).map { $0.timeIntervalSince($1) }
            let avg = gaps.reduce(0, +) / Double(gaps.count)
            if avg > 0.15 && avg < 2 { project.bpm = Float(60 / avg).clamped(30, 300).rounded() }
        }
    }
    func trigger(slice: Int) { output.restartIfNeeded(); sampler.trigger(slice: slice, velocity: 1) }

    // MARK: pattern editing
    func toggleStep(_ i: Int) {
        project.pattern.banks[project.pattern.bank][i].active.toggle()
        selectedStep = i
        if project.pattern.banks[project.pattern.bank][i].active { trigger(slice: project.pattern.banks[project.pattern.bank][i].slice % max(1, project.sliceCount)) }
    }
    func step(_ i: Int) -> Step { project.pattern.banks[project.pattern.bank][i] }
    func setStep(_ i: Int, _ s: Step) { project.pattern.banks[project.pattern.bank][i] = s }
    func clearPattern() { for i in 0..<16 { project.pattern.banks[project.pattern.bank][i].active = false; project.pattern.banks[project.pattern.bank][i].locks = Array(repeating: nil, count: 6) } }

    /// Fills the pattern from the slices: one step per slice, in order.
    func fillFromSlices() {
        let n = max(1, project.sliceCount)
        for i in 0..<16 {
            var s = project.pattern.banks[project.pattern.bank][i]
            s.slice = i % n
            s.active = i < n || i % 2 == 0
            project.pattern.banks[project.pattern.bank][i] = s
        }
    }

    // MARK: effect values
    func fxBinding(_ k: FxKind, _ p: Int) -> Binding<Float> {
        Binding(get: { p == 0 ? self.project.fx[k.rawValue].a : self.project.fx[k.rawValue].b },
                set: { if p == 0 { self.project.fx[k.rawValue].a = $0 } else { self.project.fx[k.rawValue].b = $0 } })
    }
    /// Binding to a step's lock for one effect parameter; setting creates the lock (starting from the global value).
    func lockBinding(step i: Int, fx k: FxKind, param p: Int) -> Binding<Float> {
        Binding(get: {
            let s = self.step(i)
            let v = s.locks[k.rawValue] ?? self.project.fx[k.rawValue]
            return p == 0 ? v.a : v.b
        }, set: { nv in
            var s = self.step(i)
            var v = s.locks[k.rawValue] ?? self.project.fx[k.rawValue]
            if p == 0 { v.a = nv } else { v.b = nv }
            s.locks[k.rawValue] = v
            self.setStep(i, s)
        })
    }

    // MARK: samples
    func loadDemoKit() {
        sampler.loadDemo()
        audio = nil
        var p = Project()
        p.name = "KIT_01"
        p.kitName = "Neon Break"
        p.bpm = 120
        setProject(p)
        project.sliceEdges = sampler.autoSlice(project: project, sensitivity: 0.6)
        let n = max(1, project.sliceCount)
        let hits: Set<Int> = [0, 3, 4, 8, 10, 12, 14]
        for i in 0..<16 {
            project.pattern.banks[0][i].slice = i % n
            project.pattern.banks[0][i].active = hits.contains(i)
            project.pattern.banks[1][i].slice = (i * 3) % n
        }
        project.pattern.banks[0][3].locks[FxKind.pitch.rawValue] = FxValue(0.5 - 7.0 / 24.0, 0.5)
        project.pattern.banks[0][8].lofi = true
        project.pattern.banks[0][12].reverse = true
        refreshWaveform()
        sampleInfo = "Built-in demo break  |  \(String(format: "%.1f", Double(sampler.sampleFrames) / max(1, sampler.loadedSampleRate))) s"
    }

    private func setProject(_ p: Project) {
        suppressPush = true
        project = p
        suppressPush = false
        sampler.apply(project)
    }

    func refreshWaveform() {
        waveform = sampler.peaks(from: 0, to: 1, columns: 400)
    }

    func importAudio(url: URL) {
        Task {
            do {
                let data = try await Task.detached(priority: .userInitiated) { try AudioImporter.read(url: url) }.value
                self.installSample(data, name: url.deletingPathExtension().lastPathComponent, source: url.lastPathComponent)
            } catch {
                self.message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            }
        }
    }

    func importMany(_ urls: [URL]) { if let u = urls.first { importAudio(url: u) } }

    func installSample(_ data: AudioData, name: String, source: String) {
        stop()
        guard sampler.load(data) else { message = "That audio is too short to use."; return }
        audio = data
        var p = project
        p.kitName = String(name.prefix(24))
        p.sampleSource = source
        p.trimStart = 0
        p.trimEnd = 1
        p.sliceEdges = []
        setProject(p)
        if let t = sampler.detectBPM() { project.bpm = Float(t.bpm).rounded().clamped(30, 300) }
        if project.autoSlice { project.sliceEdges = sampler.autoSlice(project: project, sensitivity: project.sensitivity) }
        else { project.sliceEdges = SamplerEngine.equalSlices(project: project, count: 16) }
        fillFromSlices()
        refreshWaveform()
        sampleInfo = "\(source)  |  \(String(format: "%.1f", data.seconds)) s  |  \(project.sliceCount) slices"
        message = nil
    }

    func resliceNow() {
        var p = project
        p.sliceEdges = []
        setProject(p)
        project.sliceEdges = project.autoSlice
            ? sampler.autoSlice(project: project, sensitivity: project.sensitivity)
            : SamplerEngine.equalSlices(project: project, count: 16)
        sampleInfo = sampleInfo.components(separatedBy: "  |  ").first.map { "\($0)  |  \(project.sliceCount) slices" } ?? sampleInfo
    }

    func setTrim(start: Float? = nil, end: Float? = nil) {
        var p = project
        if let s = start { p.trimStart = s.clamped(0, p.trimEnd - 0.02) }
        if let e = end { p.trimEnd = e.clamped(p.trimStart + 0.02, 1) }
        project = p
    }
    func trimChangeEnded() { if !project.sliceEdges.isEmpty { resliceNow() } }

    // MARK: recording
    func toggleRecord() {
        if isRecording { finishRecording(); return }
        MicRecorder.requestPermission { [weak self] ok in
            guard let self else { return }
            guard ok else { self.micDenied = true; self.message = "Microphone access is off. Turn it on in Settings > Seqwenser."; return }
            self.micDenied = false
            do {
                self.stop()
                AudioOutput.configureSession()
                self.output.restartIfNeeded()
                try self.recorder.start()
                self.recordStart = Date()
                self.isRecording = true
            } catch {
                self.message = "Could not start recording: \(error.localizedDescription)"
            }
        }
    }

    func finishRecording() {
        guard isRecording else { return }
        isRecording = false
        if let data = recorder.stop() {
            installSample(data, name: "REC \(Self.timeStamp())", source: "Microphone")
            sheet = nil
        } else {
            message = "Nothing was recorded."
        }
    }

    static func timeStamp() -> String {
        let f = DateFormatter(); f.dateFormat = "HHmm"; return f.string(from: Date())
    }

    // MARK: projects
    func refreshProjects() { savedProjects = store.list() }

    func saveProject() {
        do {
            var p = project
            if p.name.trimmingCharacters(in: .whitespaces).isEmpty { p.name = "KIT_01" }
            try store.save(p, audio: sampler.copyAudio())
            refreshProjects()
            dirtySinceSave = false
            message = "Saved \"\(p.name)\"."
        } catch {
            message = "Could not save: \(error.localizedDescription)"
        }
    }

    func loadProject(_ name: String) {
        do {
            let (p, a) = try store.load(name: name)
            stop()
            if let a, sampler.load(a) { audio = a }
            else if p.sampleFile == nil { sampler.loadDemo(); audio = nil }
            else { message = "The audio of \"\(name)\" is missing. The pattern was loaded without sound."; sampler.clearSample(); audio = nil }
            setProject(p)
            refreshWaveform()
            sampleInfo = "\(p.sampleSource ?? "Built-in demo break")  |  \(project.sliceCount) slices"
            sheet = nil
            dirtySinceSave = false
        } catch {
            message = "Could not open \"\(name)\": \(error.localizedDescription)"
        }
    }

    func deleteProject(_ name: String) {
        try? store.delete(name: name)
        refreshProjects()
    }

    func newProject() { stop(); loadDemoKit(); sheet = nil }

    // MARK: export
    func exportBounce(passes: Int, bits: Int) {
        guard sampler.hasSample else { message = "Load a sample first."; return }
        guard let out = sampler.bounce(project: project, passes: passes, tailSeconds: 1.5) else { message = "Nothing to export."; return }
        let name = ProjectStore.safeName(project.name) + "-bounce.wav"
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(name)
        do {
            try WAV.write(out, to: url, bitsPerSample: bits)
            exportURL = url
            message = "Bounce ready: \(String(format: "%.1f", out.seconds)) s."
        } catch {
            message = "Export failed: \(error.localizedDescription)"
        }
    }

    // MARK: launch arguments (used by CI screenshots)
    func applyLaunchArguments(_ args: [String]) {
        func value(_ key: String) -> String? { args.firstIndex(of: key).flatMap { $0 + 1 < args.count ? args[$0 + 1] : nil } }
        if let m = value("-seqw-mode"), let mm = MainMode(rawValue: m.uppercased()) { mode = mm }
        if let s = value("-seqw-sheet") {
            switch s {
            case "sample": sheet = .sample
            case "pattern": sheet = .pattern
            case "step": sheet = .stepFx(4); selectedStep = 4
            case "projects": sheet = .projects
            case "export": sheet = .export
            default: break
            }
        }
        if args.contains("-seqw-play") { play() }
        if let n = value("-seqw-select"), let i = Int(n) { selectedStep = i }
    }
}
