import Foundation

/// 標準エラー出力に1行書く（journald が拾う）。
/// 読みや文脈、候補の中身は書かない（docs/server.md「ログ」）。
public func log(_ message: String) {
    FileHandle.standardError.write(Data((message + "\n").utf8))
}
