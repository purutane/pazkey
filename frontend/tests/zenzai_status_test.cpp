// 設定画面に出す Zenzai の状態（docs/frontend.md「設定画面」の表）。
#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>

#include "zenzai_status.h"

using namespace pazkey;

TEST(ZenzaiStatusTest, Table) {
    EXPECT_EQ(zenzaiStatusText({true, true, "enabled", true}), "ダウンロード中");
    EXPECT_EQ(zenzaiStatusText({true, false, "", false}), "ダウンロード中");
    EXPECT_EQ(zenzaiStatusText({false, false, "enabled", true}), "変換サーバーに接続していません");
    EXPECT_EQ(zenzaiStatusText({false, true, "enabled", true}), "使用中");
    EXPECT_EQ(zenzaiStatusText({false, true, "enabled", false}), "使用中");  // 既定の場所: 読み込んだあとで消された
    EXPECT_EQ(zenzaiStatusText({false, true, "enabled", true, true}), "使用中");
    EXPECT_EQ(zenzaiStatusText({false, true, "enabled", false, true}),
              "使用中（指定したモデルがないため、前のモデルを使用）");
    EXPECT_EQ(zenzaiStatusText({false, true, "disabled", false}), "無効");
    EXPECT_EQ(zenzaiStatusText({false, true, "fallback", true}), "一時停止中");
    EXPECT_EQ(zenzaiStatusText({false, true, "unsupported", true}), "搭載の CPU をサポートしません");
    EXPECT_EQ(zenzaiStatusText({false, true, "unsupported", false}), "搭載の CPU をサポートしません");
    EXPECT_EQ(zenzaiStatusText({false, true, "unavailable", false}), "モデルがありません（ダウンロードできます）");
    EXPECT_EQ(zenzaiStatusText({false, true, "unavailable", true}), "モデルを読み込めません");
    EXPECT_EQ(zenzaiStatusText({false, true, "unavailable", false, true}), "指定したモデルが見つかりません");
    EXPECT_EQ(zenzaiStatusText({false, true, "unavailable", true, true}), "モデルを読み込めません");
}

class ZenzaiStatusPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char *old = std::getenv("XDG_DATA_HOME");
        if (old) saved_ = old;
        dir_ = std::filesystem::temp_directory_path() / ("pazkey-status-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir_ / "pazkey");
        ::setenv("XDG_DATA_HOME", dir_.c_str(), 1);
    }
    void TearDown() override {
        if (saved_.empty()) ::unsetenv("XDG_DATA_HOME"); else ::setenv("XDG_DATA_HOME", saved_.c_str(), 1);
        std::filesystem::remove_all(dir_);
    }
    std::filesystem::path dir_;
    std::string saved_;
};

TEST_F(ZenzaiStatusPathTest, DefaultAndEffectivePath) {
    EXPECT_EQ(defaultModelPath(), (dir_ / "pazkey/zenz.gguf").string());
    EXPECT_EQ(effectiveModelPath(""), defaultModelPath());
    EXPECT_EQ(effectiveModelPath("/x/y.gguf"), "/x/y.gguf");
}

TEST_F(ZenzaiStatusPathTest, FetchingFollowsLock) {
    EXPECT_FALSE(modelFetching());  // ロックのファイルがない
    std::string lock = defaultModelPath() + ".lock";
    int fd = ::open(lock.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(modelFetching());  // ファイルはあるがロックされていない
    // 別のプロセスがロックしている状態を作る（flock は同じプロセスの別の open どうしでも競合する）
    ASSERT_EQ(::flock(fd, LOCK_EX | LOCK_NB), 0);
    EXPECT_TRUE(modelFetching());
    ::close(fd);
    EXPECT_FALSE(modelFetching());
}
