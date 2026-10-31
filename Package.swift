// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "DSS1",
    platforms: [.macOS(.v14)],
    dependencies: [
        .package(url: "https://github.com/AudioKit/AudioKit.git", exact: "5.7.2"),
        .package(url: "https://github.com/AudioKit/SoundpipeAudioKit.git", exact: "5.7.4"),
    ],
    targets: [
        // Rubber Band 4.0 (GPL v2+, breakfastquay.com), built from its single-file source.
        .target(
            name: "CRubberBand",
            path: "Vendor/RubberBand",
            sources: ["single/RubberBandSingle.cpp", "shim/rb_live.cpp"],
            publicHeadersPath: "shim/include",
            cxxSettings: [.unsafeFlags(["-w"])],
            linkerSettings: [.linkedFramework("Accelerate")]
        ),
        .executableTarget(
            name: "DSS1",
            dependencies: [
                "CRubberBand",
                .product(name: "AudioKit", package: "AudioKit"),
                .product(name: "SoundpipeAudioKit", package: "SoundpipeAudioKit"),
            ],
            path: "Sources/DSS1"
        )
    ],
    cxxLanguageStandard: .cxx14
)
