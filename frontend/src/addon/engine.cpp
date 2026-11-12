#include "engine.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <notifications_public.h>

#include <filesystem>

#include "romaji.h"
#include "zenzai_status.h"

namespace pazkey {

FCITX_DEFINE_LOG_CATEGORY(pazkey_log, "pazkey");

/// デバッグログを1行書く。入力した文章は書かない（docs/frontend.md「デバッグログ」）。
#define PAZKEY_DEBUG() FCITX_LOGC(::pazkey::pazkey_log, Debug)

namespace {

/// サーバーにつながらない状態がこの時間続いたら、通知で知らせる（仮の値。docs/frontend.md）。
constexpr uint64_t kDisconnectedNoticeUsec = 30ULL * 1000 * 1000;

Key toKey(const fcitx::Key &key) {
    using T = Key::Type;
    const auto states = key.states();
    const bool ctrl = states.test(fcitx::KeyState::Ctrl);
    const bool alt = states.test(fcitx::KeyState::Alt) || states.test(fcitx::KeyState::Super) ||
                     states.test(fcitx::KeyState::Hyper) || states.test(fcitx::KeyState::Meta);
    const bool shift = states.test(fcitx::KeyState::Shift);
    const auto sym = key.sym();
    if (alt) return Key::of(T::Other);
    if (ctrl) {
        if ((sym == FcitxKey_h || sym == FcitxKey_H) && !shift) return Key::of(T::Backspace);
        // Mozc の MS-IME の表の Ctrl+Backspace と Ctrl+Enter（入力がないときはアプリへ渡る）。
        if (sym == FcitxKey_BackSpace && !shift) return Key::of(T::Backspace);
        if ((sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) && !shift) return Key::of(T::Enter);
        return Key::of(T::Other);
    }
    switch (sym) {
    case FcitxKey_BackSpace: return Key::of(T::Backspace);
    case FcitxKey_Delete:
    case FcitxKey_KP_Delete: return Key::of(T::Delete);
    case FcitxKey_Left:
    case FcitxKey_KP_Left: return Key::of(shift ? T::ShiftLeft : T::Left);
    case FcitxKey_Right:
    case FcitxKey_KP_Right: return Key::of(shift ? T::ShiftRight : T::Right);
    case FcitxKey_Home:
    case FcitxKey_KP_Home: return Key::of(T::Home);
    case FcitxKey_End:
    case FcitxKey_KP_End: return Key::of(T::End);
    case FcitxKey_Up:
    case FcitxKey_KP_Up: return Key::of(shift ? T::ShiftUp : T::Up);
    case FcitxKey_Down:
    case FcitxKey_KP_Down: return Key::of(shift ? T::ShiftDown : T::Down);
    case FcitxKey_Page_Up:
    case FcitxKey_KP_Page_Up: return Key::of(T::PageUp);
    case FcitxKey_Page_Down:
    case FcitxKey_KP_Page_Down: return Key::of(T::PageDown);
    case FcitxKey_Tab: return Key::of(shift ? T::ShiftTab : T::Tab);
    case FcitxKey_ISO_Left_Tab: return Key::of(T::ShiftTab);
    case FcitxKey_Return:
    case FcitxKey_KP_Enter: return Key::of(T::Enter);
    case FcitxKey_Escape: return Key::of(T::Escape);
    case FcitxKey_space:
    case FcitxKey_KP_Space: return Key::of(shift ? T::ShiftSpace : T::Space);
    case FcitxKey_Henkan_Mode: return Key::of(shift ? T::ShiftConvert : T::Convert);
    case FcitxKey_F6: return Key::of(T::F6);
    case FcitxKey_F7: return Key::of(T::F7);
    case FcitxKey_F8: return Key::of(T::F8);
    case FcitxKey_F9: return Key::of(T::F9);
    case FcitxKey_F10: return Key::of(T::F10);
    // テンキーの文字（NumLock が有効なとき）。字種は docs/frontend.md「字種」。
    case FcitxKey_KP_0:
    case FcitxKey_KP_1:
    case FcitxKey_KP_2:
    case FcitxKey_KP_3:
    case FcitxKey_KP_4:
    case FcitxKey_KP_5:
    case FcitxKey_KP_6:
    case FcitxKey_KP_7:
    case FcitxKey_KP_8:
    case FcitxKey_KP_9:
    case FcitxKey_KP_Decimal:
    case FcitxKey_KP_Separator:
    case FcitxKey_KP_Add:
    case FcitxKey_KP_Subtract:
    case FcitxKey_KP_Multiply:
    case FcitxKey_KP_Divide:
    case FcitxKey_KP_Equal: {
        uint32_t u = fcitx::Key::keySymToUnicode(sym);
        if (isRomajiInputChar(u)) return Key::numpadChar(static_cast<char>(u));
        return Key::of(T::Other);
    }
    default: break;
    }
    uint32_t u = fcitx::Key::keySymToUnicode(sym);
    if (isRomajiInputChar(u)) return Key::character(static_cast<char>(u));
    return Key::of(T::Other);
}

class PazkeyCandidateWord : public fcitx::CandidateWord {
public:
    PazkeyCandidateWord(PazkeyEngine *engine, size_t index, const std::string &text)
        : fcitx::CandidateWord(fcitx::Text(text)), engine_(engine), index_(index) {}
    void select(fcitx::InputContext *ic) const override { engine_->selectCandidate(ic, index_); }

private:
    PazkeyEngine *engine_;
    size_t index_;
};

}  // namespace

// ---- PazkeyContext ----

PazkeyContext::PazkeyContext(PazkeyEngine *engine, fcitx::InputContext *ic, int64_t session)
    : engine_(engine), ic_(ic), state_(*this, session) {
    engine_->registerSession(session, this);
    // ここでは ic の仮想関数を呼ばない。fcitx5 は InputContext のコンストラクタの途中でこれを作るので、
    // 派生クラスがまだできておらず、frontendName() などで落ちる。
    PAZKEY_DEBUG() << "s=" << session << " created";
}

PazkeyContext::~PazkeyContext() {
    PAZKEY_DEBUG() << "s=" << state_.session() << " destroyed";
    state_.close();
    engine_->unregisterSession(state_.session());
}

void PazkeyContext::render() {
    RenderUpdate u = state_.render();
    if (!u.preeditChanged && !u.panelChanged) return;  // 変化がなければ何も送らない（不変条件2）

    auto &panel = ic_->inputPanel();
    const bool clientPreedit = ic_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    PAZKEY_DEBUG() << "s=" << state_.session() << " render preedit=" << (u.preeditChanged ? "sent" : "same")
                   << " panel=" << (u.panelChanged ? "sent" : "same") << " clientPreedit=" << clientPreedit
                   << " preeditSegments=" << u.preedit.segments.size() << " candidates=" << u.panel.candidates.size()
                   << " selected=" << u.panel.selected << " aux=" << !u.panel.aux.empty();
    if (u.preeditChanged) {
        fcitx::Text text;
        for (const auto &seg : u.preedit.segments) {
            text.append(seg.text, seg.highlight ? fcitx::TextFormatFlag::HighLight : fcitx::TextFormatFlag::Underline);
        }
        if (!u.preedit.empty()) text.setCursor(static_cast<int>(u.preedit.cursorBytes));
        if (clientPreedit) {
            panel.setClientPreedit(text);
            ic_->updatePreedit();
        } else {
            // preedit を表示できないアプリでは、fcitx5 の入力パネル側に表示する。
            panel.setPreedit(text);
        }
    }
    if (u.panelChanged || (u.preeditChanged && !clientPreedit)) {
        if (u.panel.candidates.empty()) {
            panel.setCandidateList(nullptr);
        } else {
            auto list = std::make_unique<fcitx::CommonCandidateList>();
            const int pageSize = u.panel.pageSize;
            list->setPageSize(pageSize);
            list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
            std::vector<std::string> labels;
            for (int i = 0; i < pageSize; ++i) labels.push_back(u.panel.digitLabels ? std::to_string(i + 1) + ". " : "");
            list->setLabels(labels);
            for (size_t i = 0; i < u.panel.candidates.size(); ++i) {
                list->append<PazkeyCandidateWord>(engine_, i, u.panel.candidates[i]);
            }
            list->setPage(u.panel.selected / pageSize);
            list->setGlobalCursorIndex(u.panel.selected);
            panel.setCandidateList(std::move(list));
        }
        panel.setAuxUp(fcitx::Text(u.panel.aux));
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
}

void PazkeyContext::commitString(const std::string &text) {
    if (text.empty()) return;  // 不変条件2。PazkeyState も空では呼ばないが、念のため
    ic_->commitString(text);
}

bool PazkeyContext::connected() const { return engine_->client() && engine_->client()->connected(); }

void PazkeyContext::send(const std::string &line) {
    if (auto *client = engine_->client()) client->send(line);
}

// タイマーは一度作ったら使い回す。期限のコールバックの中から cancelFullTimer が呼ばれるので、壊さない。
void PazkeyContext::startFullTimer(int ms) {
    uint64_t at = fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(ms) * 1000;
    if (!fullTimer_) {
        fullTimer_ = engine_->instance()->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, at, 1000, [this](fcitx::EventSourceTime *, uint64_t) {
                state_.onFullTimeout();
                render();
                return true;
            });
        return;
    }
    fullTimer_->setTime(at);
    fullTimer_->setOneShot();
}

void PazkeyContext::cancelFullTimer() {
    if (fullTimer_) fullTimer_->setEnabled(false);
}

Surrounding PazkeyContext::surrounding() const {
    Surrounding s;
    if (!ic_->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) return s;
    const auto &st = ic_->surroundingText();
    if (!st.isValid()) return s;
    s.valid = true;
    s.text = st.text();
    s.cursor = st.cursor();  // コードポイント数（fcitx5 の surroundingtext.cpp で確認）
    s.anchor = st.anchor();
    return s;
}

bool PazkeyContext::isPassword() const {
    auto flags = ic_->capabilityFlags();
    return flags.test(fcitx::CapabilityFlag::Password) || flags.test(fcitx::CapabilityFlag::Sensitive);
}

const Config &PazkeyContext::config() const { return engine_->config(); }

bool PazkeyContext::debugEnabled() const { return pazkey_log().checkLogLevel(fcitx::LogLevel::Debug); }

void PazkeyContext::debugLog(const std::string &line) {
    PAZKEY_DEBUG() << "s=" << state_.session() << " " << line;
}

// ---- PazkeyEngine ----

PazkeyEngine::PazkeyEngine(fcitx::Instance *instance)
    : instance_(instance),
      factory_([this](fcitx::InputContext &ic) { return new PazkeyContext(this, &ic, nextSession_++); }) {
    loadConfig();
    client_ = std::make_unique<ServerClient>(instance_->eventLoop(), ServerClient::defaultSocketPath(),
                                             ServerClient::Callbacks{
                                                 [this] { onConnected(); },
                                                 [this] { onDisconnected(); },
                                                 [this](const Incoming &msg) { onMessage(msg); },
                                             });
    instance_->inputContextManager().registerProperty("pazkeyState", &factory_);
    armDisconnectedNotice();
    client_->start();
}

PazkeyEngine::~PazkeyEngine() {
    // fcitx5 の終了時は、イベントループが終わってからアドオンが壊される。そのあとでタイマーや監視を
    // 設定し直すと例外になるので、入力欄ごとの状態（factory_）より先に接続を捨てる。close は送らない
    // （接続が切れれば、サーバーはそのセッションを破棄する）。
    client_.reset();
}

void PazkeyEngine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) {
    if (event.isRelease()) return;
    if (event.key().isModifier()) return;  // 修飾キーだけの押下はアプリへ渡す
    auto *ctx = event.inputContext()->propertyFor(&factory_);
    const Key key = toKey(event.key());
    const auto &st = ctx->state();
    const Mode before = st.mode();
    const bool used = ctx->state().keyEvent(key);
    if (used) event.filterAndAccept();
    // 文字キーは種類だけを書き、どの文字かは書かない（docs/frontend.md「デバッグログ」）。
    PAZKEY_DEBUG() << "s=" << st.session() << " key=" << keyTypeName(key.type) << " used=" << used
                   << " mode=" << modeName(before) << "->" << modeName(st.mode()) << " kana=" << st.kana().size()
                   << " tail=" << st.romajiTail().size() << " cursor=" << st.cursor()
                   << " candidates=" << st.candidates().size() << " selected=" << st.selected()
                   << " awaiting=" << st.awaiting();
    ctx->render();
}

