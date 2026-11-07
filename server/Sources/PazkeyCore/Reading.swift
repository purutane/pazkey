import Foundation

/// `reading` の検証（docs/protocol.md「文字数の単位」）。
public enum ReadingCheck: Equatable, Sendable {
    /// `reading` の長さの上限（コードポイント数）。値と理由は docs/protocol.md「文字数の単位」。
    public static let maxLength = 256

    case ok
    case empty
    case tooLong
    case notNFC
    case combiningCharacter
    /// 書記素クラスタとコードポイントが1対1でない（ZWJ、国旗、CRLF など）。
    case multiScalarCharacter

    public static func check(_ reading: String) -> ReadingCheck {
        if reading.isEmpty { return .empty }
        if reading.unicodeScalars.count > maxLength { return .tooLong }
        if reading.unicodeScalars.contains(where: { s in
            switch s.properties.generalCategory {
            case .nonspacingMark, .spacingMark, .enclosingMark: return true
            default: return s.properties.canonicalCombiningClass.rawValue != 0
            }
        }) {
            return .combiningCharacter
        }
        // String の == は正準等価で比べるので、NFC かどうかはスカラー列で比べる。
        if !reading.unicodeScalars.elementsEqual(reading.precomposedStringWithCanonicalMapping.unicodeScalars) {
            return .notNFC
        }
        if reading.count != reading.unicodeScalars.count { return .multiScalarCharacter }
        return .ok
    }

    public var message: String {
        switch self {
        case .ok: "ok"
        case .empty: "reading が空"
        case .tooLong: "reading が長すぎる（上限は \(ReadingCheck.maxLength) コードポイント）"
        case .notNFC: "reading が NFC でない"
        case .combiningCharacter: "reading に結合文字が含まれている"
        case .multiScalarCharacter: "reading に複数のコードポイントからなる文字が含まれている"
        }
    }
}
