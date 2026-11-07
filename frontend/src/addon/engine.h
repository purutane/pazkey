// fcitx5 の入力メソッド（docs/frontend.md「構成」）。
// 入力状態とキー処理は core の PazkeyState にあり、ここは fcitx5 とのつなぎだけを持つ。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "client.h"
#include "config.h"
#include "fcitx_config.h"
#include "state.h"
#include "zenzai_notice.h"

namespace pazkey {

/// fcitx5 のログの分類 `pazkey`。デバッグログは Debug の水準で書く（docs/frontend.md「デバッグログ」）。
FCITX_DECLARE_LOG_CATEGORY(pazkey_log);

class PazkeyEngine;

/// 入力欄1つ分。PazkeyState を持ち、StateHost として fcitx5 とサーバーへつなぐ。
class PazkeyContext final : public fcitx::InputContextProperty, public StateHost {
public:
    PazkeyContext(PazkeyEngine *engine, fcitx::InputContext *ic, int64_t session);
    ~PazkeyContext() override;

    PazkeyState &state() { return state_; }
    fcitx::InputContext *inputContext() const { return ic_; }
    /// 状態を画面へ反映する。前回と違う部分だけを送る。
    void render();

    // StateHost
    void commitString(const std::string &text) override;
    bool connected() const override;
    void send(const std::string &line) override;
    void startFullTimer(int ms) override;
    void cancelFullTimer() override;
    Surrounding surrounding() const override;
    bool isPassword() const override;
    const Config &config() const override;
    bool debugEnabled() const override;
    void debugLog(const std::string &line) override;

private:
    PazkeyEngine *engine_;
    fcitx::InputContext *ic_;
    PazkeyState state_;
    std::unique_ptr<fcitx::EventSourceTime> fullTimer_;
};

class PazkeyEngine final : public fcitx::InputMethodEngineV2 {
public:
    explicit PazkeyEngine(fcitx::Instance *instance);
    ~PazkeyEngine() override;

    void keyEvent(const fcitx::InputMethodEntry &entry, fcitx::KeyEvent &event) override;
    void activate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;

    // 設定画面（docs/frontend.md「設定画面」）。正は config.json で、fcitx5 側の ini ファイルは使わない。
    const fcitx::Configuration *getConfig() const override;
    void setConfig(const fcitx::RawConfig &raw) override;
    void reloadConfig() override { loadConfig(); }

    fcitx::Instance *instance() const { return instance_; }
    /// 接続。終了の途中では nullptr。
    ServerClient *client() { return client_.get(); }
    const Config &config() const { return config_; }
    void selectCandidate(fcitx::InputContext *ic, size_t index);

    void registerSession(int64_t session, PazkeyContext *ctx) { sessions_[session] = ctx; }
    void unregisterSession(int64_t session) { sessions_.erase(session); }

private:
    void loadConfig();
    void onConnected();
    void onDisconnected();
    void onMessage(const Incoming &msg);
    void armDisconnectedNotice();
    void notify(const std::string &summary, const std::string &body);
    void onZenzaiState(const std::string &state);

    fcitx::Instance *instance_;
    Config config_;
    mutable PazkeyFcitxConfig screenConfig_;  // getConfig で返す画面の写し
    std::string lastConfigError_;
    std::unordered_map<int64_t, PazkeyContext *> sessions_;
    int64_t nextSession_ = 1;
    std::unique_ptr<fcitx::EventSourceTime> noticeTimer_;
    bool disconnectedNoticeSent_ = false;
    ZenzaiNotice zenzaiNotice_;
    std::string zenzaiState_;  // サーバーから最後に受けた zenzai（設定画面の状態の表示に使う）
    std::unique_ptr<ServerClient> client_;
    // factory_ は最後に置く。先に壊れて、入力欄ごとの状態が client_ を使って close を送れるようにする。
    fcitx::FactoryFor<PazkeyContext> factory_;

public:
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
};

class PazkeyEngineFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new PazkeyEngine(manager->instance());
    }
};

}  // namespace pazkey
