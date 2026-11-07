// fcitx5-configtool に見せる設定の項目（docs/config.md「fcitx5-configtool での表示」）。
// 正は config.json（Config）であり、これは画面とのやりとりのための写しにすぎない。
// fcitx5 側の ini ファイルには保存しない（docs/frontend.md「設定画面」）。
#pragma once

#include <string>

#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>

#include "config.h"

namespace pazkey {

namespace fcitx_config_detail {
inline const Config kDefaults{};
using ModelPathOption = fcitx::Option<std::string, fcitx::NoConstrain<std::string>,
                                      fcitx::DefaultMarshaller<std::string>, fcitx::ToolTipAnnotation>;

/// 外部のコマンドを起動するボタンに、ツールチップを付けたもの。
class ExternalOptionWithToolTip : public fcitx::ExternalOption {
public:
    ExternalOptionWithToolTip(fcitx::Configuration *parent, std::string path, std::string description,
                              std::string uri, std::string tooltip)
        : ExternalOption(parent, std::move(path), std::move(description), std::move(uri)),
          tooltip_(std::move(tooltip)) {}
    void dumpDescription(fcitx::RawConfig &config) const override {
        ExternalOption::dumpDescription(config);
        config.setValueByPath("Tooltip", tooltip_);
    }

private:
    std::string tooltip_;
};

/// 状態の行に出す文言。設定画面を開くたびに setZenzaiStatus が差し替える（docs/frontend.md「設定画面」）。
/// サブグループの説明は値ではなく既定のインスタンスから書き出されるので、注釈の中には持てない。
/// 設定画面とのやりとりは fcitx5 のメインスレッドだけで起きる。
inline std::string &currentStatusText() {
    static std::string text;
    return text;
}

/// 状態の行を、書き換えられない表示にするための注釈。選択肢が今の状態1つだけのドロップダウンにする。
struct StatusAnnotation {
    bool skipDescription() { return false; }
    bool skipSave() { return false; }
    void dumpDescription(fcitx::RawConfig &config) const {
        config.setValueByPath("IsEnum", "True");
        config.setValueByPath("Enum/0", currentStatusText());
        config.setValueByPath("EnumI18n/0", currentStatusText());
    }
};
using StatusOption = fcitx::Option<std::string, fcitx::NoConstrain<std::string>, fcitx::DefaultMarshaller<std::string>,
                                   StatusAnnotation>;

/// 設定画面のボタンが起動するもの（docs/server.md「Zenzai のモデルの取得」）。
inline constexpr const char *kFetchModelCommand = "systemctl --user start --no-block pazkey-fetch-model.service";
}  // namespace fcitx_config_detail

FCITX_CONFIGURATION(CandidatesGroup,
    fcitx::Option<int, fcitx::IntConstrain> pageSize{
        this, "page_size", "1ページの候補数", fcitx_config_detail::kDefaults.candidates.pageSize, {1, 9}};
    fcitx::Option<bool> selectWithDigits{
        this, "select_with_digits", "数字キーで候補を確定する",
        fcitx_config_detail::kDefaults.candidates.selectWithDigits};);

FCITX_CONFIGURATION(InputGroup,
    fcitx::Option<bool> liveConversion{
        this, "live_conversion", "ライブ変換", fcitx_config_detail::kDefaults.liveConversion.enabled};);

FCITX_CONFIGURATION(ContextGroup,
    fcitx::Option<bool> enabled{
        this, "enabled", "左文脈を送る", fcitx_config_detail::kDefaults.context.enabled};
    fcitx::Option<int, fcitx::IntConstrain> maxChars{
        this, "max_chars", "送る左文脈の最大文字数", fcitx_config_detail::kDefaults.context.maxChars, {0, 256}};);

FCITX_CONFIGURATION(ZenzaiGroup,
    // 状態の行は表示だけで、config.json には書かない（fromFcitxConfig が読まない）。
    fcitx_config_detail::StatusOption status{this, "status", "状態（設定を変えたら、画面を開き直すと更新される）"};
    fcitx::Option<bool> enabled{this, "enabled", "Zenzai を使う", fcitx_config_detail::kDefaults.zenzai.enabled};
    fcitx_config_detail::ExternalOptionWithToolTip downloadModel{
        this, "download_model", "モデルをダウンロード（約 74MB）", fcitx_config_detail::kFetchModelCommand,
        "Hugging Face の Miwa-Keita/zenz-v3.2-small-gguf（Apache-2.0）の決まった版を取得し、"
        "ハッシュを確かめてから ~/.local/share/pazkey/zenz.gguf に置く。結果は通知で知らせる"};
    fcitx_config_detail::ModelPathOption modelPath{
        this, "model_path", "自分で用意したモデル（空ならダウンロードしたものを使う）",
        fcitx_config_detail::kDefaults.zenzai.modelPath, {}, {},
        fcitx::ToolTipAnnotation("gguf ファイルのパスを書く。ふだんは空のままでよい")};
    fcitx::Option<int, fcitx::IntConstrain> inferenceLimit{
        this, "inference_limit", "1回の変換での推論回数の上限", fcitx_config_detail::kDefaults.zenzai.inferenceLimit,
        {1, 1000}};);

FCITX_CONFIGURATION(LearningGroup,
    fcitx::Option<bool> enabled{this, "enabled", "学習する", fcitx_config_detail::kDefaults.learning.enabled};);

FCITX_CONFIGURATION(AdvancedGroup,
    fcitx::Option<int, fcitx::IntConstrain> maxPages{
        this, "max_pages", "取得する候補の最大ページ数", fcitx_config_detail::kDefaults.candidates.maxPages, {1, 20}};
    fcitx::Option<int, fcitx::IntConstrain> fullMs{
        this, "full_ms", "変換キーを押してから代用の候補を出すまでの時間（ミリ秒）",
        fcitx_config_detail::kDefaults.timeout.fullMs, {50, 5000}};
    fcitx::Option<int, fcitx::IntConstrain> saveEveryCommits{
        this, "save_every_commits", "学習を保存する確定の回数", fcitx_config_detail::kDefaults.learning.saveEveryCommits,
        {1, 1000}};
    fcitx::Option<int, fcitx::IntConstrain> saveIdleSeconds{
        this, "save_idle_seconds", "最後の確定から学習を保存するまでの秒数",
        fcitx_config_detail::kDefaults.learning.saveIdleSeconds, {5, 3600}};);

FCITX_CONFIGURATION(PazkeyFcitxConfig,
    fcitx::Option<CandidatesGroup> candidates{this, "candidates", "候補"};
    fcitx::Option<InputGroup> input{this, "input", "入力"};
    fcitx::Option<ContextGroup> context{this, "context", "文脈"};
    fcitx::Option<ZenzaiGroup> zenzai{this, "zenzai", "Zenzai"};
    fcitx::Option<LearningGroup> learning{this, "learning", "学習"};
    fcitx::Option<AdvancedGroup> advanced{this, "advanced", "詳細"};);

/// 画面に見せる値を Config から写す。
void toFcitxConfig(const Config &from, PazkeyFcitxConfig &to);

/// Zenzai のグループの「状態」の行に出す。
void setZenzaiStatus(PazkeyFcitxConfig &to, const std::string &status);

/// 画面の値を base に重ねた Config を返す。画面に出さない項目（fallback.*、zenzai.device）は base のまま。
Config fromFcitxConfig(const PazkeyFcitxConfig &from, Config base);

}  // namespace pazkey
