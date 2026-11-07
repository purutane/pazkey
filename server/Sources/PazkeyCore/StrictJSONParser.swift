/// RFC 8259 の JSON を厳密に読む。JSONValue.parse から使う。
struct StrictJSONParser {
    private let bytes: [UInt8]
    private var i = 0
    private var depth = 0
    private static let maxDepth = 64

    init(_ bytes: [UInt8]) {
        self.bytes = bytes
    }

    mutating func parseDocument() -> JSONValue? {
        // 不正な UTF-8 を含むものは読まない。
        guard String(validating: bytes, as: UTF8.self) != nil else { return nil }
        skipWhitespace()
        guard let value = parseValue() else { return nil }
        skipWhitespace()
        return i == bytes.count ? value : nil
    }

    private var current: UInt8? { i < bytes.count ? bytes[i] : nil }

    private mutating func skipWhitespace() {
        while let c = current, c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D { i += 1 }
    }

    private mutating func expect(_ literal: String) -> Bool {
        let lit = Array(literal.utf8)
        guard i + lit.count <= bytes.count, Array(bytes[i..<i + lit.count]) == lit else { return false }
        i += lit.count
        return true
    }

    private mutating func parseValue() -> JSONValue? {
        guard let c = current else { return nil }
        switch c {
        case UInt8(ascii: "{"): return parseObject()
        case UInt8(ascii: "["): return parseArray()
        case UInt8(ascii: "\""): return parseString().map(JSONValue.string)
        case UInt8(ascii: "t"): return expect("true") ? .bool(true) : nil
        case UInt8(ascii: "f"): return expect("false") ? .bool(false) : nil
        case UInt8(ascii: "n"): return expect("null") ? .null : nil
        default: return parseNumber()
        }
    }

    private mutating func parseObject() -> JSONValue? {
        depth += 1
        defer { depth -= 1 }
        guard depth <= Self.maxDepth else { return nil }
        i += 1
        var result: [String: JSONValue] = [:]
        skipWhitespace()
        if current == UInt8(ascii: "}") { i += 1; return .object(result) }
        while true {
            skipWhitespace()
            guard current == UInt8(ascii: "\""), let key = parseString() else { return nil }
            skipWhitespace()
            guard current == UInt8(ascii: ":") else { return nil }
            i += 1
            skipWhitespace()
            guard let value = parseValue() else { return nil }
            result[key] = value  // 重複したキーは後のものを使う（Python の json と同じ）
            skipWhitespace()
            if current == UInt8(ascii: ",") { i += 1; continue }
            if current == UInt8(ascii: "}") { i += 1; return .object(result) }
            return nil
        }
    }

    private mutating func parseArray() -> JSONValue? {
        depth += 1
        defer { depth -= 1 }
        guard depth <= Self.maxDepth else { return nil }
        i += 1
        var result: [JSONValue] = []
        skipWhitespace()
        if current == UInt8(ascii: "]") { i += 1; return .array(result) }
        while true {
            skipWhitespace()
            guard let value = parseValue() else { return nil }
            result.append(value)
            skipWhitespace()
            if current == UInt8(ascii: ",") { i += 1; continue }
            if current == UInt8(ascii: "]") { i += 1; return .array(result) }
            return nil
        }
    }

    private mutating func parseHex4() -> UInt32? {
        guard i + 4 <= bytes.count else { return nil }
        var v: UInt32 = 0
        for _ in 0..<4 {
            let c = bytes[i]
            let d: UInt32
            switch c {
            case 0x30...0x39: d = UInt32(c - 0x30)
            case 0x41...0x46: d = UInt32(c - 0x41 + 10)
            case 0x61...0x66: d = UInt32(c - 0x61 + 10)
            default: return nil
            }
            v = v * 16 + d
            i += 1
        }
        return v
    }

    private mutating func parseString() -> String? {
        i += 1  // "
        var out: [UInt8] = []
        while let c = current {
            switch c {
            case UInt8(ascii: "\""):
                i += 1
                return String(validating: out, as: UTF8.self)
            case UInt8(ascii: "\\"):
                i += 1
                guard let e = current else { return nil }
                i += 1
                switch e {
                case UInt8(ascii: "\""): out.append(0x22)
                case UInt8(ascii: "\\"): out.append(0x5C)
                case UInt8(ascii: "/"): out.append(0x2F)
                case UInt8(ascii: "b"): out.append(0x08)
                case UInt8(ascii: "f"): out.append(0x0C)
                case UInt8(ascii: "n"): out.append(0x0A)
                case UInt8(ascii: "r"): out.append(0x0D)
                case UInt8(ascii: "t"): out.append(0x09)
                case UInt8(ascii: "u"):
                    guard var code = parseHex4() else { return nil }
                    if (0xD800...0xDBFF).contains(code) {
                        guard expect("\\u"), let low = parseHex4(), (0xDC00...0xDFFF).contains(low) else { return nil }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00)
                    }
                    // 対になっていないサロゲートは文字にできないので、不正とする。
                    guard let scalar = Unicode.Scalar(code) else { return nil }
                    out.append(contentsOf: Array(String(Character(scalar)).utf8))
                default: return nil
                }
            case 0x00..<0x20:
                return nil  // 制御文字は生で書けない
            default:
                out.append(c)
                i += 1
            }
        }
        return nil
    }

    private mutating func parseNumber() -> JSONValue? {
        let start = i
        if current == UInt8(ascii: "-") { i += 1 }
        guard let first = current, (0x30...0x39).contains(first) else { return nil }
        if first == 0x30 {
            i += 1
        } else {
            while let c = current, (0x30...0x39).contains(c) { i += 1 }
        }
        if current == UInt8(ascii: ".") {
            i += 1
            guard let c = current, (0x30...0x39).contains(c) else { return nil }
            while let c = current, (0x30...0x39).contains(c) { i += 1 }
        }
        if current == UInt8(ascii: "e") || current == UInt8(ascii: "E") {
            i += 1
            if current == UInt8(ascii: "+") || current == UInt8(ascii: "-") { i += 1 }
            guard let c = current, (0x30...0x39).contains(c) else { return nil }
            while let c = current, (0x30...0x39).contains(c) { i += 1 }
        }
        let text = String(decoding: bytes[start..<i], as: UTF8.self)
        guard let d = Double(text) else { return nil }
        return .number(d)
    }
}
