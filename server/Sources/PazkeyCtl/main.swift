import Foundation
import Glibc
import PazkeyCore

// pazkey-ctl: サーバーへ reload / status / devices / debug を送る（docs/server.md「pazkey-ctl」）。

/// 失敗で終わる前に呼ぶもの（fetch-model --notify が通知に使う）。
nonisolated(unsafe) var beforeFail: ((String) -> Void)?

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    beforeFail?(message)
    exit(1)
}

let usage = """
使い方: pazkey-ctl <コマンド>

  reload    設定ファイルを読み直す。不正なら誤りを表示し、終了コード 1
  status    サーバーの状態を表示する
  devices   推論に使えるデバイスを表示する
  debug on|off
            デバッグログ（操作と状態だけで、入力した文章は書かない）を書き始める / やめる。
            サーバーを起動し直すと切れる
  fetch-model [--notify]
            Zenzai のモデルを取得して既定の場所に置き、サーバーに読み込ませる。
            --notify を付けると、始めと終わりをデスクトップの通知で知らせる
"""

let args = Array(CommandLine.arguments.dropFirst())
let valid = (args.count == 1 && ["reload", "status", "devices", "fetch-model"].contains(args[0]))
    || (args.count == 2 && args[0] == "debug" && ["on", "off"].contains(args[1]))
    || (args.count == 2 && args[0] == "fetch-model" && args[1] == "--notify")
guard valid else {
    fail(usage)
}
let command = args[0]
if command == "fetch-model" {
    fetchModel(notify: args.count == 2)
}

/// サーバーに接続する。応答を待つのは最大5秒。
func connect(path: String) -> Int32 {
    guard let fd = tryConnect(path: path) else {
        fail("サーバーに接続できない（\(path): \(String(cString: strerror(errno)))）。systemctl --user status pazkey-server.socket を確かめる")
    }
    return fd
}

/// 接続できなければ nil（errno は残る）。
func tryConnect(path: String) -> Int32? {
    let fd = socket(AF_UNIX, Int32(SOCK_STREAM.rawValue | SOCK_CLOEXEC.rawValue), 0)
    guard fd >= 0 else { fail("socket に失敗した") }
    var timeout = timeval(tv_sec: 5, tv_usec: 0)
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)
    guard bytes.count < MemoryLayout.size(ofValue: addr.sun_path) else { fail("ソケットのパスが長すぎる: \(path)") }
    withUnsafeMutableBytes(of: &addr.sun_path) { buf in
        buf.copyBytes(from: bytes)
        buf[bytes.count] = 0
    }
    let rc = withUnsafePointer(to: &addr) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
            Glibc.connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
    guard rc == 0 else {
        let saved = errno
        close(fd)
        errno = saved
        return nil
    }
    return fd
}

/// サーバーとの1本の接続。
final class Link {
    private let fd: Int32
    private var inbox: [UInt8] = []

    init(path: String) {
        fd = connect(path: path)
    }

    func send(_ message: JSONValue) {
        let data = Array((message.serialized() + "\n").utf8)
        var offset = 0
        while offset < data.count {
            let n = data[offset...].withUnsafeBytes { Glibc.send(fd, $0.baseAddress, $0.count, Int32(MSG_NOSIGNAL)) }
            guard n > 0 else { fail("サーバーへの送信に失敗した") }
            offset += n
        }
    }

    /// `type` が一致するメッセージが来るまで読む（間に来る config_changed などは読み捨てる）。
    func receive(type: String) -> JSONValue {
        var chunk = [UInt8](repeating: 0, count: 65536)
        while true {
            while let nl = inbox.firstIndex(of: 0x0A) {
                let line = Array(inbox[..<nl])
                inbox.removeFirst(nl + 1)
                guard let msg = JSONValue.parse(Data(line)) else { continue }
                if msg["type"]?.stringValue == type { return msg }
                if msg["type"]?.stringValue == "error" {
                    fail("サーバーがエラーを返した: \(msg["code"]?.stringValue ?? "?") \(msg["message"]?.stringValue ?? "")")
                }
            }
            let n = chunk.withUnsafeMutableBytes { Glibc.recv(fd, $0.baseAddress, $0.count, 0) }
            if n > 0 {
                inbox.append(contentsOf: chunk[0..<n])
            } else if n < 0 && errno == EINTR {
                continue
            } else {
                fail(n == 0 ? "サーバーが接続を閉じた" : "サーバーからの応答がない（\(String(cString: strerror(errno)))）")
            }
        }
    }
}

func number(_ v: JSONValue?) -> String {
    v?.intValue.map(String.init) ?? "?"
}

let link = Link(path: Paths.socket)
link.send(["type": "hello", "protocol": .number(Double(Pazkey.protocolVersion)), "client": "pazkey-ctl",
      "version": .string(Pazkey.version)])
_ = link.receive(type: "hello")

switch command {
case "reload":
    link.send(["type": "reload"])
    let r = link.receive(type: "reload_result")
    var errors: [JSONValue] = []
    if case let .array(list)? = r["errors"] { errors = list }
    let printErrors = {
        for e in errors {
            let path = e["path"]?.stringValue ?? ""
            print("  \(path.isEmpty ? "/" : path): \(e["message"]?.stringValue ?? "")")
        }
    }
    if r["ok"]?.boolValue == true {
        print("設定を読み直した（config_generation \(number(r["config_generation"]))）")
        if errors.isEmpty { exit(0) }
        // Zenzai のモデルを読み込めなかった。ほかの項目は反映している（docs/protocol.md「reload / reload_result」）。
        print("ただし、Zenzai は前の状態のまま動いている:")
        printErrors()
        exit(1)
    }
    print("設定が不正なので、前の設定（config_generation \(number(r["config_generation"]))）のまま動いている:")
    printErrors()
    exit(1)

case "status":
    link.send(["type": "status"])
    let r = link.receive(type: "status_result")
    print("version: \(r["version"]?.stringValue ?? "?")")
    print("zenzai: \(r["zenzai"]?.stringValue ?? "?")")
    print("device: \(r["device"]?.stringValue ?? "?")")
    print("config_generation: \(number(r["config_generation"]))")
    print("sessions: \(number(r["sessions"]))")
    print("uptime_s: \(number(r["uptime_s"]))")
    print("debug: \(r["debug"]?.boolValue == true ? "on" : "off")")

case "debug":
    link.send(["type": "debug", "enabled": .bool(args[1] == "on")])
    let r = link.receive(type: "debug_result")
    print(r["enabled"]?.boolValue == true ? "デバッグログを書き始めた（journalctl --user -u pazkey-server で見る）" : "デバッグログをやめた")

case "devices":
    link.send(["type": "devices"])
    let r = link.receive(type: "devices_result")
    var names = ["auto", "cpu"]
    if case let .array(devices)? = r["devices"] {
        for d in devices {
            let name = d["name"]?.stringValue ?? "?"
            print("\(number(d["index"]))\t\(name)\t\(d["description"]?.stringValue ?? "")")
            if name.lowercased() != "cpu" { names.append(name) }
        }
    }
    print("")
    print("設定の zenzai.device に書ける値: \(names.joined(separator: ", "))")

default:
    fail(usage)
}
