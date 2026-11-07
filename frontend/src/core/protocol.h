// サーバーとの間のメッセージ（docs/protocol.md）。1メッセージ = JSON 1つ + 改行。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pazkey {

inline constexpr int kProtocolVersion = 1;
inline constexpr size_t kMaxLineBytes = 1 << 20;  // 1行の上限（1 MiB）
/// `reading` の長さの上限（コードポイント数）。値と理由は docs/protocol.md「文字数の単位」。
inline constexpr size_t kMaxReadingLength = 256;

struct Candidate {
    std::string text;
    size_t consumed = 0;  // 読みの先頭から使うコードポイント数
    bool operator==(const Candidate &) const = default;
};

enum class ConvertKind { Live, Full };

// 送るメッセージ。どれも末尾に改行を付けて返す。
std::string helloMessage(std::string_view version);
std::string convertMessage(int64_t session, int64_t seq, ConvertKind kind, std::string_view reading, size_t cursor,
                           std::string_view leftContext);
std::string commitMessage(int64_t session, int64_t seq, int64_t index);
std::string resetMessage(int64_t session);
std::string closeMessage(int64_t session);

// 受け取るメッセージ。
struct Incoming {
    std::string type;
    std::optional<int64_t> session;
    std::optional<int64_t> seq;
    std::vector<Candidate> candidates;  // result
    std::string code;                   // error
    std::string message;                // error
    std::optional<int64_t> protocol;    // hello
    std::string zenzai;                 // hello、config_changed（古いサーバーの config_changed では空）
};

/// 1行（改行を除く）を読む。JSON として読めない、または type がなければ nullopt。
/// result の候補に不正なもの（text が文字列でない、consumed が負など）があれば nullopt。
std::optional<Incoming> parseIncoming(std::string_view line);

}  // namespace pazkey
