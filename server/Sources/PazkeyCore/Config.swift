import Foundation

/// 設定。項目と既定値は docs/config.md と config.schema.json に合わせる。
public struct Config: Equatable, Sendable {
    public var version = 1
    public var candidates = Candidates()
    public var liveConversion = LiveConversion()
    public var context = Context()
    public var timeout = Timeout()
    public var zenzai = Zenzai()
    public var learning = Learning()
    public var fallback = Fallback()

    public struct Candidates: Equatable, Sendable {
        public var pageSize = 9
        public var maxPages = 5
        public var selectWithDigits = true
    }
    public struct LiveConversion: Equatable, Sendable {
        public var enabled = false
    }
    public struct Context: Equatable, Sendable {
        public var enabled = true
        public var maxChars = 64
    }
    public struct Timeout: Equatable, Sendable {
        public var fullMs = 1000
    }
    public struct Zenzai: Equatable, Sendable {
        public var enabled = true
        public var modelPath = ""
        public var inferenceLimit = 10
        public var device = "auto"
    }
    public struct Learning: Equatable, Sendable {
        public var enabled = true
        public var saveEveryCommits = 20
        public var saveIdleSeconds = 60
    }
    public struct Fallback: Equatable, Sendable {
        public var maxCrashes = 3
        public var windowSeconds = 300
    }

    public init() {}

    /// `full` で返す候補の最大数。
    public var maxCandidates: Int { candidates.pageSize * candidates.maxPages }
}

/// 設定の誤り1件。`path` は JSON Pointer（例: `/zenzai/inference_limit`）。
public struct ConfigError: Equatable, Sendable, CustomStringConvertible {
    public var path: String
    public var message: String
    public init(path: String, message: String) {
        self.path = path
        self.message = message
    }
    public var description: String { "\(path.isEmpty ? "/" : path): \(message)" }
}

public struct ConfigParseResult: Sendable {
    /// 正しければ設定。1つでも誤りがあれば nil（ファイル全体を不正とみなす）。
    public var config: Config?
    public var errors: [ConfigError]
    /// 知らないキー。無視して、ログに残す。
    public var unknownKeys: [String]
}

extension Config {
    private enum Kind {
        case integer(ClosedRange<Int>, WritableKeyPath<Config, Int>)
        case boolean(WritableKeyPath<Config, Bool>)
        case string(minLength: Int, WritableKeyPath<Config, String>)
    }

    /// config.schema.json と同じ項目と範囲。片方を変えたら両方を直す。
    private static var schema: [(section: String, fields: [(String, Kind)])] { [
        ("candidates", [
            ("page_size", .integer(1...9, \.candidates.pageSize)),
            ("max_pages", .integer(1...20, \.candidates.maxPages)),
            ("select_with_digits", .boolean(\.candidates.selectWithDigits)),
        ]),
        ("live_conversion", [
            ("enabled", .boolean(\.liveConversion.enabled)),
        ]),
        ("context", [
            ("enabled", .boolean(\.context.enabled)),
            ("max_chars", .integer(0...256, \.context.maxChars)),
        ]),
        ("timeout", [
            ("full_ms", .integer(50...5000, \.timeout.fullMs)),
        ]),
        ("zenzai", [
            ("enabled", .boolean(\.zenzai.enabled)),
            ("model_path", .string(minLength: 0, \.zenzai.modelPath)),
            ("inference_limit", .integer(1...1000, \.zenzai.inferenceLimit)),
            ("device", .string(minLength: 1, \.zenzai.device)),
        ]),
        ("learning", [
            ("enabled", .boolean(\.learning.enabled)),
            ("save_every_commits", .integer(1...1000, \.learning.saveEveryCommits)),
            ("save_idle_seconds", .integer(5...3600, \.learning.saveIdleSeconds)),
        ]),
        ("fallback", [
            ("max_crashes", .integer(1...100, \.fallback.maxCrashes)),
            ("window_seconds", .integer(10...86400, \.fallback.windowSeconds)),
        ]),
    ] }

    /// 設定ファイルの中身を検証して読む。書かれていない項目は既定値。
    public static func parse(_ data: Data) -> ConfigParseResult {
        guard let root = JSONValue.parse(data) else {
            return .init(config: nil, errors: [.init(path: "", message: "JSON として読めない")], unknownKeys: [])
        }
        guard case let .object(top) = root else {
            return .init(config: nil, errors: [.init(path: "", message: "オブジェクトでなければならない")], unknownKeys: [])
        }
        var config = Config()
        var errors: [ConfigError] = []
        var unknown: [String] = []

        if let v = top["version"] {
            if v.intValue != 1 || v.boolValue != nil {
                errors.append(.init(path: "/version", message: "1 でなければならない"))
            }
        }
        let sectionNames = Set(schema.map(\.section)).union(["version"])
        unknown += top.keys.filter { !sectionNames.contains($0) }.map { "/" + $0 }

        for (section, fields) in schema {
            guard let value = top[section] else { continue }
            guard case let .object(obj) = value else {
                errors.append(.init(path: "/\(section)", message: "オブジェクトでなければならない"))
                continue
            }
            let names = Set(fields.map(\.0))
            unknown += obj.keys.filter { !names.contains($0) }.map { "/\(section)/\($0)" }
            for (name, kind) in fields {
                guard let v = obj[name] else { continue }
                let path = "/\(section)/\(name)"
                switch kind {
                case let .integer(range, kp):
                    if let i = v.intValue, range.contains(i) {
                        config[keyPath: kp] = i
                    } else {
                        errors.append(.init(path: path, message: "\(range.lowerBound)〜\(range.upperBound) の整数でなければならない"))
                    }
                case let .boolean(kp):
                    if let b = v.boolValue {
                        config[keyPath: kp] = b
                    } else {
                        errors.append(.init(path: path, message: "true か false でなければならない"))
                    }
                case let .string(minLength, kp):
                    if let s = v.stringValue, s.unicodeScalars.count >= minLength {
                        config[keyPath: kp] = s
                    } else {
                        errors.append(.init(path: path, message: minLength > 0 ? "空でない文字列でなければならない" : "文字列でなければならない"))
                    }
                }
            }
        }
        return .init(config: errors.isEmpty ? config : nil, errors: errors, unknownKeys: unknown.sorted())
    }

    /// ファイルから読む。ファイルがなければ既定値（誤りなし）。
    public static func load(from url: URL) -> ConfigParseResult {
        guard FileManager.default.fileExists(atPath: url.path) else {
            return .init(config: Config(), errors: [], unknownKeys: [])
        }
        do {
            return parse(try Data(contentsOf: url))
        } catch {
            return .init(config: nil, errors: [.init(path: "", message: "読めない: \(error.localizedDescription)")], unknownKeys: [])
        }
    }
}