void PazkeyEngine::activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    // 何もしない（Empty のまま。preedit の更新も送らない）。
    auto *ic = event.inputContext();
    PAZKEY_DEBUG() << "s=" << ic->propertyFor(&factory_)->state().session() << " activate program="
                   << (ic->program().empty() ? "?" : ic->program()) << " frontend=" << ic->frontendName();
}

void PazkeyEngine::deactivate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *ctx = ic->propertyFor(&factory_);
    PAZKEY_DEBUG() << "s=" << ctx->state().session() << " deactivate focusOut="
                   << (event.type() == fcitx::EventType::InputContextFocusOut)
                   << " clientPreedit=" << ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    if (event.type() == fcitx::EventType::InputContextFocusOut) {
        // フォーカスが外れたとき、fcitx5 はアプリの preedit を自分で確定する（instance.cpp の FocusOut の
        // ReservedFirst の処理）。ここで確定すると二重になるので、preedit をアプリに
        // 表示していた場合は確定しない。表示していなければ（入力パネル側に出していた場合）ここで確定する。
        bool clientPreedit = ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
        ctx->state().endInput(!clientPreedit);
        ctx->state().clearContext();
    } else {
        ctx->state().endInput(true);
    }
    ctx->render();
}

void PazkeyEngine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    // アプリからのリセット（クリックでカーソルが動いたときなど）。入力を捨てる。
    auto *ctx = event.inputContext()->propertyFor(&factory_);
    PAZKEY_DEBUG() << "s=" << ctx->state().session() << " reset by app";
    ctx->state().discardInput();
    ctx->render();
}

