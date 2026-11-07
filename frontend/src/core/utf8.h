// UTF-8 とコードポイントの扱い、かなの字種の変換。
// 文字数はすべてコードポイント数で数える（docs/protocol.md「文字数の単位」）。
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pazkey {

/// 正しい UTF-8 ならコードポイントの列に直す。不正（過長、サロゲート、範囲外、途中で切れている）なら nullopt。
std::optional<std::u32string> decodeUtf8(std::string_view s);

/// コードポイントの列を UTF-8 にする。サロゲートと範囲外の値は U+FFFD にする。
std::string encodeUtf8(std::u32string_view s);

/// 末尾の n コードポイントを返す。s は正しい UTF-8 であること（不正なら空を返す）。
std::string lastCodepoints(std::string_view s, size_t n);

/// ひらがなを全角カタカナにする。ほかの文字はそのまま。
std::u32string toKatakana(std::u32string_view s);

/// ひらがなと全角カタカナ、かなの記号を半角カタカナにする。全角の英数字と記号、全角の空白は半角にする。
/// ほかの文字はそのまま。濁点と半濁点の付いた文字は2つのコードポイントになる（例: が → ｶﾞ）。
std::u32string toHalfKatakana(std::u32string_view s);

/// ASCII の印字可能文字（`!`〜`~`）を全角（U+FF01〜U+FF5E）にする。ほかの文字はそのまま。
std::u32string toFullWidthAscii(std::u32string_view s);

/// サーバーへ送る読みの形にする。全角の数字と英字を半角にし、記号やかなはそのまま（docs/frontend.md「字種」）。
std::u32string toReadingForm(std::u32string_view s);

}  // namespace pazkey
