#include <gtest/gtest.h>

#include "mozc_defaults.h"
#include "romaji.h"
#include "utf8.h"

using namespace pazkey;

namespace {

/// 文字列を1文字ずつ打ち、最後に残ったローマ字は残したまま返す。
std::pair<std::u32string, std::string> type(std::string_view keys) {
    std::u32string out;
    std::string tail;
    for (char c : keys) {
        auto step = feedRomaji(RomajiTable::builtin(), tail, c);
        out += step.output;
        tail = step.tail;
    }
    return {out, tail};
}

std::u32string typeAndFlush(std::string_view keys) {
    auto [out, tail] = type(keys);
    return out + flushRomaji(tail);
}

}  // namespace

TEST(Romaji, TableIsLoaded) { EXPECT_GT(RomajiTable::builtin().size(), 300u); }

TEST(Romaji, Basic) {
    EXPECT_EQ(typeAndFlush("ka"), U"か");
    EXPECT_EQ(typeAndFlush("kya"), U"きゃ");
    EXPECT_EQ(typeAndFlush("kikai"), U"きかい");
    EXPECT_EQ(typeAndFlush("shinbun"), U"しんぶん");
    EXPECT_EQ(typeAndFlush("xa"), U"ぁ");
    EXPECT_EQ(typeAndFlush("la"), U"ぁ");
    EXPECT_EQ(typeAndFlush("-"), U"ー");
    EXPECT_EQ(typeAndFlush("tsu"), U"つ");
    EXPECT_EQ(typeAndFlush("vu"), U"ゔ");
}

TEST(Romaji, N) {
    EXPECT_EQ(typeAndFlush("nn"), U"ん");
    EXPECT_EQ(typeAndFlush("kanna"), U"かんあ");  // Mozc と同じ。nn で「ん」
    EXPECT_EQ(typeAndFlush("kannna"), U"かんな");
    EXPECT_EQ(typeAndFlush("kanji"), U"かんじ");  // n + 子音 → ん + 子音
    EXPECT_EQ(typeAndFlush("konnyaku"), U"こんやく");
    EXPECT_EQ(typeAndFlush("kon'ya"), U"こんや");
    EXPECT_EQ(typeAndFlush("nya"), U"にゃ");
    // 末尾の n は、変換キーなどで「ん」になる。
    auto [out, tail] = type("hon");
    EXPECT_EQ(out, U"ほ");
    EXPECT_EQ(tail, "n");
    EXPECT_EQ(flushRomaji(tail), U"ん");
}

TEST(Romaji, DoubleConsonant) {
    EXPECT_EQ(typeAndFlush("kitte"), U"きって");
    EXPECT_EQ(typeAndFlush("matcha"), U"まっちゃ");
    EXPECT_EQ(typeAndFlush("www"), U"www");  // Mozc の表の www → w + ww
}

TEST(Romaji, NonRomajiStaysAsIs) {
    EXPECT_EQ(typeAndFlush("2026nen"), U"2026ねん");
    EXPECT_EQ(typeAndFlush("ky"), U"ky");  // n 以外の残りはそのまま
    EXPECT_EQ(typeAndFlush("kq"), U"kq");
    EXPECT_EQ(typeAndFlush("tca"), U"tか");
    EXPECT_EQ(typeAndFlush("A"), U"A");
}

TEST(Romaji, Punctuation) {
    EXPECT_EQ(typeAndFlush("a,b."), U"あ、b。");
    EXPECT_EQ(typeAndFlush("z/"), U"・");
}

TEST(Romaji, MozcSymbolMethod) {
    // 表にない / も、Mozc の symbol_method の既定（CORNER_BRACKET_MIDDLE_DOT）で ・ になる。
    EXPECT_EQ(typeAndFlush("/"), U"・");
    EXPECT_EQ(typeAndFlush("[a]"), U"「あ」");
    EXPECT_EQ(typeAndFlush("z/"), U"・");  // 表の規則はそのまま
}

TEST(Romaji, MozcPunctuationSymbolChoices) {
    // 選択肢ごとの規則（今は既定値だけを使うが、あとで設定から切り替えられるように Mozc の選択肢の形で持つ）。
    using namespace mozc;
    auto rules = [](PunctuationMethod p, SymbolMethod s) {
        std::u32string out;
        for (auto &[input, output] : punctuationSymbolRules(p, s)) out += output;
        return out;
    };
    EXPECT_EQ(rules(PunctuationMethod::TOUTEN_KUTEN, SymbolMethod::CORNER_BRACKET_MIDDLE_DOT), U"、。・「」");
    EXPECT_EQ(rules(PunctuationMethod::COMMA_PERIOD, SymbolMethod::SQUARE_BRACKET_SLASH), U"，．／［］");
    EXPECT_EQ(rules(PunctuationMethod::TOUTEN_PERIOD, SymbolMethod::CORNER_BRACKET_SLASH), U"、．／「」");
    EXPECT_EQ(rules(PunctuationMethod::COMMA_KUTEN, SymbolMethod::SQUARE_BRACKET_MIDDLE_DOT), U"，。・［］");
}
