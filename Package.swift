// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Seqwenser",
    platforms: [.iOS(.v16), .macOS(.v13)],
    products: [
        .library(name: "SeqwenserKit", targets: ["SeqwenserKit"]),
    ],
    targets: [
        // The portable C++17 sampler/DSP engine (also built by CMake for the Linux tests and the Logic plug-in).
        .target(
            name: "CSeqw",
            path: "core",
            exclude: ["tests", "include", "CMakeLists.txt"],
            sources: ["src"],
            publicHeadersPath: "capi",
            cxxSettings: [.headerSearchPath("include")]
        ),
        // Swift model, project files, WAV I/O and the engine wrapper. No UI frameworks: builds and tests on Linux too.
        .target(name: "SeqwenserKit", dependencies: ["CSeqw"], path: "Sources/SeqwenserKit"),
        .testTarget(name: "SeqwenserKitTests", dependencies: ["SeqwenserKit", "CSeqw"], path: "Tests/SeqwenserKitTests"),
    ],
    cxxLanguageStandard: .cxx17
)
