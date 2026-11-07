import Foundation

/// JSON の値。真偽値と数値を取り違えないために、Foundation の `Any` ではなくこれで扱う。
public enum JSONValue: Equatable, Sendable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    /// JSON として解釈する。読めなければ nil。
    ///
    /// RFC 8259 に厳密に従う（UTF-8、BOM なし、末尾のカンマや NaN を認めない）。
    /// Foundation の JSONDecoder は末尾のカンマや BOM を受け付けるので使わない。
    public static func parse(_ data: Data) -> JSONValue? {
        var parser = StrictJSONParser(Array(data))
        return parser.parseDocument()
    }

    public static func parse(_ text: String) -> JSONValue? {
        parse(Data(text.utf8))
    }

    public subscript(key: String) -> JSONValue? {
        if case let .object(o) = self { return o[key] }
        return nil
    }

    public var stringValue: String? {
        if case let .string(s) = self { return s }
        return nil
    }

    public var boolValue: Bool? {
        if case let .bool(b) = self { return b }
        return nil
    }

    /// 小数部のない数値なら整数として返す（JSON Schema の integer と同じ扱い。`5.0` も整数）。
    public var intValue: Int? {
        guard case let .number(d) = self, d.isFinite, d.rounded() == d,
              d >= -9.007_199_254_740_992e15, d <= 9.007_199_254_740_992e15 else { return nil }
        return Int(d)
    }
}

extension JSONValue: Encodable {
    public func encode(to encoder: any Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .null: try c.encodeNil()
        case let .bool(b): try c.encode(b)
        case let .number(d):
            if let i = self.intValue { try c.encode(i) } else { try c.encode(d) }
        case let .string(s): try c.encode(s)
        case let .array(a): try c.encode(a)
        case let .object(o): try c.encode(o)
        }
    }
}

extension JSONValue {
    /// 1行の JSON（改行を含まない）に書き出す。キーの順は辞書順。
    public func serialized() -> String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        // JSONEncoder は生の改行を書かない（文字列中の改行は \n にエスケープされる）。
        return String(decoding: (try? encoder.encode(self)) ?? Data("null".utf8), as: UTF8.self)
    }
}

extension JSONValue: ExpressibleByStringLiteral, ExpressibleByIntegerLiteral, ExpressibleByBooleanLiteral,
    ExpressibleByDictionaryLiteral, ExpressibleByArrayLiteral {
    public init(stringLiteral value: String) { self = .string(value) }
    public init(integerLiteral value: Int) { self = .number(Double(value)) }
    public init(booleanLiteral value: Bool) { self = .bool(value) }
    public init(dictionaryLiteral elements: (String, JSONValue)...) {
        self = .object(Dictionary(elements, uniquingKeysWith: { _, b in b }))
    }
    public init(arrayLiteral elements: JSONValue...) { self = .array(elements) }
}