void PazkeyEngine::selectCandidate(fcitx::InputContext *ic, size_t index) {
    auto *ctx = ic->propertyFor(&factory_);
    PAZKEY_DEBUG() << "s=" << ctx->state().session() << " candidate clicked index=" << index;
    ctx->state().selectCandidate(index);
    ctx->render();
}

void PazkeyEngine::loadConfig() {
    auto result = pazkey::loadConfig(configPath());
    // 知らないキーは無視し、警告をログに残す（docs/config.md「読み込みと検証」）。
    for (const auto &key : result.unknownKeys) FCITX_WARN() << "pazkey: 設定: 知らないキー " << key << " を無視する";
    if (result.config) {
        config_ = *result.config;
        lastConfigError_.clear();
        return;
    }
    // 不正なら直前の正しい設定で動き続ける（不変条件5）。同じ誤りで何度も通知しない。
    std::string text;
    for (const auto &e : result.errors) text += e.describe() + "\n";
    FCITX_WARN() << "pazkey: 設定が不正なので、前の設定を使い続ける: " << text;
    if (text != lastConfigError_) {
        lastConfigError_ = text;
        notify("pazkey: 設定に誤りがあります", "直前の正しい設定のまま動いています。\n" + configPath() + "\n" + text);
    }
}

const fcitx::Configuration *PazkeyEngine::getConfig() const {
    toFcitxConfig(config_, screenConfig_);
    std::error_code ec;
    ZenzaiStatusInput status{modelFetching(), client_ && client_->connected(), zenzaiState_,
                             std::filesystem::exists(effectiveModelPath(config_.zenzai.modelPath), ec),
                             !config_.zenzai.modelPath.empty()};
    setZenzaiStatus(screenConfig_, zenzaiStatusText(status));
    return &screenConfig_;
}

