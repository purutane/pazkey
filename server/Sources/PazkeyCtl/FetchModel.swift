import Foundation
import Glibc
import PazkeyCore

// pazkey-ctl fetch-model（docs/server.md「Zenzai のモデルの取得」）。
// pazkey はモデルを配布しない。利用者の指示で、版とハッシュを固定したモデルを取得して既定の場所に置く。

/// 取得するモデル。版とハッシュは docs/server.md「Zenzai のモデルの取得」に合わせる。
/// テストのときだけ、環境変数 PAZKEY_TEST_MODEL_URL / PAZKEY_TEST_MODEL_SHA256 で差し替える。
private enum ModelSource {
    static let url = ProcessInfo.processInfo.environment["PAZKEY_TEST_MODEL_URL"]
        ?? "https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/c67e03e07d215c869f591b274c1631170d3e11fe/ggml-model-Q5_K_M.gguf"
    static let sha256 = ProcessInfo.processInfo.environment["PAZKEY_TEST_MODEL_SHA256"]
        ?? "29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673"
}

/// 標準出力に1行書く。バッファーしないので、journald でも標準エラー出力（fail）との順番が前後しない。
private func say(_ line: String) {
    FileHandle.standardOutput.write(Data((line + "\n").utf8))
}

/// コマンドを実行し、終了コードと標準出力、標準エラー出力を返す。見つからなければ nil。
private func run(_ args: [String]) -> (status: Int32, out: String, err: String)? {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/env")
    p.arguments = args
    let out = Pipe(), err = Pipe()
    p.standardOutput = out
    p.standardError = err
    do { try p.run() } catch { return nil }
    let o = out.fileHandleForReading.readDataToEndOfFile()
    let e = err.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    return (p.terminationStatus, String(decoding: o, as: UTF8.self), String(decoding: e, as: UTF8.self))
}

/// デスクトップの通知（org.freedesktop.Notifications）。送れなくても気にしない。
private func desktopNotify(_ summary: String, _ body: String) {
    _ = run(["busctl", "--user", "call", "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
             "org.freedesktop.Notifications", "Notify", "susssasa{sv}i",
             // 以降は値。タイムアウトの -1 をオプションと読ませないため -- で区切る。
             "--", "pazkey", "0", "fcitx_pazkey", summary, body, "0", "0", "-1"])
}

private func sha256(of path: String) -> String? {
    guard let r = run(["sha256sum", "--", path]), r.status == 0 else { return nil }
    return r.out.split(separator: " ").first.map(String.init)
}

/// サーバーに reload を送る。つながらなければ nil（次の起動で読み込まれる）。読み込めなければその理由。
private func reloadServer() -> String?? {
    guard tryConnect(path: Paths.socket) != nil else { return .none }
    let link = Link(path: Paths.socket)
    link.send(["type": "hello", "protocol": .number(Double(Pazkey.protocolVersion)), "client": "pazkey-ctl",
               "version": .string(Pazkey.version)])
    _ = link.receive(type: "hello")
    link.send(["type": "reload"])
    let r = link.receive(type: "reload_result")
    var messages: [String] = []
    if case let .array(list)? = r["errors"] {
        messages = list.map { $0["message"]?.stringValue ?? "" }
    }
    if r["ok"]?.boolValue != true { messages.insert("設定ファイルが不正なので、反映されていない", at: 0) }
    return .some(messages.isEmpty ? nil : messages.joined(separator: "\n"))
}

func fetchModel(notify: Bool) -> Never {
    if notify {
        beforeFail = { desktopNotify("pazkey: Zenzai のモデルを取得できない", $0) }
    }
    let dir = Paths.dataDirectory
    let target = Paths.defaultModel.path
    let part = target + ".part"
    do {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    } catch {
        fail("\(dir.path) を作れない: \(error.localizedDescription)")
    }

    // 二重に取得しない。ロックは終了まで持ち続ける（設定画面は、このロックで「ダウンロード中」を出す）。
    let lockFd = open(target + ".lock", O_CREAT | O_RDWR | O_CLOEXEC, 0o644)
    guard lockFd >= 0 else { fail("ロックのファイルを作れない: \(String(cString: strerror(errno)))") }
    guard flock(lockFd, LOCK_EX | LOCK_NB) == 0 else { fail("ほかで取得している") }

    if FileManager.default.fileExists(atPath: target), sha256(of: target) == ModelSource.sha256 {
        say("すでにある: \(target)")
    } else {
        say("取得する: \(ModelSource.url)")
        if notify { desktopNotify("pazkey: Zenzai のモデルを取得している", "約 74MB。終わったら知らせる。") }
        unlink(part)
        guard let r = run(["curl", "-fL", "--retry", "3", "--silent", "--show-error", "-o", part, ModelSource.url]) else {
            fail("curl を実行できない")
        }
        guard r.status == 0 else {
            unlink(part)
            fail("取得に失敗した: \(r.err.trimmingCharacters(in: .whitespacesAndNewlines))")
        }
        guard let hash = sha256(of: part) else {
            unlink(part)
            fail("sha256sum を実行できない")
        }
        guard hash == ModelSource.sha256 else {
            unlink(part)
            fail("取得したファイルのハッシュが合わない（\(hash)）")
        }
        guard rename(part, target) == 0 else {
            let reason = String(cString: strerror(errno))
            unlink(part)
            fail("置けない: \(reason)")
        }
        say("置いた: \(target)")
    }

    switch reloadServer() {
    case .none:
        say("サーバーにつながらない。次にサーバーが起動したときに読み込まれる")
    case .some(nil):
        say("サーバーに読み込ませた")
    case let .some(error?):
        fail("置いたが、サーバーが読み込めなかった:\n\(error)")
    }
    if notify { desktopNotify("pazkey: Zenzai のモデルを取得した", "\(target) に置いた。") }
    exit(0)
}
