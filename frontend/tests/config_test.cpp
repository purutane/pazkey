// 設定の書き出しと保存（docs/config.md「反映の経路」）。判定そのものは tests/config/check.py が確かめる。
#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"

using namespace pazkey;

namespace {

Config sample() {
    Config c;
    c.candidates.pageSize = 7;
    c.candidates.maxPages = 3;
    c.candidates.selectWithDigits = false;
    c.liveConversion.enabled = true;
    c.context.enabled = false;
    c.context.maxChars = 100;
    c.timeout.fullMs = 800;
    c.zenzai.enabled = false;
    c.zenzai.modelPath = "/home/あ/\"zenz\"\\x.gguf";
    c.zenzai.inferenceLimit = 5;
    c.zenzai.device = "cpu";
    c.learning.enabled = false;
    c.learning.saveEveryCommits = 2;
    c.learning.saveIdleSeconds = 10;
    c.fallback.maxCrashes = 4;
    c.fallback.windowSeconds = 600;
    return c;
}

std::string readFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class TempDir {
public:
    TempDir() {
        path = std::filesystem::temp_directory_path() / ("pazkey-config-test-" + std::to_string(::getpid()));
        std::filesystem::remove_all(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

}  // namespace

TEST(ConfigWriteTest, DefaultsInTableOrder) {
    EXPECT_EQ(serializeConfig(Config{}),
              "{\n"
              "  \"version\": 1,\n"
              "  \"candidates\": {\n"
              "    \"page_size\": 9,\n"
              "    \"max_pages\": 5,\n"
              "    \"select_with_digits\": true\n"
              "  },\n"
              "  \"live_conversion\": {\n"
              "    \"enabled\": false\n"
              "  },\n"
              "  \"context\": {\n"
              "    \"enabled\": true,\n"
              "    \"max_chars\": 64\n"
              "  },\n"
              "  \"timeout\": {\n"
              "    \"full_ms\": 1000\n"
              "  },\n"
              "  \"zenzai\": {\n"
              "    \"enabled\": true,\n"
              "    \"model_path\": \"\",\n"
              "    \"inference_limit\": 10,\n"
              "    \"device\": \"auto\"\n"
              "  },\n"
              "  \"learning\": {\n"
              "    \"enabled\": true,\n"
              "    \"save_every_commits\": 20,\n"
              "    \"save_idle_seconds\": 60\n"
              "  },\n"
              "  \"fallback\": {\n"
              "    \"max_crashes\": 3,\n"
              "    \"window_seconds\": 300\n"
              "  }\n"
              "}\n");
}

TEST(ConfigWriteTest, RoundTrip) {
    auto r = parseConfig(serializeConfig(sample()));
    ASSERT_TRUE(r.config) << (r.errors.empty() ? "" : r.errors[0].describe());
    EXPECT_TRUE(r.unknownKeys.empty());
    EXPECT_EQ(serializeConfig(*r.config), serializeConfig(sample()));
}

TEST(ConfigWriteTest, SaveCreatesDirectoryAndLeavesNoTemporaryFile) {
    TempDir dir;
    auto path = dir.path / "pazkey" / "config.json";
    ASSERT_EQ(saveConfig(path.string(), sample()), std::nullopt);
    EXPECT_EQ(readFile(path), serializeConfig(sample()));
    size_t files = 0;
    for ([[maybe_unused]] const auto &e : std::filesystem::directory_iterator(path.parent_path())) ++files;
    EXPECT_EQ(files, 1u);
}

TEST(ConfigWriteTest, SaveReplacesInvalidFileAndDropsUnknownKeys) {
    TempDir dir;
    auto path = dir.path / "config.json";
    std::filesystem::create_directories(dir.path);
    std::ofstream(path) << "{\"candidates\": {\"page_size\": 99}, \"unknown\": 1,";
    ASSERT_FALSE(loadConfig(path.string()).config);
    ASSERT_EQ(saveConfig(path.string(), sample()), std::nullopt);
    auto r = loadConfig(path.string());
    ASSERT_TRUE(r.config);
    EXPECT_TRUE(r.unknownKeys.empty());
    EXPECT_EQ(r.config->candidates.pageSize, 7);
}

TEST(ConfigWriteTest, SaveReportsFailure) {
    TempDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(dir.path / "file") << "x";
    // 親が通常のファイルなのでディレクトリを作れない
    EXPECT_NE(saveConfig((dir.path / "file" / "config.json").string(), sample()), std::nullopt);
}
