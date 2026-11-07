import Dispatch
import Foundation
import Glibc
import PazkeyCore

/// 1本の接続。行（JSON + 改行）を読み書きする。受付キューの上でだけ使う。
final class Connection {
    /// 1行の上限（docs/protocol.md「伝送」）。
    static let maxLineBytes = 1 << 20
    /// 相手が読まずに溜まった送信待ちの上限。超えたら切る。
    static let maxPendingOutput = 8 << 20

    let id: Int
    private let fd: Int32
    private let queue: DispatchQueue
    private var readSource: (any DispatchSourceRead)?
    private var writeSource: (any DispatchSourceWrite)?
    private var writeSourceActive = false
    private var input: [UInt8] = []
    private var output: [UInt8] = []
    private var closeAfterFlush = false
    private(set) var isClosed = false

    /// この接続で見たセッション ID。切断のときに破棄する。
    var sessions: Set<Int> = []

    var onLine: ((Connection, [UInt8]) -> Void)?
    var onClose: ((Connection) -> Void)?

    init(id: Int, fd: Int32, queue: DispatchQueue) {
        self.id = id
        self.fd = fd
        self.queue = queue
    }

    func start() {
        let rs = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        rs.setEventHandler { [weak self] in self?.readAvailable() }
        let ws = DispatchSource.makeWriteSource(fileDescriptor: fd, queue: queue)
        ws.setEventHandler { [weak self] in self?.flush() }
        // fd を閉じるのは、両方の source の cancel が終わってから（どちらも同じ直列キューで呼ばれる）。
        let closer = FDCloser(fd: fd, users: 2)
        rs.setCancelHandler { closer.release() }
        ws.setCancelHandler { closer.release() }
        readSource = rs
        writeSource = ws
        rs.resume()
    }

    private func readAvailable() {
        var chunk = [UInt8](repeating: 0, count: 65536)
        while !isClosed {
            let n = chunk.withUnsafeMutableBytes { Glibc.read(fd, $0.baseAddress, $0.count) }
            if n > 0 {
                input.append(contentsOf: chunk[0..<n])
                processLines()
            } else if n == 0 {
                close()
                return
            } else {
                if errno == EINTR { continue }
                if errno == EAGAIN || errno == EWOULDBLOCK { return }
                close()
                return
            }
        }
    }

    private func processLines() {
        var start = 0
        while !isClosed, let nl = input[start...].firstIndex(of: 0x0A) {
            if nl - start > Self.maxLineBytes {
                log("接続 \(id): 1行が上限を超えたので切る")
                close()
                return
            }
            let line = Array(input[start..<nl])
            start = nl + 1
            if !line.isEmpty { onLine?(self, line) }
        }
        input.removeFirst(min(start, input.count))
        if input.count > Self.maxLineBytes {
            log("接続 \(id): 1行が上限を超えたので切る")
            close()
        }
    }

    func send(_ message: JSONValue) {
        guard !isClosed else { return }
        output.append(contentsOf: Array(message.serialized().utf8))
        output.append(0x0A)
        if output.count > Self.maxPendingOutput {
            log("接続 \(id): 送信待ちが上限を超えたので切る")
            close()
            return
        }
        flush()
    }

    /// 送信待ちを書き終えたら閉じる。
    func closeWhenFlushed() {
        closeAfterFlush = true
        if output.isEmpty { close() }
    }

    private func flush() {
        while !isClosed && !output.isEmpty {
            let n = output.withUnsafeBytes { Glibc.send(fd, $0.baseAddress, $0.count, Int32(MSG_NOSIGNAL)) }
            if n > 0 {
                output.removeFirst(n)
            } else if n < 0 && errno == EINTR {
                continue
            } else if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) {
                if !writeSourceActive {
                    writeSourceActive = true
                    writeSource?.resume()
                }
                return
            } else {
                close()
                return
            }
        }
        if writeSourceActive {
            writeSourceActive = false
            writeSource?.suspend()
        }
        if closeAfterFlush { close() }
    }

    func close() {
        guard !isClosed else { return }
        isClosed = true
        // 停止中の source は cancel する前に resume しておく必要がある。
        if let ws = writeSource {
            if !writeSourceActive { ws.resume() }
            ws.cancel()
        }
        readSource?.cancel()  // cancel handler が最後に fd を閉じる
        writeSource = nil
        readSource = nil
        onClose?(self)
    }
}

private final class FDCloser {
    private let fd: Int32
    private var users: Int
    init(fd: Int32, users: Int) {
        self.fd = fd
        self.users = users
    }
    func release() {
        users -= 1
        if users == 0 { Glibc.close(fd) }
    }
}
