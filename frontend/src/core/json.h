// JSON の値と、RFC 8259 に厳密に従う読み取り、1行への書き出し。
// サーバーの StrictJSONParser.swift と同じ規則で読む（設定の判定をそろえるため）。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pazkey {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() : v_(nullptr) {}
    Json(std::nullptr_t) : v_(nullptr) {}
    Json(bool b) : v_(b) {}
    Json(double d) : v_(d) {}
    Json(int i) : v_(static_cast<double>(i)) {}
    Json(int64_t i) : v_(static_cast<double>(i)) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(const char *s) : v_(std::string(s)) {}
    Json(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(v_); }
    const bool *asBool() const { return std::get_if<bool>(&v_); }
    const double *asNumber() const { return std::get_if<double>(&v_); }
    const std::string *asString() const { return std::get_if<std::string>(&v_); }
    const Array *asArray() const;
    const Object *asObject() const;

    /// 小数部のない数値なら整数として返す（JSON Schema の integer と同じ扱い。`5.0` も整数）。
    std::optional<int64_t> asInt() const;

    /// オブジェクトのキーの値。オブジェクトでないか、キーがなければ nullptr。
    const Json *get(std::string_view key) const;

    /// 1行の JSON に書き出す（キーは辞書順、非 ASCII はそのまま、制御文字はエスケープする）。
    std::string serialize() const;

    /// 読む。読めなければ nullopt。不正な UTF-8、BOM、末尾のカンマ、NaN、対になっていないサロゲートは読めない。
    static std::optional<Json> parse(std::string_view bytes);

private:
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v_;
};

/// 文字列を JSON の文字列リテラル（引用符付き）にする。
std::string jsonQuote(std::string_view s);

}  // namespace pazkey
