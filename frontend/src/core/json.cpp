#include "json.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale.h>

#include "utf8.h"

namespace pazkey {

const Json::Array *Json::asArray() const {
    auto p = std::get_if<std::shared_ptr<Array>>(&v_);
    return p ? p->get() : nullptr;
}

const Json::Object *Json::asObject() const {
    auto p = std::get_if<std::shared_ptr<Object>>(&v_);
    return p ? p->get() : nullptr;
}

std::optional<int64_t> Json::asInt() const {
    const double *d = asNumber();
    if (!d || !std::isfinite(*d) || std::round(*d) != *d) return std::nullopt;
    if (*d < -9007199254740992.0 || *d > 9007199254740992.0) return std::nullopt;
    return static_cast<int64_t>(*d);
}

const Json *Json::get(std::string_view key) const {
    const Object *o = asObject();
    if (!o) return nullptr;
    auto it = o->find(std::string(key));
    return it == o->end() ? nullptr : &it->second;
}

std::string jsonQuote(std::string_view s) {
    std::string out = "\"";
    for (char ch : s) {
        auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20 || c == 0x7F) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(ch);
            }
        }
    }
    out += "\"";
    return out;
}

std::string Json::serialize() const {
    if (isNull()) return "null";
    if (auto b = asBool()) return *b ? "true" : "false";
    if (auto i = asInt()) return std::to_string(*i);
    if (auto d = asNumber()) {
        if (!std::isfinite(*d)) return "null";
        char buf[64];
        // %.17g は小数点に '.' を使う C ロケールの書式でなければならない。
        locale_t c = newlocale(LC_NUMERIC_MASK, "C", nullptr);
        locale_t old = uselocale(c);
        std::snprintf(buf, sizeof buf, "%.17g", *d);
        uselocale(old);
        freelocale(c);
        return buf;
    }
    if (auto s = asString()) return jsonQuote(*s);
    std::string out;
    if (auto a = asArray()) {
        out = "[";
        for (size_t i = 0; i < a->size(); ++i) {
            if (i) out += ",";
            out += (*a)[i].serialize();
        }
        return out + "]";
    }
    out = "{";
    bool first = true;
    for (const auto &[k, v] : *asObject()) {
        if (!first) out += ",";
        first = false;
        out += jsonQuote(k) + ":" + v.serialize();
    }
    return out + "}";
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view b) : b_(b) {}

    std::optional<Json> document() {
        if (!decodeUtf8(b_)) return std::nullopt;  // 不正な UTF-8 を含むものは読まない
        ws();
        auto v = value();
        if (!v) return std::nullopt;
        ws();
        return i_ == b_.size() ? v : std::nullopt;
    }

