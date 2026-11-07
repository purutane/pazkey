// 設定（docs/config.md）。検証の規則はサーバーの Config.swift と config.schema.json に合わせる。
// 片方を変えたら、すべてを直す。tests/config/check.py が3者の判定の一致を確かめる。
#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace pazkey {

struct Config {
    int version = 1;
    struct {
        int pageSize = 9;
        int maxPages = 5;
        bool selectWithDigits = true;
    } candidates;
    struct {
        bool enabled = false;
    } liveConversion;
    struct {
        bool enabled = true;
        int maxChars = 64;
    } context;
    struct {
        int fullMs = 1000;
    } timeout;
    struct {
        bool enabled = true;
        std::string modelPath;
        int inferenceLimit = 10;
        std::string device = "auto";
    } zenzai;
    struct {
        bool enabled = true;
        int saveEveryCommits = 20;
        int saveIdleSeconds = 60;
    } learning;
    struct {
        int maxCrashes = 3;
        int windowSeconds = 300;
    } fallback;
};

struct ConfigError {
    std::string path;  // JSON Pointer（例: /zenzai/inference_limit）
    std::string message;
    std::string describe() const { return (path.empty() ? "/" : path) + ": " + message; }
};

struct ConfigParseResult {
    std::optional<Config> config;  // 1つでも誤りがあれば空（ファイル全体を不正とみなす）
    std::vector<ConfigError> errors;
    std::vector<std::string> unknownKeys;  // 無視したキー
};

ConfigParseResult parseConfig(std::string_view bytes);

/// ファイルから読む。ファイルがなければ既定値（誤りなし）。
ConfigParseResult loadConfig(const std::string &path);

/// config.json の形に書き出す。インデントは2文字、キーの順番は docs/config.md の表のとおり。
/// 知らないキーは持っていないので書かない。
std::string serializeConfig(const Config &config);

/// 一時ファイルに書いてから rename で置き換える。ディレクトリがなければ作る。失敗したらその理由を返す。
std::optional<std::string> saveConfig(const std::string &path, const Config &config);

/// `$XDG_CONFIG_HOME/pazkey/config.json`（既定は `~/.config/pazkey/config.json`）。
std::string configPath();

}  // namespace pazkey
