import AVFoundation
import Foundation
import SeqwenserKit

enum ImportError: LocalizedError {
    case unreadable(String), empty(String), tooLong(String)
    var errorDescription: String? {
        switch self {
        case .unreadable(let n): return "Could not read \(n). It may be damaged, protected or not an audio file."
        case .empty(let n): return "\(n) contains no audio."
        case .tooLong(let n): return "\(n) is longer than 10 minutes. Trim it first."
        }
    }
}

enum AudioImporter {
    static let maxSeconds = 600.0

    /// Decodes any file AVFoundation understands (WAV, AIFF, CAF, M4A/AAC, MP3, FLAC, ...) into stereo Float.
    static func read(url: URL) throws -> AudioData {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let name = url.lastPathComponent
        let file: AVAudioFile
        do { file = try AVAudioFile(forReading: url, commonFormat: .pcmFormatFloat32, interleaved: false) }
        catch { throw ImportError.unreadable(name) }
        let rate = file.processingFormat.sampleRate
        let total = file.length
        guard total > 64, rate > 0 else { throw ImportError.empty(name) }
        guard Double(total) / rate <= maxSeconds else { throw ImportError.tooLong(name) }
        let channels = Int(file.processingFormat.channelCount)
        var left = [Float](); left.reserveCapacity(Int(total))
        var right = [Float](); right.reserveCapacity(Int(total))
        let chunk: AVAudioFrameCount = 1 << 16
        guard let buf = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: chunk) else { throw ImportError.unreadable(name) }
        while file.framePosition < total {
            do { try file.read(into: buf, frameCount: chunk) } catch { throw ImportError.unreadable(name) }
            let n = Int(buf.frameLength)
            if n == 0 { break }
            guard let d = buf.floatChannelData else { throw ImportError.unreadable(name) }
            left.append(contentsOf: UnsafeBufferPointer(start: d[0], count: n))
            right.append(contentsOf: UnsafeBufferPointer(start: d[channels > 1 ? 1 : 0], count: n))
        }
        guard left.count > 64 else { throw ImportError.empty(name) }
        for i in 0..<left.count {
            if !left[i].isFinite { left[i] = 0 }
            if !right[i].isFinite { right[i] = 0 }
        }
        return AudioData(left: left, right: right, sampleRate: rate)
    }
}

/// Microphone recorder (max 60 s). Records through AVAudioEngine's input tap, mono mic duplicated to both sides.
final class MicRecorder {
    static let maxSeconds = 60.0
    private let engine = AVAudioEngine()
    private let lock = NSLock()
    private var left = [Float]()
    private var rate = 44100.0
    private(set) var isRecording = false
    private(set) var level: Float = 0
    var onLimit: (() -> Void)?

    static func requestPermission(_ done: @escaping (Bool) -> Void) {
        if #available(iOS 17.0, *) {
            AVAudioApplication.requestRecordPermission { ok in DispatchQueue.main.async { done(ok) } }
        } else {
            AVAudioSession.sharedInstance().requestRecordPermission { ok in DispatchQueue.main.async { done(ok) } }
        }
    }

    func start() throws {
        guard !isRecording else { return }
        let input = engine.inputNode
        let fmt = input.outputFormat(forBus: 0)
        guard fmt.sampleRate > 0, fmt.channelCount > 0 else {
            throw NSError(domain: "Seqwenser", code: 1, userInfo: [NSLocalizedDescriptionKey: "No microphone input is available."])
        }
        rate = fmt.sampleRate
        lock.lock(); left.removeAll(keepingCapacity: true); left.reserveCapacity(Int(rate * 10)); lock.unlock()
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 1024, format: fmt) { [weak self] buffer, _ in
            guard let self, let d = buffer.floatChannelData else { return }
            let n = Int(buffer.frameLength)
            var peak: Float = 0
            self.lock.lock()
            let limit = Int(self.rate * MicRecorder.maxSeconds)
            let room = max(0, limit - self.left.count)
            let take = min(n, room)
            self.left.append(contentsOf: UnsafeBufferPointer(start: d[0], count: take))
            let hitLimit = self.left.count >= limit
            self.lock.unlock()
            for i in 0..<n { peak = max(peak, abs(d[0][i])) }
            self.level = peak
            if hitLimit { DispatchQueue.main.async { self.onLimit?() } }
        }
        engine.prepare()
        try engine.start()
        isRecording = true
    }

    func stop() -> AudioData? {
        guard isRecording else { return nil }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        isRecording = false
        lock.lock(); let data = left; lock.unlock()
        guard data.count > 512 else { return nil }
        return AudioData(left: data, right: data, sampleRate: rate)
    }
}

/// Real-time output: an AVAudioSourceNode whose render block calls the C++ engine directly.
final class AudioOutput {
    private let engine = AVAudioEngine()
    private var node: AVAudioSourceNode?
    let sampler: SamplerEngine
    private(set) var running = false

    init(sampler: SamplerEngine) { self.sampler = sampler }

    static func configureSession() {
        let s = AVAudioSession.sharedInstance()
        do {
            try s.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker, .allowBluetoothA2DP, .mixWithOthers])
            try s.setPreferredIOBufferDuration(0.005)
            try s.setActive(true)
        } catch {
            // Falls back to playback-only (e.g. simulator without input, or permission refused).
            try? s.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try? s.setActive(true)
        }
    }

    func start() {
        guard !running else { return }
        let fmt = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: sampler.sampleRate, channels: 2, interleaved: false)!
        let engineRef = sampler
        let src = AVAudioSourceNode(format: fmt) { _, _, frames, abl in
            let list = UnsafeMutableAudioBufferListPointer(abl)
            guard list.count >= 2,
                  let l = list[0].mData?.assumingMemoryBound(to: Float.self),
                  let r = list[1].mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            engineRef.render(left: l, right: r, frames: Int(frames))
            return noErr
        }
        node = src
        engine.attach(src)
        engine.connect(src, to: engine.mainMixerNode, format: fmt)
        engine.mainMixerNode.outputVolume = 1
        do { try engine.start(); running = true } catch { running = false }
    }

    func restartIfNeeded() {
        if running && !engine.isRunning { try? engine.start() }
    }

    func stop() { engine.pause(); running = false }
}
