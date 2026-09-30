import Foundation

public enum StoreError: Error, Equatable {
    case notFound(String), badProject(String)
}

/// Saves projects as folders: `<root>/<name>/project.json` + `sample.wav` (the audio the project uses, so it
/// keeps working when the original file is gone).
public struct ProjectStore {
    public let root: URL
    public init(root: URL) { self.root = root }

    public static func defaultRoot() -> URL {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSTemporaryDirectory())
        return docs.appendingPathComponent("Projects", isDirectory: true)
    }

    /// File-system safe version of a project name.
    public static func safeName(_ name: String) -> String {
        let bad = CharacterSet(charactersIn: "/\\:*?\"<>|\n\r\t").union(.controlCharacters)
        var s = name.components(separatedBy: bad).joined(separator: "_").trimmingCharacters(in: .whitespaces)
        while s.hasPrefix(".") { s.removeFirst() }
        if s.isEmpty { s = "Untitled" }
        return String(s.prefix(60))
    }

    public func folder(for name: String) -> URL { root.appendingPathComponent(Self.safeName(name), isDirectory: true) }

    public func save(_ project: Project, audio: AudioData?) throws {
        var p = project
        let dir = folder(for: p.name)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        if let a = audio, !a.isEmpty {
            try WAV.write(a, to: dir.appendingPathComponent("sample.wav"), bitsPerSample: 24)
            p.sampleFile = "sample.wav"
        } else if audio == nil, p.sampleFile != nil,
                  !FileManager.default.fileExists(atPath: dir.appendingPathComponent(p.sampleFile!).path) {
            p.sampleFile = nil
        }
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        try enc.encode(p).write(to: dir.appendingPathComponent("project.json"), options: .atomic)
    }

    public func load(name: String) throws -> (Project, AudioData?) {
        let dir = folder(for: name)
        let json = dir.appendingPathComponent("project.json")
        guard let data = try? Data(contentsOf: json) else { throw StoreError.notFound(name) }
        let p: Project
        do { p = try JSONDecoder().decode(Project.self, from: data) } catch { throw StoreError.badProject(name) }
        var audio: AudioData?
        if let f = p.sampleFile { audio = try? WAV.read(dir.appendingPathComponent(f)) }
        return (Self.sanitised(p), audio)
    }

    public func list() -> [String] {
        let items = (try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        return items.filter { FileManager.default.fileExists(atPath: $0.appendingPathComponent("project.json").path) }
            .sorted { (a, b) in
                let da = (try? a.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                let db = (try? b.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                return da > db
            }
            .map { $0.lastPathComponent }
    }

    public func delete(name: String) throws {
        try FileManager.default.removeItem(at: folder(for: name))
    }

    /// Repairs a decoded project so the engine never sees out-of-range data (old or hand-edited files).
    public static func sanitised(_ input: Project) -> Project {
        var p = input
        if p.fx.count != FxKind.allCases.count { p.fx = Project.defaultFx }
        p.fx = p.fx.map { FxValue($0.a.clamped(0, 1), $0.b.clamped(0, 1)) }
        if p.pattern.banks.count != 2 || p.pattern.banks.contains(where: { $0.count != 16 }) { p.pattern = Pattern() }
        for b in 0..<2 {
            for i in 0..<16 {
                var s = p.pattern.banks[b][i]
                if s.locks.count != 6 { s.locks = Array(repeating: nil, count: 6) }
                s.velocity = s.velocity.clamped(0, 1)
                s.probability = s.probability.clamped(0, 1)
                s.slice = max(0, min(15, s.slice))
                p.pattern.banks[b][i] = s
            }
        }
        p.pattern.bank = max(0, min(1, p.pattern.bank))
        p.pattern.length = max(1, min(16, p.pattern.length))
        p.pattern.probability = p.pattern.probability.clamped(0, 1)
        p.pattern.swing = p.pattern.swing.clamped(0, 1)
        p.bpm = p.bpm.clamped(30, 300)
        p.trimStart = p.trimStart.clamped(0, 0.99)
        p.trimEnd = p.trimEnd.clamped(p.trimStart + 0.005, 1)
        p.volume = p.volume.clamped(0, 1.5)
        p.pan = p.pan.clamped(-1, 1)
        if p.sliceEdges.count < 2 || p.sliceEdges.count > 17 || zip(p.sliceEdges, p.sliceEdges.dropFirst()).contains(where: { $0 >= $1 }) {
            p.sliceEdges = []
        }
        return p
    }
}