void PazkeyEngine::setConfig(const fcitx::RawConfig &raw) {
    PazkeyFcitxConfig screen;
    toFcitxConfig(config_, screen);
    screen.load(raw, true);
    // 画面に出さない項目はメモリ上の設定のまま書く。config.json が不正でも、この内容で上書きする
    // （docs/config.md「反映の経路」）。
    Config next = fromFcitxConfig(screen, config_);
    if (auto error = saveConfig(configPath(), next)) {
        FCITX_WARN() << "pazkey: 設定を保存できない: " << *error;
        notify("pazkey: 設定を保存できませんでした", configPath() + "\n" + *error);
        return;
    }
    FCITX_INFO() << "pazkey: 設定画面から設定を保存した";
    // サーバーがつながっていなくても、フロントエンドの項目はすぐ効くように読み直す。
    loadConfig();
}

void PazkeyEngine::onConnected() {
    PAZKEY_DEBUG() << "connected";
    if (noticeTimer_) noticeTimer_->setEnabled(false);
    disconnectedNoticeSent_ = false;  // 次に切れたときに、また知らせられるようにする
}

void PazkeyEngine::onDisconnected() {
    PAZKEY_DEBUG() << "disconnected";
    // 結果を待っている入力欄には、すぐ代用の候補を出す（接続していない間と同じ扱い）。
    for (auto &[_, ctx] : sessions_) {
        if (ctx->state().awaiting()) {
            ctx->state().onFullTimeout();
            ctx->render();
        }
    }
    armDisconnectedNotice();
}

