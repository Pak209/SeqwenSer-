import Foundation

/// Decoded audio (stereo, Float, native sample rate).
public struct AudioData: Sendable {
    public var left: [Float]
    public var right: [Float]
    public var sampleRate: Double

    public init(left: [Float], right: [Float]? = nil, sampleRate: Double) {
        self.left = left
        self.right = right ?? left
        self.sampleRate = sampleRate
    }
    public var frames: Int { left.count }
    public var seconds: Double { sampleRate > 0 ? Double(frames) / sampleRate : 0 }
    public var isEmpty: Bool { frames < 64 }
}

public enum WAVError: Error, Equatable {
    case notWAV, unsupported(String), truncated, empty
}

/// Minimal WAV reader/writer: PCM 8/16/24/32 and float32, mono or stereo (more channels: first two).
/// On iOS, imports go through AVAudioFile (all formats); this is the format of stored samples and exports.
public enum WAV {
    public static func encode(_ audio: AudioData, bitsPerSample: Int = 16) -> Data {
        precondition([16, 24, 32].contains(bitsPerSample))
        let channels = 2
        let bytes = bitsPerSample / 8
        let n = audio.frames
        let dataSize = n * channels * bytes
        var d = Data()
        d.reserveCapacity(44 + dataSize)
        func u32(_ v: UInt32) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 4)) }
        func u16(_ v: UInt16) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!)
        u32(UInt32(36 + dataSize))
        d.append("WAVE".data(using: .ascii)!)
        d.append("fmt ".data(using: .ascii)!)
        u32(16)
        u16(bitsPerSample == 32 ? 3 : 1)
        u16(UInt16(channels))
        u32(UInt32(audio.sampleRate.rounded()))
        u32(UInt32(audio.sampleRate.rounded()) * UInt32(channels * bytes))
        u16(UInt16(channels * bytes))
        u16(UInt16(bitsPerSample))
        d.append("data".data(using: .ascii)!)
        u32(UInt32(dataSize))
        var body = [UInt8]()
        body.reserveCapacity(dataSize)
        for i in 0..<n {
            for ch in 0..<2 {
                let raw = ch == 0 ? audio.left[i] : audio.right[i]
                let v = raw.isFinite ? raw : 0
                switch bitsPerSample {
                case 16:
                    let s = Int16(max(-32768, min(32767, (v * 32767).rounded())))
                    let u = UInt16(bitPattern: s)
                    body.append(UInt8(u & 0xFF)); body.append(UInt8(u >> 8))
                case 24:
                    let s = Int32(max(-8388608, min(8388607, (v * 8388607).rounded())))
                    let u = UInt32(bitPattern: s)
                    body.append(UInt8(u & 0xFF)); body.append(UInt8((u >> 8) & 0xFF)); body.append(UInt8((u >> 16) & 0xFF))
                default:
                    let u = v.bitPattern
                    body.append(UInt8(u & 0xFF)); body.append(UInt8((u >> 8) & 0xFF))
                    body.append(UInt8((u >> 16) & 0xFF)); body.append(UInt8(u >> 24))
                }
            }
        }
        d.append(contentsOf: body)
        return d
    }

    public static func decode(_ data: Data) throws -> AudioData {
        let b = [UInt8](data)
        guard b.count >= 12, String(bytes: b[0..<4], encoding: .ascii) == "RIFF", String(bytes: b[8..<12], encoding: .ascii) == "WAVE" else {
            throw WAVError.notWAV
        }
        func u16(_ o: Int) -> Int { Int(b[o]) | Int(b[o + 1]) << 8 }
        func u32(_ o: Int) -> Int { Int(b[o]) | Int(b[o + 1]) << 8 | Int(b[o + 2]) << 16 | Int(b[o + 3]) << 24 }
        var pos = 12
        var format = 0, channels = 0, rate = 0, bits = 0
        var dataStart = -1, dataLen = 0
        while pos + 8 <= b.count {
            let id = String(bytes: b[pos..<pos + 4], encoding: .ascii) ?? ""
            var size = u32(pos + 4)
            let body = pos + 8
            if id == "fmt " {
                guard body + 16 <= b.count else { throw WAVError.truncated }
                format = u16(body); channels = u16(body + 2); rate = u32(body + 4); bits = u16(body + 14)
                if format == 0xFFFE, size >= 40, body + 26 <= b.count { format = u16(body + 24) }   // WAVE_FORMAT_EXTENSIBLE
            } else if id == "data" {
                dataStart = body
                if body + size > b.count { size = b.count - body }   // tolerate a truncated final chunk
                dataLen = size
                break
            }
            pos = body + size + (size & 1)
        }
        guard channels >= 1, rate > 0, dataStart >= 0 else { throw WAVError.truncated }
        let isFloat = format == 3
        guard format == 1 || isFloat else { throw WAVError.unsupported("format \(format)") }
        guard (isFloat && bits == 32) || (!isFloat && [8, 16, 24, 32].contains(bits)) else { throw WAVError.unsupported("\(bits)-bit") }
        let bytes = bits / 8
        let frameSize = bytes * channels
        let frames = dataLen / frameSize
        guard frames > 0 else { throw WAVError.empty }
        var l = [Float](repeating: 0, count: frames), r = [Float](repeating: 0, count: frames)
        func sample(_ o: Int) -> Float {
            switch bits {
            case 8: return (Float(b[o]) - 128) / 128
            case 16: return Float(Int16(bitPattern: UInt16(b[o]) | UInt16(b[o + 1]) << 8)) / 32768
            case 24:
                var v = Int32(b[o]) | Int32(b[o + 1]) << 8 | Int32(b[o + 2]) << 16
                if v & 0x800000 != 0 { v -= 0x1000000 }
                return Float(v) / 8388608
            default:
                let u = UInt32(b[o]) | UInt32(b[o + 1]) << 8 | UInt32(b[o + 2]) << 16 | UInt32(b[o + 3]) << 24
                return isFloat ? Float(bitPattern: u) : Float(Int32(bitPattern: u)) / 2147483648
            }
        }
        for i in 0..<frames {
            let o = dataStart + i * frameSize
            let a = sample(o)
            l[i] = a.isFinite ? a : 0
            let c = channels > 1 ? sample(o + bytes) : a
            r[i] = c.isFinite ? c : 0
        }
        return AudioData(left: l, right: r, sampleRate: Double(rate))
    }

    public static func read(_ url: URL) throws -> AudioData { try decode(Data(contentsOf: url)) }
    public static func write(_ audio: AudioData, to url: URL, bitsPerSample: Int = 16) throws {
        try encode(audio, bitsPerSample: bitsPerSample).write(to: url, options: .atomic)
    }
}
