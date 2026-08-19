// Minimal standalone CLI for exercising the IPC wire format against a real
// running core dylib, independent of the AppKit UI - useful for dev/debug,
// and originally written to catch any C/Swift struct-layout mismatch that
// only shows up at runtime (padding, endianness of the enum-backing int)
// rather than at compile time.
import CIPCProtocol
import Darwin
import Foundation

func connectSocket() -> Int32 {
    let fd = socket(AF_UNIX, SOCK_STREAM, 0)
    guard fd >= 0 else {
        fatalError("socket() failed")
    }
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let path = APPLESCREEN_IPC_SOCKET_PATH
    withUnsafeMutableBytes(of: &addr.sun_path) { raw in
        let buf = raw.bindMemory(to: CChar.self)
        for (i, byte) in path.utf8.enumerated() where i < buf.count - 1 {
            buf[i] = CChar(bitPattern: byte)
        }
    }
    let result = withUnsafePointer(to: &addr) { ptr -> Int32 in
        ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { saPtr in
            Darwin.connect(fd, saPtr, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
    guard result == 0 else {
        fatalError("connect() failed, errno=\(errno)")
    }
    return fd
}

let commandTypeArg = CommandLine.arguments.count > 1 ? Int32(CommandLine.arguments[1]) ?? 1 : 1

var command = applescreen_command_t()
command.magic = UInt32(APPLESCREEN_IPC_MAGIC)
command.version = UInt32(APPLESCREEN_IPC_VERSION)
command.request_id = 7
command.type = commandTypeArg
command.arg1 = 111
command.arg2 = 222

let fd = connectSocket()

withUnsafeBytes(of: &command) { raw in
    var sent = 0
    while sent < raw.count {
        let n = Darwin.send(fd, raw.baseAddress!.advanced(by: sent), raw.count - sent, 0)
        precondition(n > 0, "send failed")
        sent += n
    }
}

var responseBytes = [UInt8](repeating: 0, count: MemoryLayout<applescreen_response_t>.size)
var got = 0
while got < responseBytes.count {
    let n = responseBytes.withUnsafeMutableBytes { raw in
        Darwin.recv(fd, raw.baseAddress!.advanced(by: got), raw.count - got, 0)
    }
    precondition(n > 0, "recv failed")
    got += n
}
let response = responseBytes.withUnsafeBytes { $0.load(as: applescreen_response_t.self) }

print("request_id=\(response.request_id) status=\(response.status) value1=\(response.value1) value2=\(response.value2)")
close(fd)
