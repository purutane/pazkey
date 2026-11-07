#include <gtest/gtest.h>

#include "utf8.h"

using namespace pazkey;

TEST(Utf8, DecodeCountsCodepoints) {
    // 絵文字（4バイト）、結合文字（が = か + ゛）、ZWJ でつないだ絵文字。
    auto d = decodeUtf8("a😀が👨‍👩‍👧");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->size(), 1u + 1 + 2 + 5);
}

TEST(Utf8, RejectsInvalid) {
    EXPECT_FALSE(decodeUtf8("\xC0\x80"));          // 過長
    EXPECT_FALSE(decodeUtf8("\xED\xA0\x80"));      // サロゲート
    EXPECT_FALSE(decodeUtf8("\xF4\x90\x80\x80"));  // 範囲外
    EXPECT_FALSE(decodeUtf8("\xE3\x81"));          // 途中で切れている
    EXPECT_FALSE(decodeUtf8("\x80"));
}

TEST(Utf8, LastCodepointsDoesNotSplitCharacters) {
    EXPECT_EQ(lastCodepoints("あいう😀え", 2), "😀え");
    EXPECT_EQ(lastCodepoints("あいう", 10), "あいう");
    EXPECT_EQ(lastCodepoints("あいう", 0), "");
    // 結合文字の途中で切れることはある（単位はコードポイント）が、UTF-8 としては正しい。
    auto s = lastCodepoints("が", 1);
    EXPECT_EQ(s, "゙");
    EXPECT_TRUE(decodeUtf8(s));
}

TEST(Utf8, Katakana) {
    EXPECT_EQ(toKatakana(U"きゃっと、ゔぁー"), U"キャット、ヴァー");
    EXPECT_EQ(toKatakana(U"abc"), U"abc");
}

TEST(Utf8, HalfKatakana) {
    EXPECT_EQ(toHalfKatakana(U"がっこう"), U"ｶﾞｯｺｳ");
    EXPECT_EQ(toHalfKatakana(U"ぱーてぃー。"), U"ﾊﾟｰﾃｨｰ｡");
    EXPECT_EQ(toHalfKatakana(U"ゔ"), U"ｳﾞ");
    EXPECT_EQ(toHalfKatakana(U"x1"), U"x1");
}
