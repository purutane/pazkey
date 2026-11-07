// ローマ字からかなへの変換（docs/frontend.md「ローマ字かな変換」）。
// 表は Mozc の romanji-hiragana.tsv（data/romaji.tsv）。変換は状態を持たない関数として作る。
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pazkey {

class RomajiTable {
public:
    struct Rule {
        std::u32string output;  // 出力するかな
        std::string rest;       // 出力した後に残すローマ字（例: tt → っ + t）
    };

    /// TSV（入力、出力、残すもの、の2〜3列）から作る。空行は読み飛ばす。
    static RomajiTable parse(std::string_view tsv);
    /// ビルド時に組み込んだ表に、Mozc の punctuation_method と symbol_method の規則を上書きしたもの
    /// （mozc_defaults.h）。
    static const RomajiTable &builtin();

    /// 規則を足す。同じ入力の規則があれば置き換える。
    void add(const std::string &input, Rule rule);

    const Rule *find(std::string_view input) const;
    /// input で始まり、input より長い規則があるか。
    bool isStrictPrefix(std::string_view input) const;
    size_t size() const { return rules_.size(); }

private:
    std::unordered_map<std::string, Rule> rules_;
    std::unordered_set<std::string> prefixes_;
};

struct RomajiStep {
    std::u32string output;  // 確定したかな（ローマ字にならない文字はそのまま入る）
    std::string tail;       // まだかなにならないローマ字の末尾
};

/// tail の後ろに1文字 c を打ったときの結果。c は ASCII の印字可能文字。
RomajiStep feedRomaji(const RomajiTable &table, std::string_view tail, char c);

/// 変換キー、確定キー、カーソル移動のときに残っている tail をかなにする。
/// `n` だけなら「ん」にし、それ以外はそのまま（半角英字のまま）返す。
std::u32string flushRomaji(std::string_view tail);

/// ローマ字で入力できる文字か（ASCII の印字可能文字。空白は含まない）。
bool isRomajiInputChar(char32_t c);

}  // namespace pazkey
