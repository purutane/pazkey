import Dispatch
import Foundation
import Glibc
import PazkeyCore

/// 受付（I/O）。接続、メッセージの解釈、convert のまとめ（docs/server.md「スレッド構成」）。
///
/// 状態はすべて `ioQueue` の上でだけ触る。変換キューへは非同期に積むだけで、
/// 変換キューの処理を待たない（変換キューのほうは `ioQueue.sync` で処理待ちを取り出す）。
final class Server: @unchecked Sendable {
    let ioQueue = DispatchQueue(label: "pazkey.io")
    private let engine: Engine
    private let listenFD: Int32
    private var listenSource: (any DispatchSourceRead)?
    private var connections: [Int: Connection] = [:]
    private var nextConnectionID = 1
    private let startTime = DispatchTime.now()

    /// 変換キューで処理を待つ操作。セッションごとに、届いた順に並べる。
    private enum Op {
        case convert(ConvertRequest)
        case commit(seq: Int, index: Int)
        case reset
        case close
    }
    private var pending: [SessionKey: [Op]] = [:]

    private var config: Config
    private var configGeneration = 1
    private var watcher: ConfigWatcher?
    /// Zenzai の状態と、推論に使っているデバイス。変換キューの Engine から写して持つ。
    private var zenzaiState: ZenzaiState
    private var deviceName: String
    /// デバッグログを書くか（docs/server.md「デバッグログ」）。`pazkey-ctl debug` で切り替え、設定には持たない。
    private var debugEnabled = false

    /// デバッグログを1行書く。読み、文脈、候補の文字列は渡さない。
    private func debug(_ message: @autoclosure () -> String) {
        dispatchPrecondition(condition: .onQueue(ioQueue))
        if debugEnabled { log("debug: " + message()) }
    }

    /// この起動の記録。正常終了するときに消す（docs/server.md「退避モード」）。
    private let crashRecord: CrashRecord

    init(listenFD: Int32, engine: Engine, config: Config, crashRecord: CrashRecord) {
        self.listenFD = listenFD
        self.crashRecord = crashRecord
        self.engine = engine
        self.config = config
        (self.zenzaiState, self.deviceName) = engine.queue.sync { (engine.zenzaiState, engine.deviceName) }
    }

    func start() {
        ioQueue.async { [self] in
            let s = DispatchSource.makeReadSource(fileDescriptor: listenFD, queue: ioQueue)
            s.setEventHandler { [weak self] in self?.acceptAll() }
            s.resume()
            listenSource = s
            let w = ConfigWatcher(file: Paths.configFile, queue: ioQueue) { [weak self] in
                self?.reload(replyTo: nil)
            }
            w.start()
            watcher = w
        }
    }

    // MARK: - 設定

    /// 設定を読み直す（docs/config.md「読み込みと検証」、docs/server.md「設定の反映」）。
    /// 正しければ変換キューで反映し、`config_generation` を増やして全接続へ `config_changed` を送る。
    /// 不正なら前の設定のまま動き続ける（不変条件5）。
    /// `replyTo` は `reload_result` を返す接続の ID（監視から呼ぶときは nil）。
    private func reload(replyTo: Int?) {
        dispatchPrecondition(condition: .onQueue(ioQueue))
        guard !shuttingDown else { return }
        let result = Config.load(from: Paths.configFile)
        for key in result.unknownKeys { log("設定: 知らないキー \(key) を無視する") }
        guard let newConfig = result.config else {
            for error in result.errors { log("設定の誤り: \(error)") }
            log("設定が不正なので、前の設定（config_generation \(configGeneration)）のまま動き続ける")
            // クライアント（フロントエンド）が読み直して誤りを通知できるように知らせる（docs/protocol.md「config_rejected」）。
            let rejected: JSONValue = ["type": "config_rejected", "config_generation": .number(Double(configGeneration))]
            for conn in connections.values { conn.send(rejected) }
            replyTo.flatMap { connections[$0] }?.send([
                "type": "reload_result",
                "ok": false,
                "config_generation": .number(Double(configGeneration)),
                "errors": .array(result.errors.map { ["path": .string($0.path), "message": .string($0.message)] }),
            ])
            return
        }
        engine.queue.async { [self] in
            let zenzaiError = engine.apply(config: newConfig)
            let zenzai = (engine.zenzaiState, engine.deviceName)
            ioQueue.async { [self] in
                config = newConfig
                (zenzaiState, deviceName) = zenzai
                configGeneration += 1
                log("設定を反映した（config_generation \(configGeneration)）")
                if let zenzaiError { log("Zenzai は前の状態（\(zenzaiState.rawValue)）のまま: \(zenzaiError)") }
                // zenzai は、設定画面だけを使う利用者に Zenzai の状態を知らせるため（docs/protocol.md「config_changed」）。
                let changed: JSONValue = [
                    "type": "config_changed",
                    "config_generation": .number(Double(configGeneration)),
                    "zenzai": .string(zenzaiState.rawValue),
                ]
                for conn in connections.values { conn.send(changed) }
                replyTo.flatMap { connections[$0] }?.send([
                    "type": "reload_result",
                    "ok": true,
                    "config_generation": .number(Double(configGeneration)),
                    // Zenzai のモデルを読み込めなくても、ほかの項目は反映している（docs/protocol.md「reload / reload_result」）。
                    "errors": .array(zenzaiError.map { [["path": "/zenzai", "message": .string($0)]] } ?? []),
                ])
            }
        }
    }