void PazkeyEngine::armDisconnectedNotice() {
    if (disconnectedNoticeSent_) return;
    uint64_t at = fcitx::now(CLOCK_MONOTONIC) + kDisconnectedNoticeUsec;
    if (!noticeTimer_) {
        noticeTimer_ = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, at, 1000000, [this](fcitx::EventSourceTime *, uint64_t) {
            if (!client_ || client_->connected() || disconnectedNoticeSent_) return true;
            disconnectedNoticeSent_ = true;
            notify("pazkey: 変換サーバーに接続できません",
                   "かな入力はできますが、変換はできません。\n"
                   "状態の確認: systemctl --user status pazkey-server.socket\n"
                   "再起動の上限に達したときは、次のコマンドで戻せます:\n"
                   "systemctl --user reset-failed pazkey-server.socket pazkey-server.service\n"
                   "systemctl --user start pazkey-server.socket");
            return true;
        });
        return;
    }
    if (noticeTimer_->isEnabled()) return;  // すでに数えている
    noticeTimer_->setTime(at);
    noticeTimer_->setOneShot();
}

void PazkeyEngine::onMessage(const Incoming &msg) {
    if (msg.type != "result" && msg.type != "error") {
        PAZKEY_DEBUG() << "recv " << msg.type << (msg.protocol ? " protocol=" + std::to_string(*msg.protocol) : "");
    }
    if (msg.type == "hello") {
        if (msg.protocol && *msg.protocol != kProtocolVersion) {
            client_->stop();
            notify("pazkey: 変換サーバーと版が合いません", "fcitx5-pazkey と pazkey-server を同じ版にしてください。");
            return;
        }
        onZenzaiState(msg.zenzai);
        return;
    }
    if (msg.type == "error" && !msg.session) PAZKEY_DEBUG() << "recv error code=" << msg.code;
    if (msg.type == "error" && msg.code == "protocol_mismatch") {
        client_->stop();
        notify("pazkey: 変換サーバーと版が合いません", "fcitx5-pazkey と pazkey-server を同じ版にしてください。");
        return;
    }
    if (msg.type == "config_changed") {
        loadConfig();
        onZenzaiState(msg.zenzai);
        return;
    }
    if (msg.type == "config_rejected") {
        // 不正なら loadConfig が通知する。読んだ時点で直っていれば、そのまま読み込む。
        loadConfig();
        return;
    }
    if ((msg.type == "result" || msg.type == "error") && msg.session) {
        auto it = sessions_.find(*msg.session);
        if (it == sessions_.end()) {
            PAZKEY_DEBUG() << "s=" << *msg.session << " recv " << msg.type << " for unknown session";
            return;
        }
        PazkeyContext *ctx = it->second;
        if (msg.type == "result") {
            ctx->state().onResult(msg);
        } else {
            ctx->state().onError(msg);
        }
        ctx->render();
    }
}

void PazkeyEngine::onZenzaiState(const std::string &state) {
    PAZKEY_DEBUG() << "zenzai=" << state;
    if (!state.empty()) zenzaiState_ = state;
    auto notice = zenzaiNotice_.update(state);
    if (!notice) return;
    if (*notice == "unavailable") {
        notify("pazkey: Zenzai のモデルを読み込めません",
               "Zenzai を使わずに変換を続けます。\n"
               "モデルは、設定画面の「Zenzai」の「モデルをダウンロード」で取得できます。\n"
               "自分で用意したモデルを使う場合は、パスを確認してください。");
    } else if (*notice == "unsupported") {
        notify("pazkey: Zenzai は搭載の CPU をサポートしません",
               "AVX2 に対応した CPU が必要です。Zenzai を使わずに変換を続けます。");
    } else {
        notify("pazkey: Zenzai を一時的に止めています",
               "変換サーバーの異常終了が続いたため、Zenzai を使わずに動いています。\n"
               "設定画面で保存し直すと、Zenzai をもう一度試します。");
    }
}

void PazkeyEngine::notify(const std::string &summary, const std::string &body) {
    // showTip は「今後表示しない」を選ばれると二度と出なくなるので使わない。
    auto *n = notifications();
    if (!n) {
        FCITX_WARN() << "pazkey: notifications アドオンがないので通知しない: " << summary;
        return;
    }
    FCITX_INFO() << "pazkey: 通知を送る: " << summary;
    n->call<fcitx::INotifications::sendNotification>("fcitx5-pazkey", 0, "fcitx_pazkey", summary, body,
                                                     std::vector<std::string>{}, -1, nullptr, nullptr);
}

}  // namespace pazkey

FCITX_ADDON_FACTORY(pazkey::PazkeyEngineFactory);
