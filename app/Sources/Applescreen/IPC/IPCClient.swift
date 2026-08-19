import CIPCProtocol
import Darwin
import Foundation

enum IPCError: Error, CustomStringConvertible {
    case socketCreationFailed
    case connectFailed(Int32)
    case notConnected
    case sendFailed
    case recvFailed

    var description: String {
        switch self {
        case .socketCreationFailed: return "failed to create socket"
        case .connectFailed(let errno): return "connect() failed (errno=\(errno))"
        case .notConnected: return "not connected"
        case .sendFailed: return "send() failed"
        case .recvFailed: return "recv() failed or connection closed"
        }
    }
}

/// Talks to the injected core's AF_UNIX/SOCK_STREAM server at
/// APPLESCREEN_IPC_SOCKET_PATH, using the exact wire structs from
/// core/include/applescreen/ipc_protocol.h via the CIPCProtocol shim - no
/// hand-mirrored Swift structs to drift out of sync.
final class IPCClient {
    private var fd: Int32 = -1
    private var nextRequestID: UInt32 = 1

    var isConnected: Bool { fd >= 0 }

    func connect() throws {
        disconnect()

        let sockFD = socket(AF_UNIX, SOCK_STREAM, 0)
        guard sockFD >= 0 else { throw IPCError.socketCreationFailed }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let path = APPLESCREEN_IPC_SOCKET_PATH
        withUnsafeMutableBytes(of: &addr.sun_path) { rawPtr in
            let buffer = rawPtr.bindMemory(to: CChar.self)
            for (i, byte) in path.utf8.enumerated() where i < buffer.count - 1 {
                buffer[i] = CChar(bitPattern: byte)
            }
        }

        let result = withUnsafePointer(to: &addr) { ptr -> Int32 in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { saPtr in
                Darwin.connect(sockFD, saPtr, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard result == 0 else {
            let err = errno
            close(sockFD)
            throw IPCError.connectFailed(err)
        }
        fd = sockFD
    }

    func disconnect() {
        if fd >= 0 {
            close(fd)
            fd = -1
        }
    }

    @discardableResult
    func send(type: applescreen_cmd_type_t, arg1: Int32 = 0, arg2: Int32 = 0) throws -> applescreen_response_t {
        guard fd >= 0 else { throw IPCError.notConnected }

        let requestID = nextRequestID
        nextRequestID += 1

        var command = applescreen_command_t()
        command.magic = UInt32(APPLESCREEN_IPC_MAGIC)
        command.version = UInt32(APPLESCREEN_IPC_VERSION)
        command.request_id = requestID
        command.type = Int32(type.rawValue)
        command.arg1 = arg1
        command.arg2 = arg2

        try sendFull(command)
        return try recvFull()
    }

    private func sendFull<T>(_ value: T) throws {
        var value = value
        try withUnsafeBytes(of: &value) { raw in
            var sent = 0
            while sent < raw.count {
                let n = Darwin.send(fd, raw.baseAddress!.advanced(by: sent), raw.count - sent, 0)
                if n <= 0 { throw IPCError.sendFailed }
                sent += n
            }
        }
    }

    private func recvFull<T>() throws -> T {
        let size = MemoryLayout<T>.size
        var buffer = [UInt8](repeating: 0, count: size)
        var got = 0
        while got < size {
            let n = try buffer.withUnsafeMutableBytes { raw -> Int in
                guard let base = raw.baseAddress else { throw IPCError.recvFailed }
                return Darwin.recv(fd, base.advanced(by: got), size - got, 0)
            }
            if n <= 0 { throw IPCError.recvFailed }
            got += n
        }
        return buffer.withUnsafeBytes { $0.load(as: T.self) }
    }
}
