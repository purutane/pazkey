import Dispatch
import Foundation
import Glibc
import PazkeyCore

/// 設定ディレクトリを inotify で監視し、`config.json` が変わったら知らせる（docs/config.md「反映の経路」）。
///
/// ファイルではなくディレクトリを監視する。多くの編集器は一時ファイルに書いてから rename するので、
/// ファイルを監視すると置き換えを見落とすからである。
final class ConfigWatcher {
    /// 連続した書き込みをまとめるために待つ時間。
    static let debounce: DispatchTimeInterval = .milliseconds(200)

    private let directory: URL
    private let fileName: String
    private let queue: DispatchQueue
    private let onChange: () -> Void
    private var fd: Int32 = -1
    private var source: (any DispatchSourceRead)?
    private var pendingWork: DispatchWorkItem?

    init(file: URL, queue: DispatchQueue, onChange: @escaping () -> Void) {
        self.directory = file.deletingLastPathComponent()
        self.fileName = file.lastPathComponent
        self.queue = queue
        self.onChange = onChange
    }

    /// `queue` の上で呼ぶ。ディレクトリがなければ作る（ファイルは作らない）。
    func start() {
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        fd = inotify_init1(Int32(IN_NONBLOCK | IN_CLOEXEC))
        guard fd >= 0 else {
            log("inotify を使えない。設定の反映には pazkey-ctl reload を使う")
            return
        }
        let mask = UInt32(IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM)
        guard inotify_add_watch(fd, directory.path, mask) >= 0 else {
            log("設定ディレクトリを監視できない（\(String(cString: strerror(errno)))）。設定の反映には pazkey-ctl reload を使う")
            Glibc.close(fd)
            fd = -1
            return
        }
        let s = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        s.setEventHandler { [weak self] in self?.readEvents() }
        s.resume()
        source = s
    }

    private func readEvents() {
        var buffer = [UInt8](repeating: 0, count: 64 * 1024)
        var relevant = false
        while true {
            let n = buffer.withUnsafeMutableBytes { Glibc.read(fd, $0.baseAddress, $0.count) }
            if n <= 0 { break }
            var offset = 0
            let header = MemoryLayout<inotify_event>.size
            while offset + header <= n {
                let (len, mask) = buffer.withUnsafeBytes { raw -> (Int, UInt32) in
                    let e = raw.loadUnaligned(fromByteOffset: offset, as: inotify_event.self)
                    return (Int(e.len), e.mask)
                }
                let nameBytes = buffer[(offset + header)..<min(offset + header + len, n)].prefix { $0 != 0 }
                let name = String(decoding: nameBytes, as: UTF8.self)
                if name == fileName || mask & UInt32(IN_Q_OVERFLOW) != 0 { relevant = true }
                offset += header + len
            }
        }
        guard relevant else { return }
        pendingWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.onChange() }
        pendingWork = work
        queue.asyncAfter(deadline: .now() + Self.debounce, execute: work)
    }
}
