import Foundation

struct JavaSigningReport {
    let hasHardenedRuntime: Bool
    let allowsDyldEnvVars: Bool
    let disablesLibraryValidation: Bool

    /// True if DYLD_INSERT_LIBRARIES would likely be silently stripped
    /// (risk #1 in docs/RISKS.md) or the injected dylib's dlopen would be
    /// blocked by library validation (risk #2), i.e. this java binary needs
    /// InjectionManager.prepareJavaForInjection before it'll work.
    var needsPreparation: Bool {
        hasHardenedRuntime && (!allowsDyldEnvVars || !disablesLibraryValidation)
    }
}

enum InjectionError: Error, CustomStringConvertible {
    case javaBinaryNotFound(String)
    case corePathNotFound(String)
    case codesignFailed(String)
    case launchFailed(String)

    var description: String {
        switch self {
        case .javaBinaryNotFound(let path): return "java binary not found at \(path)"
        case .corePathNotFound(let path): return "core dylib not found at \(path)"
        case .codesignFailed(let output): return "codesign failed: \(output)"
        case .launchFailed(let reason): return "launch failed: \(reason)"
        }
    }
}

/// Owns everything specific to getting DYLD_INSERT_LIBRARIES to actually
/// take effect against a real java binary on macOS - see docs/RISKS.md
/// risks #1-#3. Isolated here so it can be revisited independently of the
/// rest of the app as JDK vendors/launchers change their signing.
final class InjectionManager {

    /// Inspects a java binary's code signature for the two entitlements
    /// that determine whether injection will work unmodified.
    func inspectSigning(javaPath: String) -> JavaSigningReport {
        let flags = runProcess("/usr/bin/codesign", ["-d", "--verbose=4", javaPath])
        let entitlements = runProcess("/usr/bin/codesign", ["-d", "--entitlements", ":-", javaPath])
        return JavaSigningReport(
            hasHardenedRuntime: flags.contains("flags=0x10000") || flags.lowercased().contains("(runtime)"),
            allowsDyldEnvVars: entitlements.contains("com.apple.security.cs.allow-dyld-environment-variables"),
            disablesLibraryValidation: entitlements.contains("com.apple.security.cs.disable-library-validation")
        )
    }

    /// Makes a local, ad-hoc-signed copy of `originalJavaPath` in Application
    /// Support with the two entitlements DYLD_INSERT_LIBRARIES-based
    /// injection needs. This is a per-machine workaround over a *copy*, not
    /// a modification of the original launcher-provisioned binary, and has
    /// to be redone whenever the launcher updates its bundled JRE.
    func prepareJavaForInjection(originalJavaPath: String) throws -> String {
        let fm = FileManager.default
        guard fm.fileExists(atPath: originalJavaPath) else {
            throw InjectionError.javaBinaryNotFound(originalJavaPath)
        }

        let dir = try applescreenSupportDirectory()
        let preparedPath = dir.appendingPathComponent("java-injectable").path
        if fm.fileExists(atPath: preparedPath) {
            try fm.removeItem(atPath: preparedPath)
        }
        try fm.copyItem(atPath: originalJavaPath, toPath: preparedPath)

        let entitlementsPath = dir.appendingPathComponent("java-injectable.entitlements").path
        let entitlementsPlist = """
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
            <key>com.apple.security.cs.allow-dyld-environment-variables</key>
            <true/>
            <key>com.apple.security.cs.disable-library-validation</key>
            <true/>
        </dict>
        </plist>
        """
        try entitlementsPlist.write(toFile: entitlementsPath, atomically: true, encoding: .utf8)

        let output = runProcess("/usr/bin/codesign", [
            "--force", "--sign", "-", "--entitlements", entitlementsPath, preparedPath,
        ])
        guard fm.fileExists(atPath: preparedPath) else {
            throw InjectionError.codesignFailed(output)
        }

        let report = inspectSigning(javaPath: preparedPath)
        guard !report.needsPreparation else {
            throw InjectionError.codesignFailed("re-signed copy still not injectable: \(output)")
        }

        return preparedPath
    }

    /// Launches `javaPath` with DYLD_INSERT_LIBRARIES pointed at the core
    /// dylib. Caller is responsible for calling prepareJavaForInjection
    /// first if inspectSigning reported needsPreparation.
    func launch(javaPath: String, javaArguments: [String], corePath: String) throws -> Process {
        let fm = FileManager.default
        guard fm.fileExists(atPath: javaPath) else { throw InjectionError.javaBinaryNotFound(javaPath) }
        guard fm.fileExists(atPath: corePath) else { throw InjectionError.corePathNotFound(corePath) }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: javaPath)
        process.arguments = javaArguments

        var env = ProcessInfo.processInfo.environment
        env["DYLD_INSERT_LIBRARIES"] = corePath
        process.environment = env

        do {
            try process.run()
        } catch {
            throw InjectionError.launchFailed(error.localizedDescription)
        }
        return process
    }

    private func applescreenSupportDirectory() throws -> URL {
        let base = try FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let dir = base.appendingPathComponent("Applescreen", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    private func runProcess(_ path: String, _ args: [String]) -> String {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: path)
        process.arguments = args
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        do {
            try process.run()
        } catch {
            return "error running \(path): \(error)"
        }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        return String(data: data, encoding: .utf8) ?? ""
    }
}
