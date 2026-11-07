import Foundation

/// 退避モードの判定に使う、起動の記録（docs/server.md「退避モード」）。
///
/// 起動のたびに起動時刻を足し、正常終了したら自分の起動の分を消す。
/// 残っている記録は「正常終了しなかった起動」である。Zenzai が原因かどうかは区別しない。
public struct CrashRecord: Sendable {
    /// ファイルの中身。起動時刻（UNIX 時刻のミリ秒）の一覧。
    private struct Contents: Codable {
        var starts: [Int64]
    }

    public let file: URL
    /// この起動の起動時刻。正常終了のときに、これだけを消す。
    public let startedAt: Int64
    /// この起動より前の、時間幅の中にある「正常終了しなかった起動」の回数。
    public let recentCrashes: Int

    /// 起動を記録し、直近 `windowSeconds` 秒の「正常終了しなかった起動」を数える。
    /// 時間幅の外の記録はここで捨てる。ファイルが壊れていたら空として扱う（`problem` に理由を返す）。
    public static func begin(
        file: URL, windowSeconds: Int, now: Date = Date()
    ) -> (record: CrashRecord, problem: String?) {
        let nowMs = Int64((now.timeIntervalSince1970 * 1000).rounded(.down))
        let (starts, problem) = read(file)
        // 時計が戻った場合に備え、未来の記録も捨てる（数えたままにすると、いつまでも消えない）。
        let recent = starts.filter { $0 > nowMs - Int64(windowSeconds) * 1000 && $0 <= nowMs }
        let record = CrashRecord(file: file, startedAt: nowMs, recentCrashes: recent.count)
        var problems = problem.map { [$0] } ?? []
        if let error = write(recent + [nowMs], to: file) { problems.append(error) }
        return (record, problems.isEmpty ? nil : problems.joined(separator: "。"))
    }

    /// 正常終了したときに呼ぶ。この起動の記録だけを消す（ほかの起動の記録は残す）。
    @discardableResult
    public func end() -> String? {
        let (starts, problem) = Self.read(file)
        if let problem { return problem }
        var rest = starts
        if let i = rest.firstIndex(of: startedAt) { rest.remove(at: i) }
        if rest.isEmpty {
            do {
                try FileManager.default.removeItem(at: file)
            } catch CocoaError.fileNoSuchFile {
            } catch {
                return "\(file.path) を消せない: \(error.localizedDescription)"
            }
            return nil
        }
        return Self.write(rest, to: file)
    }

    private static func read(_ file: URL) -> (starts: [Int64], problem: String?) {
        let data: Data
        do {
            data = try Data(contentsOf: file)
        } catch CocoaError.fileReadNoSuchFile {
            return ([], nil)
        } catch {
            return ([], "\(file.path) を読めない: \(error.localizedDescription)")
        }
        guard let contents = try? JSONDecoder().decode(Contents.self, from: data) else {
            return ([], "\(file.path) が壊れているので、記録がないものとして扱う")
        }
        return (contents.starts, nil)
    }

    private static func write(_ starts: [Int64], to file: URL) -> String? {
        do {
            try FileManager.default.createDirectory(
                at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try JSONEncoder().encode(Contents(starts: starts)).write(to: file, options: .atomic)
            return nil
        } catch {
            return "\(file.path) に書けない: \(error.localizedDescription)"
        }
    }
}
