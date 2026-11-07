import Dispatch
import Foundation
import Glibc
import PazkeyCore

let arguments = Array(CommandLine.arguments.dropFirst())

func fail(_ message: String) -> Never {
    log(message)
    exit(1)
}

// --check-config <file>: 設定ファイルを検証するだけで終わる（tests/config/ で使う）。
if arguments.first == "--check-config" {
    guard arguments.count == 2 else {
        log("使い方: pazkey-server --check-config <file>")
        exit(2)
    }
    let result = Config.load(from: URL(fileURLWithPath: arguments[1]))
    for key in result.unknownKeys { print("warning: 知らないキー \(key)") }
    for error in result.errors { print("error: \(error)") }
    print(result.config == nil ? "invalid" : "valid")
    exit(result.config == nil ? 1 : 0)
}

var listenPath: String?
do {
    var i = 0
    while i < arguments.count {
        switch arguments[i] {
        case "--listen" where i + 1 < arguments.count:
            listenPath = arguments[i + 1]
            i += 2
        default:
            fail("使い方: pazkey-server [--listen <path>] | --check-config <file>")
        }
    }
}

signal(SIGPIPE, SIG_IGN)

// 1. 設定を読む。不正なら既定値で起動する。
let loaded = Config.load(from: Paths.configFile)
for key in loaded.unknownKeys { log("設定: 知らないキー \(key) を無視する") }
for error in loaded.errors { log("設定の誤り: \(error)") }
let initialConfig = loaded.config ?? Config()
if loaded.config == nil { log("設定が不正なので既定値で起動する") }

// 2. 待ち受けるソケット。systemd のソケット起動なら fd 3、そうでなければ --listen で作る。
func socketFromSystemd() -> Int32? {
    let env = ProcessInfo.processInfo.environment
    guard let pid = env["LISTEN_PID"].flatMap(Int32.init), pid == getpid(),
          let fds = env["LISTEN_FDS"].flatMap(Int.init), fds >= 1 else { return nil }
    unsetenv("LISTEN_PID")
    unsetenv("LISTEN_FDS")
    unsetenv("LISTEN_FDNAMES")
    return 3
}

func makeListeningSocket(path: String) -> Int32 {
    let dir = (path as NSString).deletingLastPathComponent
    if !dir.isEmpty {
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
    }
    unlink(path)
    let fd = socket(AF_UNIX, Int32(SOCK_STREAM.rawValue | SOCK_CLOEXEC.rawValue), 0)
    guard fd >= 0 else { fail("socket に失敗した") }
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)
    guard bytes.count < MemoryLayout.size(ofValue: addr.sun_path) else { fail("ソケットのパスが長すぎる") }
    withUnsafeMutableBytes(of: &addr.sun_path) { buf in
        buf.copyBytes(from: bytes)
        buf[bytes.count] = 0
    }
    let old = umask(0o077)
    let rc = withUnsafePointer(to: &addr) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
            bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
    umask(old)
    guard rc == 0 else { fail("bind に失敗した: \(String(cString: strerror(errno)))") }
    guard listen(fd, 64) == 0 else { fail("listen に失敗した") }
    return fd
}

let listenFD: Int32
if let fd = socketFromSystemd() {
    listenFD = fd
    log("systemd から受け取ったソケットで待ち受ける")
} else if let listenPath {
    listenFD = makeListeningSocket(path: listenPath)
    log("\(listenPath) で待ち受ける")
} else {
    fail("待ち受けるソケットがない（systemd のソケット起動で動かすか、--listen <path> を付ける）")
}
_ = fcntl(listenFD, F_SETFL, fcntl(listenFD, F_GETFL) | O_NONBLOCK)

// 3. 変換器を作る（変換キューの上で）。
// 4. 退避モードの判定をしてから、Zenzai のモデルを読み込む（Engine の初期化の中で。読めなければ Zenzai なしで動く）。
let (crashRecord, crashRecordProblem) = CrashRecord.begin(
    file: Paths.crashRecord, windowSeconds: initialConfig.fallback.windowSeconds)
if let crashRecordProblem { log("起動の記録: \(crashRecordProblem)") }
let fallback = crashRecord.recentCrashes >= initialConfig.fallback.maxCrashes
if fallback {
    log("直近 \(initialConfig.fallback.windowSeconds) 秒に正常終了しなかった起動が \(crashRecord.recentCrashes) 回あるので、退避モードで起動する")
}
quietLlamaLog()
let conversionQueue = DispatchQueue(label: "pazkey.conversion")
let engine = conversionQueue.sync { Engine(queue: conversionQueue, config: initialConfig, fallback: fallback) }

// 5. 受付を始める。設定ディレクトリの監視も始める。
let server = Server(listenFD: listenFD, engine: engine, config: initialConfig, crashRecord: crashRecord)
server.installTerminationHandlers()
server.start()
log("pazkey-server \(Pazkey.version) を起動した")
dispatchMain()