private:
    static constexpr int kMaxDepth = 64;
    std::string_view b_;
    size_t i_ = 0;
    int depth_ = 0;

    int cur() const { return i_ < b_.size() ? static_cast<unsigned char>(b_[i_]) : -1; }
    void ws() {
        while (cur() == ' ' || cur() == '\t' || cur() == '\n' || cur() == '\r') ++i_;
    }
    bool literal(std::string_view lit) {
        if (b_.substr(i_, lit.size()) != lit) return false;
        i_ += lit.size();
        return true;
    }
    static bool digit(int c) { return c >= '0' && c <= '9'; }

    std::optional<Json> value() {
        switch (cur()) {
        case '{': return object();
        case '[': return array();
        case '"': {
            auto s = string();
            if (!s) return std::nullopt;
            return Json(std::move(*s));
        }
        case 't': return literal("true") ? std::optional<Json>(Json(true)) : std::nullopt;
        case 'f': return literal("false") ? std::optional<Json>(Json(false)) : std::nullopt;
        case 'n': return literal("null") ? std::optional<Json>(Json(nullptr)) : std::nullopt;
        default: return number();
        }
    }

    std::optional<Json> object() {
        if (++depth_ > kMaxDepth) return std::nullopt;
        ++i_;
        Json::Object result;
        ws();
        if (cur() == '}') {
            ++i_;
            --depth_;
            return Json(std::move(result));
        }
        while (true) {
            ws();
            if (cur() != '"') return std::nullopt;
            auto key = string();
            if (!key) return std::nullopt;
            ws();
            if (cur() != ':') return std::nullopt;
            ++i_;
            ws();
            auto v = value();
            if (!v) return std::nullopt;
            result[*key] = std::move(*v);  // 重複したキーは後のものを使う（Python の json と同じ）
            ws();
            if (cur() == ',') {
                ++i_;
                continue;
            }
            if (cur() == '}') {
                ++i_;
                --depth_;
                return Json(std::move(result));
            }
            return std::nullopt;
        }
    }

    std::optional<Json> array() {
        if (++depth_ > kMaxDepth) return std::nullopt;
        ++i_;
        Json::Array result;
        ws();
        if (cur() == ']') {
            ++i_;
            --depth_;
            return Json(std::move(result));
        }
        while (true) {
            ws();
            auto v = value();
            if (!v) return std::nullopt;
            result.push_back(std::move(*v));
            ws();
            if (cur() == ',') {
                ++i_;
                continue;
            }
            if (cur() == ']') {
                ++i_;
                --depth_;
                return Json(std::move(result));
            }
            return std::nullopt;
        }
    }

    std::optional<char32_t> hex4() {
        if (i_ + 4 > b_.size()) return std::nullopt;
        char32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            int c = cur();
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else return std::nullopt;
            v = v * 16 + d;
            ++i_;
        }
        return v;
    }

    std::optional<std::string> string() {
        ++i_;  // "
        std::string out;
        while (cur() >= 0) {
            int c = cur();
            if (c == '"') {
                ++i_;
                return out;
            }
            if (c == '\\') {
                ++i_;
                int e = cur();
                if (e < 0) return std::nullopt;
                ++i_;
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    auto code = hex4();
                    if (!code) return std::nullopt;
                    if (*code >= 0xD800 && *code <= 0xDBFF) {
                        if (!literal("\\u")) return std::nullopt;
                        auto low = hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF) return std::nullopt;
                        *code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
                    } else if (*code >= 0xDC00 && *code <= 0xDFFF) {
                        return std::nullopt;  // 対になっていないサロゲートは文字にできない
                    }
                    out += encodeUtf8(std::u32string(1, *code));
                    break;
                }
                default: return std::nullopt;
                }
                continue;
            }
            if (c < 0x20) return std::nullopt;  // 制御文字は生で書けない
            out.push_back(static_cast<char>(c));
            ++i_;
        }
        return std::nullopt;
    }

    std::optional<Json> number() {
        size_t start = i_;
        if (cur() == '-') ++i_;
        if (!digit(cur())) return std::nullopt;
        if (cur() == '0') {
            ++i_;
        } else {
            while (digit(cur())) ++i_;
        }
        if (cur() == '.') {
            ++i_;
            if (!digit(cur())) return std::nullopt;
            while (digit(cur())) ++i_;
        }
        if (cur() == 'e' || cur() == 'E') {
            ++i_;
            if (cur() == '+' || cur() == '-') ++i_;
            if (!digit(cur())) return std::nullopt;
            while (digit(cur())) ++i_;
        }
        std::string text(b_.substr(start, i_ - start));
        // strtod は桁あふれで ±HUGE_VAL、桁落ちで 0 を返す（Swift の Double(String) と同じ）。
        locale_t c = newlocale(LC_NUMERIC_MASK, "C", nullptr);
        double d = strtod_l(text.c_str(), nullptr, c);
        freelocale(c);
        return Json(d);
    }
};

}  // namespace

std::optional<Json> Json::parse(std::string_view bytes) { return Parser(bytes).document(); }

}  // namespace pazkey
