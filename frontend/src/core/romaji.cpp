#include "romaji.h"

#include "mozc_defaults.h"
#include "romaji_table.h"  // ビルド時に data/romaji.tsv から作る（kRomajiTsv）
#include "utf8.h"

namespace pazkey {

RomajiTable RomajiTable::parse(std::string_view tsv) {
    RomajiTable table;
    size_t pos = 0;
    while (pos < tsv.size()) {
        size_t end = tsv.find('\n', pos);
        if (end == std::string_view::npos) end = tsv.size();
        std::string_view line = tsv.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string_view::npos || t1 == 0) continue;
        std::string input(line.substr(0, t1));
        std::string_view rest = line.substr(t1 + 1);
        size_t t2 = rest.find('\t');
        std::string_view output = rest.substr(0, t2);
        std::string_view remain = t2 == std::string_view::npos ? std::string_view{} : rest.substr(t2 + 1);
        auto decoded = decodeUtf8(output);
        if (!decoded) continue;
        table.add(input, Rule{*decoded, std::string(remain)});
    }
    return table;
}

void RomajiTable::add(const std::string &input, Rule rule) {
    rules_[input] = std::move(rule);
    for (size_t k = 1; k < input.size(); ++k) prefixes_.insert(input.substr(0, k));
}

const RomajiTable &RomajiTable::builtin() {
    static const RomajiTable table = [] {
        RomajiTable t = parse(kRomajiTsv);
        for (auto &[input, output] : mozc::punctuationSymbolRules(mozc::kPunctuationMethod, mozc::kSymbolMethod)) {
            t.add(input, Rule{output, ""});
        }
        return t;
    }();
    return table;
}

const RomajiTable::Rule *RomajiTable::find(std::string_view input) const {
    auto it = rules_.find(std::string(input));
    return it == rules_.end() ? nullptr : &it->second;
}

bool RomajiTable::isStrictPrefix(std::string_view input) const {
    return prefixes_.contains(std::string(input));
}

RomajiStep feedRomaji(const RomajiTable &table, std::string_view tail, char c) {
    RomajiStep step;
    std::string pending(tail);
    pending.push_back(c);
    // 表の規則は有限なので、この繰り返しは必ず終わる。念のため回数に上限を置く。
    for (int guard = 0; !pending.empty() && guard < 64; ++guard) {
        // もっと長い規則の途中なら待つ（「n」のように、それ自体が規則でも待つ）。
        if (table.isStrictPrefix(pending)) break;
        if (const auto *rule = table.find(pending)) {
            step.output += rule->output;
            pending = rule->rest;
            continue;
        }
        // どの規則にもならない。先頭から最も長く規則に当たる部分を出し、残りを読み直す。
        bool matched = false;
        for (size_t k = pending.size() - 1; k >= 1; --k) {
            if (const auto *rule = table.find(std::string_view(pending).substr(0, k))) {
                step.output += rule->output;
                pending = rule->rest + pending.substr(k);
                matched = true;
                break;
            }
        }
        if (!matched) {
            // 先頭の1文字はかなにならない。そのまま出す。
            step.output.push_back(static_cast<unsigned char>(pending[0]));
            pending.erase(0, 1);
        }
    }
    step.tail = pending;
    return step;
}

std::u32string flushRomaji(std::string_view tail) {
    if (tail == "n") return U"ん";
    std::u32string out;
    for (char c : tail) out.push_back(static_cast<unsigned char>(c));
    return out;
}

bool isRomajiInputChar(char32_t c) { return c >= 0x21 && c <= 0x7E; }

}  // namespace pazkey
