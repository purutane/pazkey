// 入力欄1つ分の入力状態とキー処理（docs/frontend.md「状態」「キーごとの動作」「非同期の扱い」）。
// fcitx5 には依存しない。アプリやサーバーとのやりとりは StateHost を通す。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "protocol.h"

namespace pazkey {

/// キー。fcitx5 のキーイベントをアドオンの側でこれに直す。
struct Key {
    enum class Type {
        Char,        // ローマ字で入力できる文字（ch に入る。テンキーなら numpad が真）
        Space,
        ShiftSpace,
        Convert,     // 変換キー
        ShiftConvert,
        Enter,       // Ctrl+Enter を含む
        Escape,
        Backspace,   // Ctrl+H、Ctrl+Backspace を含む
        Delete,
        Left,
        Right,
        Home,
        End,
        ShiftLeft,
        ShiftRight,
        Up,
        Down,
        ShiftUp,
        ShiftDown,
        Tab,
        ShiftTab,
        PageUp,
        PageDown,
        F6,
        F7,
        F8,
        F9,
        F10,
        Other,       // 上のどれでもないキー
    };
    Type type = Type::Other;
    char ch = 0;
    bool numpad = false;  // テンキーの文字（docs/frontend.md「字種」）
    static Key character(char c) { return {Type::Char, c, false}; }
    static Key numpadChar(char c) { return {Type::Char, c, true}; }
    static Key of(Type t) { return {t, 0, false}; }
};

/// アプリから受け取った周辺テキスト。cursor と anchor はコードポイント数。
struct Surrounding {
    bool valid = false;
    std::string text;
    unsigned cursor = 0;
    unsigned anchor = 0;
    bool operator==(const Surrounding &) const = default;
};

class StateHost {
public:
    virtual ~StateHost() = default;
    /// アプリへ確定文字列を送る。PazkeyState は空文字列では呼ばない。
    virtual void commitString(const std::string &text) = 0;
    virtual bool connected() const = 0;
    /// サーバーへ1行送る。connected() のときだけ呼ばれる。
    virtual void send(const std::string &line) = 0;
    /// `full` の期限のタイマー。期限が来たら PazkeyState::onFullTimeout を呼ぶ。
    virtual void startFullTimer(int ms) = 0;
    virtual void cancelFullTimer() = 0;
    virtual Surrounding surrounding() const = 0;
    /// パスワード欄（CapabilityFlag::Password か Sensitive）か。
    virtual bool isPassword() const = 0;
    virtual const Config &config() const = 0;
    /// デバッグログ（docs/frontend.md「デバッグログ」）。入力した文章は渡さない。
    virtual bool debugEnabled() const { return false; }
    virtual void debugLog(const std::string &) {}
};

enum class Mode { Empty, Composing, Converting };

/// デバッグログ用の名前。
const char *keyTypeName(Key::Type type);
const char *modeName(Mode mode);

struct PreeditSegment {
    std::string text;
    bool highlight = false;  // 偽なら下線
    bool operator==(const PreeditSegment &) const = default;
};

struct PreeditView {
    std::vector<PreeditSegment> segments;
    size_t cursorBytes = 0;
    bool operator==(const PreeditView &) const = default;
    bool empty() const { return segments.empty(); }
    std::string text() const;
};

struct PanelView {
    std::vector<std::string> candidates;
    int selected = -1;
    int pageSize = 9;
    bool digitLabels = true;
    std::string aux;  // 補助テキスト（つながっていないときの「サーバー未接続」だけ）
    bool operator==(const PanelView &) const = default;
    bool empty() const { return candidates.empty() && aux.empty(); }
};

/// render の結果。前回と違う部分だけ changed が真になる。
struct RenderUpdate {
    bool preeditChanged = false;
    bool panelChanged = false;
    PreeditView preedit;
    PanelView panel;
};

inline constexpr const char *kDisconnectedAux = "サーバー未接続";

class PazkeyState {
public:
    PazkeyState(StateHost &host, int64_t session) : host_(host), session_(session) {}

    /// キーを処理する。真ならキーを使った（アプリへ渡さない）。
    bool keyEvent(const Key &key);

    void onResult(const Incoming &msg);
    void onError(const Incoming &msg);
    void onFullTimeout();

    /// 入力を終える（IME の切り替え、フォーカスの喪失）。docs/frontend.md「IME の切り替えとフォーカス」。
    /// commitText が真なら表示している文字列をアプリへ確定する。偽なら確定は fcitx5 に任せる。
    /// 候補選択中なら、選択中の候補の commit をサーバーへ送る。そのあと Empty へ移る。
    void endInput(bool commitText);
    /// 入力を捨てて Empty へ移る（アプリからのリセット）。
    void discardInput();
    /// フォーカスが外れたときに、左文脈の代わりに持っている確定文字列を捨てる。
    void clearContext();
    /// 候補ウィンドウで候補をクリックした（全体での番号）。
    void selectCandidate(size_t index);
    /// 入力欄が破棄される。一度でもサーバーへ送っていれば close を送る。
    void close();

