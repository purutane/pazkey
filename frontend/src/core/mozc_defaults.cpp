#include "mozc_defaults.h"

namespace pazkey::mozc {

std::vector<std::pair<std::string, std::u32string>> punctuationSymbolRules(PunctuationMethod punctuation,
                                                                          SymbolMethod symbol) {
    using P = PunctuationMethod;
    using S = SymbolMethod;
    const bool comma = punctuation == P::COMMA_PERIOD || punctuation == P::COMMA_KUTEN;
    const bool period = punctuation == P::COMMA_PERIOD || punctuation == P::TOUTEN_PERIOD;
    const bool slash = symbol == S::SQUARE_BRACKET_SLASH || symbol == S::CORNER_BRACKET_SLASH;
    const bool corner = symbol == S::CORNER_BRACKET_MIDDLE_DOT || symbol == S::CORNER_BRACKET_SLASH;
    return {
        {",", comma ? U"，" : U"、"},
        {".", period ? U"．" : U"。"},
        {"/", slash ? U"／" : U"・"},
        {"[", corner ? U"「" : U"［"},
        {"]", corner ? U"」" : U"］"},
    };
}

std::string spaceForEmpty(FundamentalCharacterForm form) {
    return form == FundamentalCharacterForm::FUNDAMENTAL_HALF_WIDTH ? " " : "　";
}

}  // namespace pazkey::mozc
