// Mozc の設定に当たる入力の規則（docs/frontend.md「Mozc の設定との対応」）。
// 名前と選択肢は Mozc の src/protocol/config.proto と同じにし、Mozc の既定値を決め打ちにする。
// 設定で切り替えられるようにするときは、ここの値を設定から読むようにする。
// ローマ字表（data/romaji.tsv）はここに含めない。表は Mozc の表に手を加えずに写したものとして別に持つ（docs/frontend.md「ローマ字かな変換」）。
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pazkey::mozc {

enum class SessionKeymap { ATOK, MSIME, KOTOERI, MOBILE, CHROMEOS };
enum class PunctuationMethod { TOUTEN_KUTEN, COMMA_PERIOD, TOUTEN_PERIOD, COMMA_KUTEN };
enum class SymbolMethod { CORNER_BRACKET_MIDDLE_DOT, SQUARE_BRACKET_SLASH, CORNER_BRACKET_SLASH, SQUARE_BRACKET_MIDDLE_DOT };
enum class FundamentalCharacterForm { FUNDAMENTAL_INPUT_MODE, FUNDAMENTAL_FULL_WIDTH, FUNDAMENTAL_HALF_WIDTH };
enum class ShiftKeyModeSwitch { OFF, ASCII_INPUT_MODE, KATAKANA_INPUT_MODE };
enum class NumpadCharacterForm { NUMPAD_INPUT_MODE, NUMPAD_FULL_WIDTH, NUMPAD_HALF_WIDTH, NUMPAD_DIRECT_INPUT };
enum class CharacterForm { HALF_WIDTH, FULL_WIDTH, LAST_FORM, NO_CONVERSION };

/// キー操作。Linux での Mozc の既定（GetDefaultKeyMap）。キーの扱いは state.cpp のキー処理にある。
inline constexpr SessionKeymap kSessionKeymap = SessionKeymap::MSIME;
inline constexpr PunctuationMethod kPunctuationMethod = PunctuationMethod::TOUTEN_KUTEN;
inline constexpr SymbolMethod kSymbolMethod = SymbolMethod::CORNER_BRACKET_MIDDLE_DOT;
/// 入力がないときの Space の空白の幅。pazkey の入力モードはひらがなだけなので、INPUT_MODE は全角になる。
inline constexpr FundamentalCharacterForm kSpaceCharacterForm = FundamentalCharacterForm::FUNDAMENTAL_INPUT_MODE;
/// Shift で英字を打ったときの振る舞い。ASCII_INPUT_MODE だけを実装している。
inline constexpr ShiftKeyModeSwitch kShiftKeyModeSwitch = ShiftKeyModeSwitch::ASCII_INPUT_MODE;
/// テンキーから入れる文字の幅。NUMPAD_HALF_WIDTH だけを実装している。
inline constexpr NumpadCharacterForm kNumpadCharacterForm = NumpadCharacterForm::NUMPAD_HALF_WIDTH;
/// character_form_rules の入力中の字種（Mozc の既定では、すべてのグループで全角）。FULL_WIDTH だけを実装している。
/// 変換後の字種（Mozc の既定は英数と記号で LAST_FORM）は持たない。azooKey の候補と学習に任せる。
inline constexpr CharacterForm kPreeditCharacterForm = CharacterForm::FULL_WIDTH;

/// punctuation_method と symbol_method から、ローマ字表に上書きする規則（入力、出力）を作る。
/// Mozc と同じく、表の `,` `.` `/` `[` `]` の規則をこれで置き換える。
std::vector<std::pair<std::string, std::u32string>> punctuationSymbolRules(PunctuationMethod punctuation,
                                                                          SymbolMethod symbol);

/// space_character_form から、入力がないときの Space で確定する空白。
std::string spaceForEmpty(FundamentalCharacterForm form);

}  // namespace pazkey::mozc