    /// 状態から表示を作り、前回の表示と比べる。
    RenderUpdate render();

    Mode mode() const { return mode_; }
    const std::u32string &kana() const { return kana_; }
    const std::string &romajiTail() const { return tail_; }
    size_t cursor() const { return cursor_; }
    int64_t session() const { return session_; }
    const std::vector<Candidate> &candidates() const { return candidates_; }
    size_t selected() const { return selected_; }
    bool awaiting() const { return awaiting_; }
    bool asciiInput() const { return ascii_; }
    /// 今の状態で送る左文脈（テスト用にも公開する）。
    std::string leftContext() const;

private:
    enum class Form { Kana, Katakana, HalfKatakana, FullAlnum, HalfAlnum };

    static Form formOf(Key::Type type);  // F6〜F10 の表示の字種
    bool keyEmpty(const Key &key);
    bool keyComposing(const Key &key);
    bool keyConverting(const Key &key);

    // ライブ変換（docs/frontend.md「ライブ変換の表示と確定」）。
    bool liveAllowed() const;  // 設定で有効で、パスワード欄でない
    void requestLive();        // 送れる状態なら live の convert を送る
    /// ライブ変換の表示（候補の文字列 + 残りのかな + romaji_tail）。表示しないなら空。
    std::optional<std::string> liveDisplay() const;
    void sendLiveCommit();  // 表示している last_live が最新の seq の結果なら、その commit を送る
    void clearLive();

    void insertKey(const Key &key);  // Char のキーを入力する（テンキーなら半角のまま）
    void insertChar(char c);
    /// cursor_ に text を入れる。raw はそれを打ったときのキー（F9 / F10 の表示に使う）。
    void insertKana(std::u32string_view text, std::string raw);
    void eraseKana(size_t pos, size_t n);
    std::u32string rawText() const;
    void flushTail();
    void cancelConverting();  // Converting から Composing へ戻る（かなは消さない）
    std::u32string composingText() const;  // Composing で表示・確定する文字列（F6〜F8 を反映）
    void startConverting();
    void requestFull();
    void showFallback(const char *reason);
    /// 候補 index を確定し、読みから consumed を取り除く。残りの扱いは呼び出し側が決める。
    void commitCandidateText(size_t index);
    /// 候補を確定する。読みが残れば残りを変換し直し、空なら Empty へ移る。
    void commitCandidate(size_t index);
    void commitToApp(const std::string &text);
    /// 候補 index の commit をサーバーへ送る。代用の候補とパスワード欄では送らない。
    void sendCommit(size_t index);
    void goEmpty();
    void sendLine(const std::string &line);
    void noteCommitted(const std::string &text);
    void startContext();  // 入力を始めたときに context_base を決める
    /// デバッグログを書く。f は書くときだけ呼ばれ、行を返す。
    template <class F>
    void debug(F &&f) const {
        if (host_.debugEnabled()) host_.debugLog(f());
    }

    StateHost &host_;
    const int64_t session_;

    Mode mode_ = Mode::Empty;
    std::u32string kana_;
    std::string tail_;
    size_t cursor_ = 0;  // Converting では変換範囲の終わり
    /// kana_ の各文字を打ったときのキー（kana_ と同じ長さ）。1回の規則で複数の文字が出たときは、
    /// 先頭の文字にまとめて持つ。F9 / F10 で、打ったとおりの英数字を表示するのに使う。
    std::vector<std::string> raw_;
    Form form_ = Form::Kana;
    /// Shift で英字を打ってから確定するまでの、一時的な半角英字の入力（docs/frontend.md「字種」）。
    bool ascii_ = false;
    /// kana_ を変えるたびに増やす。キー処理の前後で比べ、live を送るかを決める。
    uint64_t kanaVersion_ = 0;

    int64_t seq_ = 0;
    bool awaiting_ = false;  // seq_ の full の result を待っている
    std::vector<Candidate> candidates_;
    std::optional<int64_t> candidatesSeq_;  // 代用の候補なら空
    bool candidatesDisconnected_ = false;   // つながっていないときに出した代用の候補
    size_t selected_ = 0;

    struct Live {
        std::string text;
        size_t consumed = 0;
        std::u32string reading;  // この結果を求めたときの読み
        int64_t seq = 0;
    };
    std::optional<Live> lastLive_;     // docs/frontend.md の last_live
    std::optional<int64_t> liveSeq_;   // 結果を待っている live の seq
    std::u32string liveReading_;       // そのときの読み

    std::string committedContext_;
    std::string contextBase_;  // この入力で使う左文脈（docs/frontend.md「左文脈」）
    struct Pending {
        bool active = false;
        Surrounding snapshot;  // 確定したときの周辺テキスト
        std::string context;   // 確定した後の context_base
    } pending_;

    bool used_ = false;  // サーバーへ何か送ったか
    PreeditView lastPreedit_;
    PanelView lastPanel_;
};

}  // namespace pazkey
