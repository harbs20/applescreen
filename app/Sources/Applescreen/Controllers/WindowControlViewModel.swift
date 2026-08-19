import CIPCProtocol
import Foundation

/// Glue between the AppKit UI and IPCClient/InjectionManager. All network
/// and process calls happen off the main thread; results come back to the
/// UI through onLog/onConnectionChange on the main queue.
final class WindowControlViewModel {
    private let ipc = IPCClient()
    let injection = InjectionManager()

    var onLog: (String) -> Void = { _ in }
    var onConnectionChange: (Bool) -> Void = { _ in }

    private func log(_ message: String) {
        DispatchQueue.main.async { self.onLog(message) }
    }

    func connect() {
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                try self.ipc.connect()
                self.log("connected to \(APPLESCREEN_IPC_SOCKET_PATH)")
                DispatchQueue.main.async { self.onConnectionChange(true) }
            } catch {
                self.log("connect failed: \(error)")
                DispatchQueue.main.async { self.onConnectionChange(false) }
            }
        }
    }

    private func sendCommand(_ type: applescreen_cmd_type_t, arg1: Int32 = 0, arg2: Int32 = 0, describe: String) {
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let response = try self.ipc.send(type: type, arg1: arg1, arg2: arg2)
                self.log("\(describe) -> status=\(response.status) value1=\(response.value1) value2=\(response.value2)")
            } catch {
                self.log("\(describe) failed: \(error)")
            }
        }
    }

    func ping() {
        sendCommand(APPLESCREEN_CMD_PING, describe: "PING")
    }

    func setPosition(x: Int32, y: Int32) {
        sendCommand(APPLESCREEN_CMD_SET_WINDOW_POS, arg1: x, arg2: y, describe: "SET_WINDOW_POS(\(x), \(y))")
    }

    func setSize(w: Int32, h: Int32) {
        sendCommand(APPLESCREEN_CMD_SET_WINDOW_SIZE, arg1: w, arg2: h, describe: "SET_WINDOW_SIZE(\(w), \(h))")
    }

    func focusWindow() {
        sendCommand(APPLESCREEN_CMD_FOCUS_WINDOW, describe: "FOCUS_WINDOW")
    }

    func getPosition() {
        sendCommand(APPLESCREEN_CMD_GET_WINDOW_POS, describe: "GET_WINDOW_POS")
    }

    func getSize() {
        sendCommand(APPLESCREEN_CMD_GET_WINDOW_SIZE, describe: "GET_WINDOW_SIZE")
    }

    func launchMinecraft(javaPath: String, corePath: String, javaArgs: [String]) {
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                var effectiveJavaPath = javaPath
                let report = self.injection.inspectSigning(javaPath: javaPath)
                if report.needsPreparation {
                    self.log("java binary is hardened-runtime-signed without the entitlements " +
                             "DYLD_INSERT_LIBRARIES injection needs - preparing a local injectable copy " +
                             "(see docs/RISKS.md #1/#2)")
                    effectiveJavaPath = try self.injection.prepareJavaForInjection(originalJavaPath: javaPath)
                    self.log("prepared injectable java copy: \(effectiveJavaPath)")
                }
                let process = try self.injection.launch(
                    javaPath: effectiveJavaPath, javaArguments: javaArgs, corePath: corePath)
                self.log("launched java pid=\(process.processIdentifier) with DYLD_INSERT_LIBRARIES=\(corePath)")
            } catch {
                self.log("launch failed: \(error)")
            }
        }
    }

    func inspectSigning(javaPath: String) {
        DispatchQueue.global(qos: .userInitiated).async {
            let report = self.injection.inspectSigning(javaPath: javaPath)
            self.log("""
                signing report for \(javaPath):
                  hardened runtime: \(report.hasHardenedRuntime)
                  allow-dyld-environment-variables: \(report.allowsDyldEnvVars)
                  disable-library-validation: \(report.disablesLibraryValidation)
                  needs preparation before injection will work: \(report.needsPreparation)
                """)
        }
    }
}