    /// SIGTERM、SIGINT を受けたら: 受付を止め、処理待ちの convert を捨て、学習を保存してから終わる。
    func installTerminationHandlers() {
        for sig in [SIGTERM, SIGINT] {
            signal(sig, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: sig, queue: ioQueue)
            source.setEventHandler { [weak self] in self?.shutdown(signal: sig) }
            source.resume()
            signalSources.append(source)
        }
    }

    private var signalSources: [any DispatchSourceSignal] = []
    private var shuttingDown = false

    private func shutdown(signal: Int32) {
        guard !shuttingDown else { return }
        shuttingDown = true
        log("シグナル \(signal) を受けたので終了する")
        listenSource?.cancel()
        listenSource = nil
        for key in Array(pending.keys) { dropPendingConverts(key, reason: "shutdown") }
        // 変換キューに積まれている commit などを処理し終えてから保存する。
        engine.queue.async { [engine, crashRecord] in
            let ms = engine.saveLearning()
            log("学習を保存した（終了時。\(ms)ms）")
            if let error = crashRecord.end() { log("起動の記録: \(error)") }
            exit(0)
        }
    }

    private func acceptAll() {
        while true {
            // accept4 は Swift の Glibc から見えないので、accept の後に fcntl で設定する。
            let fd = accept(listenFD, nil, nil)
            if fd >= 0 {
                _ = fcntl(fd, F_SETFD, FD_CLOEXEC)
                _ = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)
            }
            if fd < 0 {
                if errno == EINTR { continue }
                if errno != EAGAIN && errno != EWOULDBLOCK {
                    log("accept に失敗した: \(String(cString: strerror(errno)))")
                }
                return
            }
            let conn = Connection(id: nextConnectionID, fd: fd, queue: ioQueue)
            nextConnectionID += 1
            conn.onLine = { [weak self] c, line in self?.handle(line: line, from: c) }
            conn.onClose = { [weak self] c in self?.connectionClosed(c) }
            connections[conn.id] = conn
            debug("接続 \(conn.id) を受け付けた")
            conn.start()
        }
    }

    private func connectionClosed(_ conn: Connection) {
        connections[conn.id] = nil
        debug("接続 \(conn.id) が閉じた sessions=\(conn.sessions.count)")
        for session in conn.sessions {
            let key = SessionKey(connection: conn.id, session: session)
            // 応答を返す相手がいないので、処理待ちの convert は捨てる。
            dropPendingConverts(key, reason: "disconnected")
            enqueue(key, .close)
        }
        conn.sessions = []
    }

    // MARK: - メッセージ

    private func sendError(_ conn: Connection, _ code: String, _ message: String, session: Int? = nil, seq: Int? = nil) {
        var obj: [String: JSONValue] = ["type": "error", "code": .string(code), "message": .string(message)]
        if let session { obj["session"] = .number(Double(session)) }
        if let seq { obj["seq"] = .number(Double(seq)) }
        debug("接続 \(conn.id) error code=\(code) session=\(session.map(String.init) ?? "-") seq=\(seq.map(String.init) ?? "-")")
        conn.send(.object(obj))
    }

    private func handle(line: [UInt8], from conn: Connection) {
        guard let msg = JSONValue.parse(Data(line)), case .object = msg else {
            sendError(conn, "malformed", "JSON のオブジェクトとして読めない")
            return
        }
        guard let type = msg["type"]?.stringValue else {
            sendError(conn, "malformed", "type がない")
            return
        }
        let session = msg["session"]?.intValue
        let seq = msg["seq"]?.intValue
        if debugEnabled {
            // 中身（reading、left_context）は長さだけを書く。
            var fields = "接続 \(conn.id) 受信 \(type)"
            if let session { fields += " session=\(session)" }
            if let seq { fields += " seq=\(seq)" }
            if type == "convert" {
                fields += " kind=\(msg["kind"]?.stringValue ?? "?")"
                fields += " len=\(msg["reading"]?.stringValue?.unicodeScalars.count ?? -1)"
                fields += " cursor=\(msg["cursor"]?.intValue ?? -1)"
                fields += " ctx=\(msg["left_context"]?.stringValue?.unicodeScalars.count ?? 0)"
            }
            if type == "commit" { fields += " index=\(msg["index"]?.intValue ?? -1)" }
            debug(fields)
        }

        switch type {
        case "hello":
            guard msg["protocol"]?.intValue == Pazkey.protocolVersion else {
                sendError(conn, "protocol_mismatch", "プロトコルの版が違う（サーバーは \(Pazkey.protocolVersion)）")
                conn.closeWhenFlushed()
                return
            }
            conn.send([
                "type": "hello",
                "protocol": .number(Double(Pazkey.protocolVersion)),
                "server": "pazkey-server",
                "version": .string(Pazkey.version),
                "zenzai": .string(zenzaiState.rawValue),
                "config_generation": .number(Double(configGeneration)),
            ])

        case "convert":
            guard let session, let seq, seq >= 1,
                  let kindName = msg["kind"]?.stringValue, let kind = ConvertRequest.Kind(rawValue: kindName),
                  let reading = msg["reading"]?.stringValue,
                  let cursor = msg["cursor"]?.intValue else {
                sendError(conn, "malformed", "convert に必要なフィールドがない、または型が違う", session: session, seq: seq)
                return
            }
            let leftContext: String
            switch msg["left_context"] {
            case nil: leftContext = ""
            case let .string(s)?: leftContext = s
            default:
                sendError(conn, "malformed", "left_context が文字列でない", session: session, seq: seq)
                return
            }
            let check = ReadingCheck.check(reading)
            guard check == .ok else {
                sendError(conn, "invalid_reading", check.message, session: session, seq: seq)
                return
            }
            guard (0...reading.unicodeScalars.count).contains(cursor) else {
                sendError(conn, "invalid_cursor", "cursor が範囲外", session: session, seq: seq)
                return
            }
            let key = SessionKey(connection: conn.id, session: session)
            conn.sessions.insert(session)
            enqueue(key, .convert(.init(key: key, seq: seq, kind: kind, reading: reading, cursor: cursor, leftContext: leftContext)))

        case "commit":
            guard let session, let seq, let index = msg["index"]?.intValue else {
                sendError(conn, "malformed", "commit に必要なフィールドがない、または型が違う", session: session, seq: seq)
                return
            }
            guard conn.sessions.contains(session) else { return }  // 知らないセッション。黙って無視する
            enqueue(SessionKey(connection: conn.id, session: session), .commit(seq: seq, index: index))

        case "reset":
            guard let session else {
                sendError(conn, "malformed", "reset に session がない")
                return
            }
            guard conn.sessions.contains(session) else { return }
            enqueue(SessionKey(connection: conn.id, session: session), .reset)

        case "close":
            guard let session else {
                sendError(conn, "malformed", "close に session がない")
                return
            }
            guard conn.sessions.remove(session) != nil else { return }
            enqueue(SessionKey(connection: conn.id, session: session), .close)

        case "reload":
            reload(replyTo: conn.id)

        case "status":
            let uptime = (DispatchTime.now().uptimeNanoseconds - startTime.uptimeNanoseconds) / 1_000_000_000
            conn.send([
                "type": "status_result",
                "version": .string(Pazkey.version),
                "zenzai": .string(zenzaiState.rawValue),
                "device": .string(deviceName),
                "config_generation": .number(Double(configGeneration)),
                "sessions": .number(Double(connections.values.reduce(0) { $0 + $1.sessions.count })),
                "uptime_s": .number(Double(uptime)),
                "debug": .bool(debugEnabled),
            ])

        case "debug":
            guard let enabled = msg["enabled"]?.boolValue else {
                sendError(conn, "malformed", "debug に enabled（真偽値）がない")
                return
            }
            if enabled != debugEnabled { log(enabled ? "デバッグログを書き始める" : "デバッグログをやめる") }
            debugEnabled = enabled
            conn.send(["type": "debug_result", "enabled": .bool(enabled)])

        case "devices":
            // 今は GPU に対応しないので、CPU だけを返す（docs/protocol.md「devices / devices_result」）。
            conn.send([
                "type": "devices_result",
                "devices": [["index": -1, "name": "CPU", "description": "CPU"]],
            ])

        default:
            sendError(conn, "unknown_type", "知らない type")
        }
    }

    // MARK: - 変換キューへの受け渡し

    /// 操作を積む。処理待ちの最後が convert で、新しいものも convert なら上書きする（古いほうには応答しない）。
    private func enqueue(_ key: SessionKey, _ op: Op) {
        dispatchPrecondition(condition: .onQueue(ioQueue))
        if shuttingDown, case .convert = op { return }
        var ops = pending[key, default: []]
        if case let .convert(new) = op, let last = ops.last, case let .convert(old) = last {
            debug("接続 \(key.connection) session=\(key.session) convert seq=\(old.seq) を捨てた（seq=\(new.seq) で上書き）")
            ops[ops.count - 1] = op
            pending[key] = ops
            return
        }
        ops.append(op)
        pending[key] = ops
        // 積んだ操作1つにつき、変換キューの仕事を1つ積む。仕事は、そのセッションの先頭を1つ取り出して処理する。
        engine.queue.async { [self] in runNext(key) }
    }

    private func dropPendingConverts(_ key: SessionKey, reason: String) {
        guard let ops = pending[key] else { return }
        for case let .convert(request) in ops {
            debug("接続 \(key.connection) session=\(key.session) convert seq=\(request.seq) を捨てた（\(reason)）")
        }
        pending[key] = ops.filter { if case .convert = $0 { false } else { true } }
    }

    /// 変換キューの上で呼ばれる。
    private func runNext(_ key: SessionKey) {
        dispatchPrecondition(condition: .onQueue(engine.queue))
        let op: Op? = ioQueue.sync {
            guard var ops = pending[key], !ops.isEmpty else { return nil }
            let first = ops.removeFirst()
            pending[key] = ops.isEmpty ? nil : ops
            return first
        }
        guard let op else { return }
        do {
            switch op {
            case let .convert(request):
                let response = try engine.convert(request)
                ioQueue.async { [self] in sendResult(request, response) }
            case let .commit(seq, index):
                let learned = try engine.commit(key: key, seq: seq, index: index)
                ioQueue.async { [self] in
                    debug("接続 \(key.connection) session=\(key.session) commit seq=\(seq) index=\(index) \(learned ? "学習した" : "無視した")")
                }
            case .reset:
                try engine.reset(key: key)
            case .close:
                engine.close(key: key)
            }
        } catch {
            log("変換キューでエラー: \(error)")
            if case let .convert(request) = op {
                ioQueue.async { [self] in
                    guard let conn = connections[key.connection] else { return }
                    sendError(conn, "internal", "変換に失敗した", session: key.session, seq: request.seq)
                }
            }
        }
    }

    private func sendResult(_ request: ConvertRequest, _ response: ConvertResponse) {
        guard let conn = connections[request.key.connection] else { return }
        debug("接続 \(conn.id) 送信 result session=\(request.key.session) seq=\(request.seq) candidates=\(response.candidates.count) elapsed=\(response.elapsedMs)ms")
        conn.send([
            "type": "result",
            "session": .number(Double(request.key.session)),
            "seq": .number(Double(request.seq)),
            "candidates": .array(response.candidates.map {
                ["text": .string($0.text), "consumed": .number(Double($0.consumed))]
            }),
            "elapsed_ms": .number(Double(response.elapsedMs)),
        ])
    }
}
