// 設定画面とのやりとり（docs/frontend.md「設定画面」）。fcitx5 の RawConfig を経由して値が保たれるかを確かめる。
#include <gtest/gtest.h>

#include <fcitx-config/rawconfig.h>

#include "fcitx_config.h"

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
    c.zenzai.modelPath = "/home/あ/zenz.gguf";
    c.zenzai.inferenceLimit = 5;
    c.zenzai.device = "cpu";
    c.learning.enabled = false;
    c.learning.saveEveryCommits = 2;
    c.learning.saveIdleSeconds = 10;
    c.fallback.maxCrashes = 4;
    c.fallback.windowSeconds = 600;
    return c;
}

}  // namespace

TEST(FcitxConfigTest, DefaultsMatchConfig) {
    PazkeyFcitxConfig screen;
    EXPECT_EQ(serializeConfig(fromFcitxConfig(screen, Config{})), serializeConfig(Config{}));
}

TEST(FcitxConfigTest, RoundTripThroughRawConfig) {
    PazkeyFcitxConfig screen;
    toFcitxConfig(sample(), screen);
    fcitx::RawConfig raw;
    screen.save(raw);

    PazkeyFcitxConfig received;
    received.load(raw, true);
    EXPECT_EQ(serializeConfig(fromFcitxConfig(received, Config{})), [] {
        Config expected = sample();
        expected.zenzai.device = "auto";  // 画面に出さない項目は base のまま
        expected.fallback = Config{}.fallback;
        return serializeConfig(expected);
    }());
}

TEST(FcitxConfigTest, HiddenItemsComeFromBase) {
    PazkeyFcitxConfig screen;
    Config c = fromFcitxConfig(screen, sample());
    EXPECT_EQ(c.zenzai.device, "cpu");
    EXPECT_EQ(c.fallback.maxCrashes, 4);
    EXPECT_EQ(c.fallback.windowSeconds, 600);
}

TEST(FcitxConfigTest, PartialRawKeepsCurrentValues) {
    PazkeyFcitxConfig screen;
    toFcitxConfig(sample(), screen);
    fcitx::RawConfig raw;
    raw.setValueByPath("candidates/page_size", "3");
    screen.load(raw, true);
    Config c = fromFcitxConfig(screen, sample());
    EXPECT_EQ(c.candidates.pageSize, 3);
    EXPECT_EQ(c.context.maxChars, 100);
}

TEST(FcitxConfigTest, OutOfRangeIsNotAccepted) {
    PazkeyFcitxConfig screen;
    toFcitxConfig(sample(), screen);
    fcitx::RawConfig raw;
    raw.setValueByPath("candidates/page_size", "99");
    raw.setValueByPath("context/max_chars", "-1");
    screen.load(raw, true);
    Config c = fromFcitxConfig(screen, sample());
    // 範囲外の値は採らない。どちらに落ちても、config.json に範囲外の値は書かない。
    EXPECT_GE(c.candidates.pageSize, 1);
    EXPECT_LE(c.candidates.pageSize, 9);
    EXPECT_GE(c.context.maxChars, 0);
    EXPECT_TRUE(parseConfig(serializeConfig(c)).config);
}

TEST(FcitxConfigTest, DescriptionHasGroups) {
    PazkeyFcitxConfig screen;
    fcitx::RawConfig desc;
    screen.dumpDescription(desc);
    EXPECT_TRUE(desc.get("PazkeyFcitxConfig/candidates"));
    EXPECT_TRUE(desc.get("zenzai$ZenzaiGroup/model_path"));
    // 画面に出さない項目は宣言しない
    EXPECT_FALSE(desc.get("zenzai$ZenzaiGroup/device"));
    // モデルを取得するボタン
    auto button = desc.get("zenzai$ZenzaiGroup/download_model");
    ASSERT_TRUE(button);
    EXPECT_EQ(button->valueByPath("Type") ? *button->valueByPath("Type") : "", "External");
    EXPECT_EQ(button->valueByPath("External") ? *button->valueByPath("External") : "",
              "systemctl --user start --no-block pazkey-fetch-model.service");
    EXPECT_TRUE(button->valueByPath("Tooltip"));
}

TEST(FcitxConfigTest, StatusRowIsSingleChoice) {
    PazkeyFcitxConfig screen;
    setZenzaiStatus(screen, "使用中");
    fcitx::RawConfig desc;
    screen.dumpDescription(desc);
    auto row = desc.get("zenzai$ZenzaiGroup/status");
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->valueByPath("IsEnum"), "True");
    EXPECT_EQ(*row->valueByPath("Enum/0"), "使用中");
    EXPECT_EQ(*row->valueByPath("EnumI18n/0"), "使用中");
    EXPECT_FALSE(row->valueByPath("Enum/1"));
    EXPECT_EQ(*desc.valueByPath("PazkeyFcitxConfig/zenzai/Description"), "Zenzai");
    fcitx::RawConfig values;
    screen.save(values);
    EXPECT_EQ(*values.valueByPath("zenzai/status"), "使用中");
}

TEST(FcitxConfigTest, StatusRowIsNotWrittenToConfig) {
    PazkeyFcitxConfig screen;
    toFcitxConfig(Config{}, screen);
    fcitx::RawConfig raw;
    raw.setValueByPath("zenzai/status", "something");
    screen.load(raw, true);
    EXPECT_EQ(serializeConfig(fromFcitxConfig(screen, Config{})), serializeConfig(Config{}));
}

TEST(FcitxConfigTest, ZenzaiRowsInOrder) {
    PazkeyFcitxConfig screen;
    fcitx::RawConfig desc;
    screen.dumpDescription(desc);
    std::vector<std::string> names;
    desc.get("zenzai$ZenzaiGroup")->visitSubItems([&](fcitx::RawConfig &, const std::string &name) {
        names.push_back(name);
        return true;
    });
    EXPECT_EQ(names, (std::vector<std::string>{"status", "enabled", "download_model", "model_path", "inference_limit"}));
}

TEST(FcitxConfigTest, ButtonIsNotSaved) {
    PazkeyFcitxConfig screen;
    fcitx::RawConfig raw;
    screen.save(raw);
    EXPECT_FALSE(raw.get("zenzai/download_model"));
}
