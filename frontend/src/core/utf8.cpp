#include "utf8.h"

#include <unordered_map>

namespace pazkey {

std::optional<std::u32string> decodeUtf8(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        auto b0 = static_cast<unsigned char>(s[i]);
        char32_t cp;
        size_t len;
        char32_t min;
        if (b0 < 0x80) {
            out.push_back(b0);
            ++i;
            continue;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            len = 2;
            min = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            len = 3;
            min = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            len = 4;
            min = 0x10000;
        } else {
            return std::nullopt;
        }
        if (i + len > s.size()) return std::nullopt;
        for (size_t k = 1; k < len; ++k) {
            auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) return std::nullopt;
            cp = (cp << 6) | (b & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return std::nullopt;
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string encodeUtf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t c : s) {
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::string lastCodepoints(std::string_view s, size_t n) {
    auto decoded = decodeUtf8(s);
    if (!decoded) return {};
    if (decoded->size() <= n) return std::string(s);
    return encodeUtf8(std::u32string_view(*decoded).substr(decoded->size() - n));
}

std::u32string toKatakana(std::u32string_view s) {
    std::u32string out(s);
    for (auto &c : out) {
        // ぁ(3041)〜ゖ(3096) と ゝゞ(309D, 309E) はカタカナと 0x60 ずれている。
        if ((c >= 0x3041 && c <= 0x3096) || c == 0x309D || c == 0x309E) c += 0x60;
    }
    return out;
}

namespace {

const std::unordered_map<char32_t, std::u32string> &halfKanaTable() {
    static const std::unordered_map<char32_t, std::u32string> table = [] {
        std::unordered_map<char32_t, std::u32string> t;
        // 全角カタカナ（ァ〜ヶ、ヴ、ー）と半角カタカナの対応。
        const std::u32string full =
            U"ァアィイゥウェエォオカキクケコサシスセソタチッツテトナニヌネノハヒフヘホマミムメモャヤュユョヨラリルレロワヲンー"
            U"。「」、・ヮヵヶ";
        const std::u32string half =
            U"ｧｱｨｲｩｳｪｴｫｵｶｷｸｹｺｻｼｽｾｿﾀﾁｯﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓｬﾔｭﾕｮﾖﾗﾘﾙﾚﾛﾜｦﾝｰ"
            U"｡｢｣､･ﾜｶｹ";
        for (size_t i = 0; i < full.size(); ++i) t[full[i]] = std::u32string(1, half[i]);
        const std::u32string dakuon = U"ガギグゲゴザジズゼゾダヂヅデドバビブベボ";
        const std::u32string dakuBase = U"ｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾊﾋﾌﾍﾎ";
        for (size_t i = 0; i < dakuon.size(); ++i) t[dakuon[i]] = std::u32string{dakuBase[i], U'ﾞ'};
        const std::u32string handakuon = U"パピプペポ";
        const std::u32string handakuBase = U"ﾊﾋﾌﾍﾎ";
        for (size_t i = 0; i < handakuon.size(); ++i) t[handakuon[i]] = std::u32string{handakuBase[i], U'ﾟ'};
        t[U'ヴ'] = U"ｳﾞ";
        t[U'ヷ'] = U"ﾜﾞ";
        t[U'ヺ'] = U"ｦﾞ";
        t[U'〜'] = U"~";
        return t;
    }();
    return table;
}

}  // namespace

std::u32string toHalfKatakana(std::u32string_view s) {
    const auto &table = halfKanaTable();
    std::u32string out;
    for (char32_t c : toKatakana(s)) {
        auto it = table.find(c);
        if (it != table.end()) {
            out += it->second;
        } else if (c >= 0xFF01 && c <= 0xFF5E) {
            out.push_back(c - 0xFEE0);
        } else if (c == 0x3000) {
            out.push_back(U' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string toFullWidthAscii(std::u32string_view s) {
    std::u32string out(s);
    for (char32_t &c : out) {
        if (c >= 0x21 && c <= 0x7E) c += 0xFEE0;
    }
    return out;
}

std::u32string toReadingForm(std::u32string_view s) {
    std::u32string out(s);
    for (char32_t &c : out) {
        const bool digit = c >= U'０' && c <= U'９';
        const bool upper = c >= U'Ａ' && c <= U'Ｚ';
        const bool lower = c >= U'ａ' && c <= U'ｚ';
        if (digit || upper || lower) c -= 0xFEE0;
    }
    return out;
}

}  // namespace pazkey
