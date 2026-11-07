#include "config.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

#include "json.h"
#include "utf8.h"

namespace pazkey {

namespace {

struct Field {
    std::string name;
    enum class Kind { Integer, Boolean, String } kind;
    int64_t min = 0, max = 0;  // Integer のとき
    size_t minLength = 0;      // String のとき
    std::function<void(Config &, const Json &)> assign;
    std::function<std::string(const Config &)> dump;  // config.json に書く値（JSON の字面）
};

struct Section {
    std::string name;
    std::vector<Field> fields;
};

template <typename F>
Field intField(std::string name, int64_t min, int64_t max, F ref) {
    return {std::move(name), Field::Kind::Integer, min, max, 0,
            [ref](Config &c, const Json &v) { ref(c) = static_cast<int>(*v.asInt()); },
            [ref](const Config &c) { return std::to_string(ref(const_cast<Config &>(c))); }};
}
template <typename F>
Field boolField(std::string name, F ref) {
    return {std::move(name), Field::Kind::Boolean, 0, 0, 0, [ref](Config &c, const Json &v) { ref(c) = *v.asBool(); },
            [ref](const Config &c) { return std::string(ref(const_cast<Config &>(c)) ? "true" : "false"); }};
}
template <typename F>
Field stringField(std::string name, size_t minLength, F ref) {
    return {std::move(name), Field::Kind::String, 0, 0, minLength,
            [ref](Config &c, const Json &v) { ref(c) = *v.asString(); },
            [ref](const Config &c) { return jsonQuote(ref(const_cast<Config &>(c))); }};
}

/// config.schema.json と同じ項目と範囲。順番は docs/config.md の表と同じ（書き出す順番になる）。
const std::vector<Section> &schema() {
    static const std::vector<Section> s = {
        {"candidates",
         {
             intField("page_size", 1, 9, [](Config &c) -> int & { return c.candidates.pageSize; }),
             intField("max_pages", 1, 20, [](Config &c) -> int & { return c.candidates.maxPages; }),
             boolField("select_with_digits", [](Config &c) -> bool & { return c.candidates.selectWithDigits; }),
         }},
        {"live_conversion",
         {
             boolField("enabled", [](Config &c) -> bool & { return c.liveConversion.enabled; }),
         }},
        {"context",
         {
             boolField("enabled", [](Config &c) -> bool & { return c.context.enabled; }),
             intField("max_chars", 0, 256, [](Config &c) -> int & { return c.context.maxChars; }),
         }},
        {"timeout",
         {
             intField("full_ms", 50, 5000, [](Config &c) -> int & { return c.timeout.fullMs; }),
         }},
        {"zenzai",
         {
             boolField("enabled", [](Config &c) -> bool & { return c.zenzai.enabled; }),
             stringField("model_path", 0, [](Config &c) -> std::string & { return c.zenzai.modelPath; }),
             intField("inference_limit", 1, 1000, [](Config &c) -> int & { return c.zenzai.inferenceLimit; }),
             stringField("device", 1, [](Config &c) -> std::string & { return c.zenzai.device; }),
         }},
        {"learning",
         {
             boolField("enabled", [](Config &c) -> bool & { return c.learning.enabled; }),
             intField("save_every_commits", 1, 1000, [](Config &c) -> int & { return c.learning.saveEveryCommits; }),
             intField("save_idle_seconds", 5, 3600, [](Config &c) -> int & { return c.learning.saveIdleSeconds; }),
         }},
        {"fallback",
         {
             intField("max_crashes", 1, 100, [](Config &c) -> int & { return c.fallback.maxCrashes; }),
             intField("window_seconds", 10, 86400, [](Config &c) -> int & { return c.fallback.windowSeconds; }),
         }},
    };
    return s;
}

}  // namespace

ConfigParseResult parseConfig(std::string_view bytes) {
    ConfigParseResult result;
    auto root = Json::parse(bytes);
    if (!root) {
        result.errors.push_back({"", "JSON として読めない"});
        return result;
    }
    const auto *top = root->asObject();
    if (!top) {
        result.errors.push_back({"", "オブジェクトでなければならない"});
        return result;
    }
    Config config;
    if (const Json *v = root->get("version")) {
        if (v->asInt() != 1) result.errors.push_back({"/version", "1 でなければならない"});
    }
    std::set<std::string> sectionNames = {"version"};
    for (const auto &s : schema()) sectionNames.insert(s.name);
    for (const auto &[k, _] : *top) {
        if (!sectionNames.contains(k)) result.unknownKeys.push_back("/" + k);
    }
    for (const auto &section : schema()) {
        const Json *value = root->get(section.name);
        if (!value) continue;
        const auto *obj = value->asObject();
        if (!obj) {
            result.errors.push_back({"/" + section.name, "オブジェクトでなければならない"});
            continue;
        }
        std::set<std::string> names;
        for (const auto &f : section.fields) names.insert(f.name);
        for (const auto &[k, _] : *obj) {
            if (!names.contains(k)) result.unknownKeys.push_back("/" + section.name + "/" + k);
        }
        for (const auto &f : section.fields) {
            const Json *v = value->get(f.name);
            if (!v) continue;
            std::string path = "/" + section.name + "/" + f.name;
            switch (f.kind) {
            case Field::Kind::Integer: {
                auto i = v->asInt();
                if (i && *i >= f.min && *i <= f.max) {
                    f.assign(config, *v);
                } else {
                    result.errors.push_back(
                        {path, std::to_string(f.min) + "〜" + std::to_string(f.max) + " の整数でなければならない"});
                }
                break;
            }
            case Field::Kind::Boolean:
                if (v->asBool()) {
                    f.assign(config, *v);
                } else {
                    result.errors.push_back({path, "true か false でなければならない"});
                }
                break;
            case Field::Kind::String: {
                const auto *s = v->asString();
                if (s && decodeUtf8(*s)->size() >= f.minLength) {
                    f.assign(config, *v);
                } else {
                    result.errors.push_back(
                        {path, f.minLength > 0 ? "空でない文字列でなければならない" : "文字列でなければならない"});
                }
                break;
            }
            }
        }
    }
    std::sort(result.unknownKeys.begin(), result.unknownKeys.end());
    if (result.errors.empty()) result.config = config;
    return result;
}

ConfigParseResult loadConfig(const std::string &path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && !ec) {
        ConfigParseResult r;
        r.config = Config{};
        return r;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ConfigParseResult r;
        r.errors.push_back({"", std::string("読めない: ") + std::strerror(errno)});
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseConfig(ss.str());
}

std::string serializeConfig(const Config &config) {
    std::string out = "{\n  \"version\": " + std::to_string(config.version);
    for (const auto &section : schema()) {
        out += ",\n  " + jsonQuote(section.name) + ": {";
        bool first = true;
        for (const auto &f : section.fields) {
            out += first ? "\n" : ",\n";
            first = false;
            out += "    " + jsonQuote(f.name) + ": " + f.dump(config);
        }
        out += "\n  }";
    }
    out += "\n}\n";
    return out;
}

std::optional<std::string> saveConfig(const std::string &path, const Config &config) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return "ディレクトリを作れない: " + ec.message();
    }
    // 同じディレクトリの一時ファイルに書いてから rename する（docs/config.md「反映の経路」）。
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return std::string("一時ファイルを作れない: ") + std::strerror(errno);
    std::string bytes = serializeConfig(config);
    const char *p = bytes.data();
    size_t left = bytes.size();
    std::optional<std::string> error;
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            error = std::string("書けない: ") + std::strerror(errno);
            break;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    if (!error && ::fsync(fd) != 0) error = std::string("書けない: ") + std::strerror(errno);
    if (::close(fd) != 0 && !error) error = std::string("書けない: ") + std::strerror(errno);
    if (!error && ::rename(tmp.c_str(), path.c_str()) != 0) error = std::string("置き換えられない: ") + std::strerror(errno);
    if (error) ::unlink(tmp.c_str());
    return error;
}

std::string configPath() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && xdg[0] == '/') {
        base = xdg;
    } else {
        const char *home = std::getenv("HOME");
        base = std::string(home ? home : "") + "/.config";
    }
    return base + "/pazkey/config.json";
}

}  // namespace pazkey
