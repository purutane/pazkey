// Zenzai の状態の通知（docs/frontend.md「Zenzai の状態を知らせる」）。
#include <gtest/gtest.h>

#include "protocol.h"
#include "zenzai_notice.h"

using namespace pazkey;

TEST(ZenzaiNoticeTest, NotifiesOnceForSameState) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("enabled"), std::nullopt);
    EXPECT_EQ(n.update("unavailable"), "unavailable");
    EXPECT_EQ(n.update("unavailable"), std::nullopt);  // 再接続の hello や、次の config_changed
}

TEST(ZenzaiNoticeTest, FirstHelloCanNotify) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("fallback"), "fallback");
}

TEST(ZenzaiNoticeTest, ChangeBetweenBadStatesNotifies) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("fallback"), "fallback");
    EXPECT_EQ(n.update("unavailable"), "unavailable");
}

TEST(ZenzaiNoticeTest, GoodStateResets) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("fallback"), "fallback");
    EXPECT_EQ(n.update("enabled"), std::nullopt);
    EXPECT_EQ(n.update("fallback"), "fallback");
    EXPECT_EQ(n.update("disabled"), std::nullopt);
    EXPECT_EQ(n.update("fallback"), "fallback");
}

TEST(ZenzaiNoticeTest, EmptyAndUnknownAreIgnored) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("unavailable"), "unavailable");
    EXPECT_EQ(n.update(""), std::nullopt);
    EXPECT_EQ(n.update("something"), std::nullopt);
    EXPECT_EQ(n.update("unavailable"), std::nullopt);
}

TEST(ZenzaiNoticeTest, ParsedFromMessages) {
    auto hello = parseIncoming(R"({"type":"hello","protocol":1,"zenzai":"fallback","config_generation":1})");
    ASSERT_TRUE(hello);
    EXPECT_EQ(hello->zenzai, "fallback");
    auto changed = parseIncoming(R"({"type":"config_changed","config_generation":2,"zenzai":"enabled"})");
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->zenzai, "enabled");
    auto old = parseIncoming(R"({"type":"config_changed","config_generation":2})");
    ASSERT_TRUE(old);
    EXPECT_EQ(old->zenzai, "");
}

TEST(ZenzaiNoticeTest, UnsupportedNotifiesOnce) {
    ZenzaiNotice n;
    EXPECT_EQ(n.update("unsupported"), "unsupported");
    EXPECT_EQ(n.update("unsupported"), std::nullopt);
    EXPECT_EQ(n.update("disabled"), std::nullopt);
    EXPECT_EQ(n.update("unsupported"), "unsupported");
}
