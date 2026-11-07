#include <gtest/gtest.h>

#include "json.h"
#include "protocol.h"

using namespace pazkey;

TEST(Json, ParsesAndRejects) {
    EXPECT_TRUE(Json::parse(R"({"a":[1,2.5,-0,1e3,true,null,"x"]})"));
    EXPECT_FALSE(Json::parse("{\"a\":1,}"));
    EXPECT_FALSE(Json::parse("\xEF\xBB\xBF{}"));
    EXPECT_FALSE(Json::parse("NaN"));
    EXPECT_FALSE(Json::parse("01"));
    EXPECT_FALSE(Json::parse(R"("\ud800")"));
    EXPECT_FALSE(Json::parse("\"a\tb\""));
    EXPECT_FALSE(Json::parse("{} {}"));
    EXPECT_EQ(*Json::parse(R"("😀")")->asString(), "😀");
}

TEST(Json, Integers) {
    EXPECT_EQ(Json::parse("5.0")->asInt(), 5);
    EXPECT_EQ(Json::parse("5e0")->asInt(), 5);
    EXPECT_FALSE(Json::parse("5.5")->asInt());
    EXPECT_FALSE(Json::parse("1e400")->asInt());
    EXPECT_FALSE(Json::parse("true")->asInt());
}

TEST(Json, DuplicateKeyLastWins) { EXPECT_EQ(Json::parse(R"({"a":1,"a":2})")->get("a")->asInt(), 2); }

TEST(Json, QuoteEscapesControlCharacters) {
    EXPECT_EQ(jsonQuote("a\"b\\c\nd\x01"), R"("a\"b\\c\nd\u0001")");
    EXPECT_EQ(jsonQuote("漢字"), "\"漢字\"");
}

TEST(Protocol, Messages) {
    EXPECT_EQ(convertMessage(1, 42, ConvertKind::Full, "きかい", 3, "新しい\n"),
              "{\"type\":\"convert\",\"session\":1,\"seq\":42,\"kind\":\"full\",\"reading\":\"きかい\",\"cursor\":3,"
              "\"left_context\":\"新しい\\n\"}\n");
    auto parsed = Json::parse(helloMessage("0.1.0"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->get("protocol")->asInt(), 1);
}

TEST(Protocol, ParseResult) {
    auto in = parseIncoming(
        R"({"type":"result","session":1,"seq":3,"candidates":[{"text":"機械","consumed":3},{"text":"機会を","consumed":4}],"elapsed_ms":9})");
    ASSERT_TRUE(in);
    EXPECT_EQ(in->type, "result");
    EXPECT_EQ(in->seq, 3);
    ASSERT_EQ(in->candidates.size(), 2u);
    EXPECT_EQ(in->candidates[1].text, "機会を");
    EXPECT_EQ(in->candidates[1].consumed, 4u);
    EXPECT_FALSE(parseIncoming(R"({"type":"result","candidates":[{"text":1,"consumed":1}]})"));
    EXPECT_FALSE(parseIncoming(R"({"session":1})"));
}
