// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Applescreen",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "CIPCProtocol"),
        .executableTarget(
            name: "Applescreen",
            dependencies: ["CIPCProtocol"]
        ),
        .executableTarget(
            name: "ipc-smoke-test",
            dependencies: ["CIPCProtocol"]
        ),
    ]
)
