// PazkeyState のテスト。docs/frontend.md の規則と、空の文字列を確定しないこと、変化のない preedit を
// 送らないこと（docs/frontend.md「画面への反映（render）」）を確かめる。
#include <gtest/gtest.h>

#include "json.h"
#include "state.h"

using namespace pazkey;
using T = Key::Type;

namespace {

class FakeHost : public StateHost {
public:
    void commitString(const std::string &text) override { commits.push_back(text); }
    bool connected() const override { return isConnected; }
    void send(const std::string &line) override {
        ASSERT_TRUE(isConnected);
        ASSERT_FALSE(line.empty());
        ASSERT_EQ(line.back(), '\n');
        auto j = Json::parse(std::string_view(line).substr(0, line.size() - 1));
        ASSERT_TRUE(j);
        sent.push_back(*j);
    }
    void startFullTimer(int ms) override {
        timerRunning = true;
        timerMs = ms;
    }
    void cancelFullTimer() override { timerRunning = false; }
    Surrounding surrounding() const override { return surround; }
    bool isPassword() const override { return password; }
    const Config &config() const override { return cfg; }
    bool debugEnabled() const override { return debug; }
    void debugLog(const std::string &line) override { log.push_back(line); }

    std::vector<std::string> sentTypes() const {
        std::vector<std::string> out;
        for (const auto &j : sent) out.push_back(*j.get("type")->asString());
        return out;
    }
    const Json &last(std::string_view type) const {
        for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
            if (*it->get("type")->asString() == type) return *it;
        }
        throw std::runtime_error("not sent");
    }

    bool isConnected = true;
    bool password = false;
    bool timerRunning = false;
    int timerMs = 0;
    Surrounding surround;
    Config cfg;
    bool debug = false;
    std::vector<std::string> log;
    std::vector<std::string> commits;
    std::vector<Json> sent;
};

class StateTest : public ::testing::Test {
protected:
    FakeHost host;
    PazkeyState state{host, 7};

    void type(std::string_view keys) {
        for (char c : keys) EXPECT_TRUE(state.keyEvent(Key::character(c)));
    }
    bool press(T t) { return state.keyEvent(Key::of(t)); }
    int64_t lastSeq() const { return *host.last("convert").get("seq")->asInt(); }
    void reply(int64_t seq, std::vector<Candidate> cands) {
        Incoming in;
        in.type = "result";
        in.session = 7;
        in.seq = seq;
        in.candidates = std::move(cands);
        state.onResult(in);
    }
    std::string preedit() { return state.render().preedit.text(); }
};

}  // namespace

TEST_F(StateTest, EmptyPassesNonInputKeys) {
    // 入力のない状態の Ctrl+A などは、アプリへそのまま渡し、何も送らない（PR#33）。
    EXPECT_FALSE(press(T::Other));
    EXPECT_FALSE(press(T::Enter));
    EXPECT_FALSE(press(T::Backspace));
    auto r = state.render();
    EXPECT_FALSE(r.preeditChanged);
    EXPECT_FALSE(r.panelChanged);
    EXPECT_TRUE(host.commits.empty());
    EXPECT_TRUE(host.sent.empty());
}

TEST_F(StateTest, ComposingShowsKanaAndTail) {
    type("kik");
    EXPECT_EQ(state.mode(), Mode::Composing);
    auto r = state.render();
    EXPECT_TRUE(r.preeditChanged);
    EXPECT_EQ(r.preedit.text(), "きｋ");  // 打ちかけのローマ字も全角で表示する（Mozc と同じ）
    EXPECT_EQ(r.preedit.cursorBytes, std::string("きｋ").size());
    // 何も変えなければ、次の render は何も送らない。
    r = state.render();
    EXPECT_FALSE(r.preeditChanged);
    EXPECT_FALSE(r.panelChanged);
}

TEST_F(StateTest, EnterCommitsKanaAndResets) {
    type("kikain");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"きかいん"});
    EXPECT_EQ(state.mode(), Mode::Empty);
    EXPECT_EQ(host.sentTypes(), std::vector<std::string>{"reset"});
}

TEST_F(StateTest, BackspaceToEmptySendsResetAndNoCommit) {
    type("ka");
    press(T::Backspace);
    EXPECT_EQ(state.mode(), Mode::Empty);
    EXPECT_TRUE(host.commits.empty());
    EXPECT_EQ(host.sentTypes(), std::vector<std::string>{"reset"});
    // preedit は空に戻す（1回だけ）。
    EXPECT_TRUE(state.render().preedit.empty());
}

TEST_F(StateTest, BackspaceRemovesTailFirst) {
    type("kak");
    press(T::Backspace);
    EXPECT_EQ(preedit(), "か");
    press(T::Backspace);
    EXPECT_EQ(state.mode(), Mode::Empty);
}

TEST_F(StateTest, EscapeDiscards) {
    type("aiu");
    press(T::Escape);
    EXPECT_TRUE(host.commits.empty());
    EXPECT_EQ(state.mode(), Mode::Empty);
    EXPECT_EQ(host.sentTypes(), std::vector<std::string>{"reset"});
}

TEST_F(StateTest, CursorMovementAndInsert) {
    type("aiu");
    press(T::Left);
    press(T::Left);
    type("ka");
    EXPECT_EQ(preedit(), "あかいう");
    press(T::Home);
    press(T::Delete);
    EXPECT_EQ(preedit(), "かいう");
    press(T::End);
    EXPECT_EQ(state.cursor(), 3u);
}

TEST_F(StateTest, MovingCursorFlushesN) {
    type("hon");
    press(T::Left);
    EXPECT_EQ(preedit(), "ほん");
    EXPECT_EQ(state.cursor(), 1u);
}

TEST_F(StateTest, F7F8ChangeDisplayAndCommit) {
    type("gakkou");
    press(T::F7);
    EXPECT_EQ(preedit(), "ガッコウ");
    press(T::F8);
    EXPECT_EQ(preedit(), "ｶﾞｯｺｳ");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"ｶﾞｯｺｳ"});
    EXPECT_TRUE(host.sent.empty() == false);  // reset
}

TEST_F(StateTest, ConvertSendsFullAndWaitsWithKana) {
    type("kikain");
    press(T::Space);
    EXPECT_EQ(state.mode(), Mode::Converting);
    const Json &c = host.last("convert");
    EXPECT_EQ(*c.get("kind")->asString(), "full");
    EXPECT_EQ(*c.get("reading")->asString(), "きかいん");  // 末尾の n はんにしてから送る
    EXPECT_EQ(c.get("cursor")->asInt(), 4);
    EXPECT_EQ(c.get("session")->asInt(), 7);
    EXPECT_TRUE(host.timerRunning);
    EXPECT_EQ(host.timerMs, 1000);
    auto r = state.render();
    EXPECT_EQ(r.preedit.text(), "きかいん");
    EXPECT_TRUE(r.panel.candidates.empty());
}

TEST_F(StateTest, ResultShowsCandidatesAndCommitLearns) {
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機会", 3}, {"機械", 3}});
    EXPECT_FALSE(host.timerRunning);
    auto r = state.render();
    ASSERT_EQ(r.panel.candidates.size(), 2u);
    EXPECT_EQ(r.preedit.text(), "機会");
    press(T::Space);
    EXPECT_EQ(state.render().preedit.text(), "機械");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    const Json &commit = host.last("commit");
    EXPECT_EQ(commit.get("seq")->asInt(), lastSeq());
    EXPECT_EQ(commit.get("index")->asInt(), 1);
    // 読みがすべて使われた commit の直後にも reset を送る。
    EXPECT_EQ(host.sentTypes(), (std::vector<std::string>{"convert", "commit", "reset"}));
    EXPECT_EQ(state.mode(), Mode::Empty);
}

TEST_F(StateTest, StaleResultIsDropped) {
    type("kikai");
    press(T::Space);
    int64_t first = lastSeq();
    press(T::ShiftLeft);  // 範囲を縮めて送り直す
    EXPECT_EQ(host.last("convert").get("cursor")->asInt(), 2);
    reply(first, {{"古い", 3}});
    EXPECT_TRUE(state.candidates().empty());
    reply(lastSeq(), {{"機", 2}});
    EXPECT_EQ(state.candidates().size(), 1u);
}

TEST_F(StateTest, PartialCommitConvertsRestWithContext) {
    type("kikaiwotukuru");
    press(T::Space);
    reply(lastSeq(), {{"機械を", 4}});
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械を"});
    EXPECT_EQ(state.mode(), Mode::Converting);
    const Json &c = host.last("convert");
    EXPECT_EQ(*c.get("reading")->asString(), "つくる");
    EXPECT_EQ(c.get("cursor")->asInt(), 3);
    // 周辺テキストを渡さないアプリでは、直前に確定した文字列を左文脈にする。
    EXPECT_EQ(*c.get("left_context")->asString(), "機械を");
}

TEST_F(StateTest, PartialCommitAfterShrinkUsesRestOfReading) {
    type("kikaiwo");
    press(T::Space);
    for (int i = 0; i < 2; ++i) press(T::ShiftLeft);  // 範囲は「きかい」
    reply(lastSeq(), {{"機械", 3}});
    press(T::Enter);
    // 範囲を使い切ったら、残り全体を範囲にする（cursor が 0 にならない）。
    EXPECT_EQ(*host.last("convert").get("reading")->asString(), "を");
    EXPECT_EQ(host.last("convert").get("cursor")->asInt(), 1);
}

TEST_F(StateTest, PartialCommitAppendsToContextWithoutRereading) {
    host.surround = {true, "今日は", 3, 3};
    type("kikaiwotukuru");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "今日は");
    reply(lastSeq(), {{"機械を", 4}});
    // アプリが周辺テキストを更新したかどうかに関係なく、確定した文字列を足す（docs/frontend.md「左文脈」）。
    host.surround = {true, "今日は機械をつくる", 9, 9};
    press(T::Enter);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "今日は機械を");
}

TEST_F(StateTest, PreeditInSurroundingTextIsIgnored) {
    // KWrite は周辺テキストに preedit を含めて渡すことがある（docs/frontend.md「左文脈」）。
    host.surround = {true, "今日は", 3, 3};
    type("aiueo");
    host.surround = {true, "今日はあいうえお", 8, 8};
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "今日は");
}

TEST_F(StateTest, NextInputBeforeAppUpdatesSurroundingUsesCommitted) {
    host.surround = {true, "今日は", 3, 3};
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    press(T::Enter);
    // アプリはまだ周辺テキストを更新していないまま、次の入力が始まる。
    type("wo");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "今日は機械");
    press(T::Escape);
    press(T::Escape);
    // アプリが更新したら、周辺テキストから作る。
    host.surround = {true, "今日は機械が", 6, 6};
    type("wo");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "今日は機械が");
}

TEST_F(StateTest, BackspaceInConvertingCancelsLikeMozc) {
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    press(T::Backspace);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "きかい");  // かなは消さない
    EXPECT_TRUE(host.commits.empty());
    press(T::Backspace);
    EXPECT_EQ(preedit(), "きか");
    // 結果を待っている間も同じ。
    press(T::Space);
    press(T::Backspace);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_FALSE(host.timerRunning);
    EXPECT_EQ(preedit(), "きか");
}

TEST_F(StateTest, LeftContextIsTruncatedByCodepoints) {
    host.cfg.context.maxChars = 3;
    host.surround = {true, "ab👨‍👩‍👧😀x", 9, 9};
    type("a");
    press(T::Space);
    auto ctx = *host.last("convert").get("left_context")->asString();
    // 単位はコードポイント。UTF-8 の文字の途中では切らない。
    EXPECT_EQ(ctx, "👧😀x");
}

TEST_F(StateTest, ContextDisabled) {
    host.cfg.context.enabled = false;
    host.surround = {true, "今日は", 3, 3};
    type("a");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "");
}

TEST_F(StateTest, TimeoutShowsFallbackAndExpiresSeq) {
    type("kikai");
    press(T::Space);
    int64_t seq = lastSeq();
    state.onFullTimeout();
    ASSERT_EQ(state.candidates().size(), 3u);
    EXPECT_EQ(state.candidates()[0].text, "きかい");
    EXPECT_EQ(state.candidates()[1].text, "キカイ");
    EXPECT_EQ(state.candidates()[2].text, "ｷｶｲ");
    EXPECT_EQ(state.candidates()[0].consumed, 3u);
    press(T::Space);
    // 後から届いた同じ seq の result は捨てる（選んでいる途中で差し替わらない）。
    reply(seq, {{"機械", 3}});
    EXPECT_EQ(state.candidates()[1].text, "キカイ");
    EXPECT_EQ(state.selected(), 1u);
    // 代用の候補を確定しても commit は送らない。reset は送る。
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"キカイ"});
    EXPECT_EQ(host.sentTypes(), (std::vector<std::string>{"convert", "reset"}));
}

TEST_F(StateTest, ErrorShowsFallback) {
    type("a");
    press(T::Space);
    Incoming err;
    err.type = "error";
    err.seq = lastSeq();
    err.code = "internal";
    state.onError(err);
    EXPECT_FALSE(state.candidates().empty());
    EXPECT_TRUE(state.render().panel.aux.empty());  // つながっているので「サーバー未接続」は出さない
}

TEST_F(StateTest, DisconnectedShowsFallbackImmediately) {
    host.isConnected = false;
    type("kikai");
    press(T::Space);
    EXPECT_TRUE(host.sent.empty());
    EXPECT_EQ(state.candidates().size(), 3u);
    auto r = state.render();
    EXPECT_EQ(r.panel.aux, kDisconnectedAux);
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"きかい"});
    EXPECT_TRUE(host.sent.empty());  // つながっていないので reset も送らない
}

TEST_F(StateTest, TooLongReadingIsNotSent) {
    std::string keys(kMaxReadingLength + 1, 'a');
    type(keys);
    press(T::Space);
    EXPECT_TRUE(host.sentTypes().empty());
    EXPECT_FALSE(state.candidates().empty());
}

TEST_F(StateTest, PasswordDoesNotLearnOrSendContext) {
    host.password = true;
    host.surround = {true, "secret", 6, 6};
    type("kikai");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "");
    reply(lastSeq(), {{"機械", 3}});
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    for (const auto &t : host.sentTypes()) EXPECT_NE(t, "commit");
    // 確定した文字列は左文脈に入れない。フォーカスが外れないまま普通の入力欄へ移っても、送られない。
    type("aa");
    press(T::Enter);  // かなのままの確定も同じ
    host.password = false;
    host.surround = {};  // 周辺テキストを渡さないアプリ
    type("kikai");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "");
    press(T::Escape);
    press(T::Escape);
    // 周辺テキストが確定したときと同じままでも、確定直後の記録は使わない。
    host.password = true;
    host.surround = {true, "", 0, 0};
    type("a");
    press(T::Enter);
    host.password = false;
    type("kikai");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("left_context")->asString(), "");
}

TEST_F(StateTest, DigitsSelectOnPage) {
    host.cfg.candidates.pageSize = 2;
    type("a");
    press(T::Space);
    reply(lastSeq(), {{"亜", 1}, {"阿", 1}, {"あ", 1}, {"ア", 1}});
    press(T::PageDown);
    EXPECT_EQ(state.selected(), 2u);
    type("2");
    EXPECT_EQ(host.commits, std::vector<std::string>{"ア"});
    EXPECT_EQ(host.last("commit").get("index")->asInt(), 3);
}

TEST_F(StateTest, DigitsAsInputWhenDisabled) {
    host.cfg.candidates.selectWithDigits = false;
    type("a");
    press(T::Space);
    reply(lastSeq(), {{"亜", 1}});
    type("1");
    EXPECT_EQ(host.commits, std::vector<std::string>{"亜"});
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "１");
}

TEST_F(StateTest, TypingWhileConvertingCommitsAndContinues) {
    type("kikaiwo");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    type("t");
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "をｔ");
    // 読みが残っているので reset はまだ送らない。
    EXPECT_EQ(host.sentTypes(), (std::vector<std::string>{"convert", "commit"}));
}

TEST_F(StateTest, EscapeFromConvertingDropsLateResult) {
    type("kikai");
    press(T::Space);
    int64_t seq = lastSeq();
    press(T::Escape);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_FALSE(host.timerRunning);
    reply(seq, {{"機械", 3}});
    EXPECT_TRUE(state.candidates().empty());
    EXPECT_EQ(preedit(), "きかい");
}

TEST_F(StateTest, EndInputCommitsDisplayed) {
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    state.endInput(true);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    EXPECT_EQ(host.sentTypes(), (std::vector<std::string>{"convert", "commit", "reset"}));
    EXPECT_EQ(state.mode(), Mode::Empty);
}

TEST_F(StateTest, EndInputWithoutCommitStillLearnsAndResets) {
    type("kikaiwo");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    state.endInput(false);  // フォーカスの喪失。確定は fcitx5 が行う
    EXPECT_TRUE(host.commits.empty());
    EXPECT_EQ(host.sentTypes(), (std::vector<std::string>{"convert", "commit", "reset"}));
}

TEST_F(StateTest, EndInputWhenEmptyDoesNothing) {
    // LibreOffice（#40）、Wayland（PR#38）: 入力していないときは何も送らない。
    state.endInput(true);
    state.endInput(false);
    state.discardInput();
    auto r = state.render();
    EXPECT_FALSE(r.preeditChanged);
    EXPECT_FALSE(r.panelChanged);
    EXPECT_TRUE(host.commits.empty());
    EXPECT_TRUE(host.sent.empty());
}

TEST_F(StateTest, NoEmptyCommitEver) {
    type("n");
    press(T::Backspace);
    type("a");
    press(T::Space);
    reply(lastSeq(), {{"", 1}});
    press(T::Enter);
    for (const auto &c : host.commits) EXPECT_FALSE(c.empty());
}

TEST_F(StateTest, CloseOnlyAfterUse) {
    state.close();
    EXPECT_TRUE(host.sent.empty());
    type("a");
    press(T::Escape);
    state.close();
    EXPECT_EQ(host.sentTypes().back(), "close");
}

TEST_F(StateTest, DebugLogHasNoText) {
    // デバッグログには、読み、候補、確定した文字列、文脈を書かない（docs/frontend.md「デバッグログ」）。
    // ここで使う文字列はどれも ASCII でないので、ログが ASCII だけなら中身は入っていない。
    host.debug = true;
    host.surround = {true, "秘密の文脈", 5, 5};
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}, {"機会", 3}});
    press(T::Down);
    press(T::Enter);
    type("wo");
    press(T::Space);
    host.isConnected = false;
    state.onFullTimeout();
    state.endInput(false);
    ASSERT_FALSE(host.log.empty());
    for (const auto &line : host.log) {
        for (unsigned char c : line) EXPECT_LT(c, 0x80) << line;
    }
}

TEST_F(StateTest, DebugLogOffWritesNothing) {
    type("kikai");
    press(T::Space);
    EXPECT_TRUE(host.log.empty());
}

// ---- 字種（docs/frontend.md「字種」） ----

TEST_F(StateTest, SpaceWhenEmptyCommitsFullWidthSpace) {
    // 入力がないときの Space は全角、Shift+Space は半角の空白（Mozc と同じ）。サーバーには何も送らない。
    EXPECT_TRUE(press(T::Space));
    EXPECT_TRUE(press(T::ShiftSpace));
    EXPECT_EQ(host.commits, (std::vector<std::string>{"　", " "}));
    EXPECT_EQ(state.mode(), Mode::Empty);
    EXPECT_TRUE(host.sent.empty());
    EXPECT_FALSE(state.render().preeditChanged);
}

TEST_F(StateTest, SymbolsAndDigitsAreFullWidthWhileComposing) {
    type("2026nenn?");
    EXPECT_EQ(preedit(), "２０２６ねん？");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"２０２６ねん？"});
}

TEST_F(StateTest, TailIsFlushedAsFullWidth) {
    type("ky");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"ｋｙ"});
}

TEST_F(StateTest, WwwIsFullWidthWhileComposing) {
    // Mozc の表は www → w + ww と半角の w を出す。入力中はそれも全角にする。
    type("www");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"ｗｗｗ"});
}

TEST_F(StateTest, ReadingHasHalfWidthDigitsAndLetters) {
    // 読みでは数字と英字を半角、記号を全角にする（azooKey は読みの字種のまま候補を出す）。
    type("2026nenn");
    press(T::Space);
    EXPECT_EQ(*host.last("convert").get("reading")->asString(), "2026ねん");
    press(T::Escape);
    press(T::Escape);
    type("honntou?www");
    press(T::Space);
    const auto &c = host.last("convert");
    EXPECT_EQ(*c.get("reading")->asString(), "ほんとう？www");
    EXPECT_EQ(*c.get("cursor")->asInt(), 8);  // 字種を変えても文字数は同じ
}

TEST_F(StateTest, ShiftLetterStartsAsciiInput) {
    // Shift で英字を打ったら、確定するまで半角英字。小文字もローマ字にしない。
    type("Google");
    EXPECT_TRUE(state.asciiInput());
    EXPECT_EQ(preedit(), "Google");
    press(T::Space);  // 変換せず、半角の空白を入れる
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "Google ");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"Google "});
    // 確定したら元に戻る。
    EXPECT_FALSE(state.asciiInput());
    type("ka");
    EXPECT_EQ(preedit(), "か");
}

TEST_F(StateTest, ShiftLetterInTheMiddle) {
    type("aiG");
    EXPECT_EQ(preedit(), "あいG");
    type("oo");
    EXPECT_EQ(preedit(), "あいGoo");
    // 打ちかけのローマ字は、先に全角にしてから半角英字の入力に移る。
    press(T::Escape);
    type("kT");
    EXPECT_EQ(preedit(), "ｋT");
}

TEST_F(StateTest, ConvertKeyConvertsInAsciiInput) {
    type("Abc");
    press(T::Convert);
    EXPECT_EQ(state.mode(), Mode::Converting);
    EXPECT_EQ(*host.last("convert").get("reading")->asString(), "Abc");
}

TEST_F(StateTest, FallbackFirstCandidateIsDisplayForm) {
    host.isConnected = false;
    type("2026?");
    press(T::Space);
    std::vector<std::string> texts;
    for (const auto &c : state.candidates()) texts.push_back(c.text);
    // 1つめは入力中の表示と同じ。半角カタカナの形では英数字と記号も半角になる。
    EXPECT_EQ(texts, (std::vector<std::string>{"２０２６？", "2026?"}));
}

// ---- Mozc の MS-IME の表に合わせたキー（docs/frontend.md「キーごとの動作」） ----

TEST_F(StateTest, SlashIsMiddleDot) {
    type("a/b");
    EXPECT_EQ(preedit(), "あ・ｂ");
}

TEST_F(StateTest, ShiftArrowsMoveCursorWhileComposing) {
    type("kikai");
    press(T::ShiftLeft);
    press(T::ShiftLeft);
    EXPECT_EQ(state.cursor(), 1u);
    press(T::ShiftRight);
    EXPECT_EQ(state.cursor(), 2u);
    press(T::Down);  // 末尾へ
    EXPECT_EQ(state.cursor(), 3u);
    EXPECT_TRUE(host.sent.empty());
}

TEST_F(StateTest, ShiftSpaceConvertsWhileComposing) {
    type("kikai");
    press(T::ShiftSpace);
    EXPECT_EQ(state.mode(), Mode::Converting);
    EXPECT_EQ(*host.last("convert").get("reading")->asString(), "きかい");
}

TEST_F(StateTest, F9F10ShowTypedKeys) {
    // 打ったとおりの英数字にする（Mozc の ConvertToFullAlphanumeric / ConvertToHalfAlphanumeric）。
    type("shinnbunn?");
    press(T::F10);
    EXPECT_EQ(preedit(), "shinnbunn?");
    press(T::F9);
    EXPECT_EQ(preedit(), "ｓｈｉｎｎｂｕｎｎ？");
    press(T::F6);
    EXPECT_EQ(preedit(), "しんぶん？");
    // っ になった子音や、打ちかけのローマ字も含める。
    press(T::Escape);
    type("kitteky");
    press(T::F10);
    EXPECT_EQ(preedit(), "kitteky");
    press(T::Enter);
    EXPECT_EQ(host.commits.back(), "kitteky");
}

TEST_F(StateTest, F10AfterBackspaceAndPartialCommit) {
    type("kikai");
    press(T::Backspace);  // い を消す
    press(T::F10);
    EXPECT_EQ(preedit(), "kika");
    press(T::Escape);
    type("kikaibuhinn");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    press(T::Enter);  // 機械 を確定し、残りの ぶひん を変換する
    press(T::F10);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "buhinn");
}

TEST_F(StateTest, FunctionKeysInConvertingCancelAndChangeForm) {
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    press(T::F7);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "キカイ");
    press(T::Space);  // 結果を待っている間も同じ
    press(T::F10);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_FALSE(host.timerRunning);
    EXPECT_EQ(preedit(), "kikai");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"kikai"});
}

TEST_F(StateTest, ShiftKeysInConverting) {
    host.cfg.candidates.pageSize = 2;
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}, {"機会", 3}, {"奇怪", 3}, {"きかい", 3}, {"キカイ", 3}});
    press(T::ShiftDown);  // 次のページ
    EXPECT_EQ(state.selected(), 2u);
    press(T::ShiftUp);  // 前のページ
    EXPECT_EQ(state.selected(), 0u);
    press(T::ShiftTab);  // 前の候補（先頭から末尾へ回る）
    EXPECT_EQ(state.selected(), 4u);
    press(T::ShiftConvert);
    EXPECT_EQ(state.selected(), 3u);
}

TEST_F(StateTest, DeleteInConvertingCancels) {
    type("kikai");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}});
    press(T::Delete);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "きかい");
    press(T::Space);
    press(T::Delete);  // 結果を待っている間も同じ
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_TRUE(host.commits.empty());
}

TEST_F(StateTest, NumpadIsHalfWidth) {
    // テンキーの文字は、ローマ字にせず半角のまま入れる（Mozc の numpad_character_form の既定）。
    EXPECT_TRUE(state.keyEvent(Key::numpadChar('1')));
    EXPECT_EQ(state.mode(), Mode::Composing);
    type("ka");
    state.keyEvent(Key::numpadChar('-'));
    type("k");
    state.keyEvent(Key::numpadChar('2'));  // 打ちかけのローマ字は先に全角にする
    EXPECT_EQ(preedit(), "1か-ｋ2");
    press(T::F10);
    EXPECT_EQ(preedit(), "1ka-k2");
}

TEST_F(StateTest, F10KeepsKeysOfMultiCharRule) {
    // 1回の規則で出た きゃ のキーは き にまとめて持つ。ゃ を消しても kya のまま。
    type("kya");
    press(T::Backspace);
    press(T::F10);
    EXPECT_EQ(preedit(), "kya");
}

// ---- ライブ変換（docs/frontend.md「ライブ変換の表示と確定」） ----

namespace {

class LiveTest : public StateTest {
protected:
    void SetUp() override { host.cfg.liveConversion.enabled = true; }
    const Json &lastConvert() const { return host.last("convert"); }
    std::string lastKind() const { return *lastConvert().get("kind")->asString(); }
    size_t convertCount() const {
        size_t n = 0;
        for (const auto &t : host.sentTypes()) n += t == "convert";
        return n;
    }
};

}  // namespace

TEST_F(LiveTest, DisabledSendsNothing) {
    host.cfg.liveConversion.enabled = false;
    type("kikai");
    EXPECT_TRUE(host.sent.empty());
}

TEST_F(LiveTest, SendsOnlyWhenKanaChanges) {
    type("k");  // ローマ字の途中では送らない
    EXPECT_TRUE(host.sent.empty());
    type("i");
    ASSERT_EQ(convertCount(), 1u);
    EXPECT_EQ(lastKind(), "live");
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "き");
    EXPECT_EQ(*lastConvert().get("cursor")->asInt(), 1);
    type("k");
    EXPECT_EQ(convertCount(), 1u);
    type("a");
    EXPECT_EQ(convertCount(), 2u);
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "きか");
}

TEST_F(LiveTest, KeepsPreviousResultWhileWaiting) {
    // 木 → 木か → 価格 → 価格い → 機械（docs/frontend.md「ライブ変換の表示と確定」）。
    type("ki");
    reply(lastSeq(), {{"木", 1}});
    EXPECT_EQ(preedit(), "木");
    type("ka");
    EXPECT_EQ(preedit(), "木か");
    reply(lastSeq(), {{"価格", 2}});
    EXPECT_EQ(preedit(), "価格");
    type("i");
    EXPECT_EQ(preedit(), "価格い");
    type("k");
    EXPECT_EQ(preedit(), "価格いｋ");  // 打ちかけのローマ字は末尾に
    auto r = state.render();
    EXPECT_EQ(r.preedit.cursorBytes, r.preedit.text().size());
}

TEST_F(LiveTest, StaleResultIsDropped) {
    type("ki");
    int64_t first = lastSeq();
    type("ka");
    reply(first, {{"木", 1}});
    EXPECT_EQ(preedit(), "きか");
}

TEST_F(LiveTest, PartialConsumedShowsRest) {
    type("kikaiwo");
    reply(lastSeq(), {{"機械", 3}});
    EXPECT_EQ(preedit(), "機械を");
}

TEST_F(LiveTest, UnusableOrErrorShowsKana) {
    type("ki");
    reply(lastSeq(), {{"木", 1}});
    type("ka");
    reply(lastSeq(), {{"x", 0}});
    EXPECT_EQ(preedit(), "きか");
    type("i");
    reply(lastSeq(), {{"機械", 3}});
    EXPECT_EQ(preedit(), "機械");
    type("n");
    type("n");
    Incoming err;
    err.type = "error";
    err.session = 7;
    err.seq = lastSeq();
    err.code = "internal";
    state.onError(err);
    EXPECT_EQ(preedit(), "きかいん");
}

TEST_F(LiveTest, BackspaceDropsResultAndResends) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    size_t before = convertCount();
    press(T::Backspace);
    EXPECT_EQ(preedit(), "きか");  // 前の部分が変わったので、前の結果は使わない
    EXPECT_EQ(convertCount(), before + 1);
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "きか");
}

TEST_F(LiveTest, BackspaceOfTailOnlyDoesNotSend) {
    type("kak");
    size_t before = convertCount();
    press(T::Backspace);
    EXPECT_EQ(convertCount(), before);
}

TEST_F(LiveTest, CursorNotAtEndShowsKanaAndResumesAtEnd) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    size_t before = convertCount();
    press(T::Left);
    EXPECT_EQ(preedit(), "きかい");
    EXPECT_EQ(convertCount(), before);
    type("n");
    type("n");  // 途中に入れても、末尾にないので送らない
    EXPECT_EQ(convertCount(), before);
    press(T::End);  // 末尾に戻ったら送る
    EXPECT_EQ(convertCount(), before + 1);
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "きかんい");
    press(T::End);  // もう末尾にあれば送らない
    EXPECT_EQ(convertCount(), before + 1);
}

TEST_F(LiveTest, EnterCommitsLiveAndLearnsLatest) {
    type("kikai");
    int64_t seq = lastSeq();
    reply(seq, {{"機械", 3}});
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    const Json &c = host.last("commit");
    EXPECT_EQ(*c.get("seq")->asInt(), seq);
    EXPECT_EQ(*c.get("index")->asInt(), 0);
    EXPECT_EQ(host.sentTypes().back(), "reset");
    EXPECT_EQ(state.mode(), Mode::Empty);
}

TEST_F(LiveTest, EnterWithPreviousResultDoesNotLearn) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    type("wo");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械を"});
    for (const auto &t : host.sentTypes()) EXPECT_NE(t, "commit");
}

TEST_F(LiveTest, EnterFlushesTailAfterLive) {
    type("kikai");
    int64_t seq = lastSeq();
    reply(seq, {{"機械", 3}});
    type("n");
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械ん"});
    EXPECT_EQ(*host.last("commit").get("seq")->asInt(), seq);  // 送り直していないので、最新のまま
}

TEST_F(LiveTest, EscapeFromConvertingShowsKanaUntilKanaChanges) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    press(T::Space);
    EXPECT_EQ(lastKind(), "full");
    size_t before = convertCount();
    press(T::Escape);
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(preedit(), "きかい");
    EXPECT_EQ(convertCount(), before);
    type("wo");
    EXPECT_EQ(convertCount(), before + 1);
    EXPECT_EQ(lastKind(), "live");
}

TEST_F(LiveTest, CursorBackToEndAfterEscapeSendsLive) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    press(T::Space);
    press(T::Escape);
    size_t before = convertCount();
    press(T::End);  // もう末尾にあるので送らない
    EXPECT_EQ(convertCount(), before);
    press(T::Left);
    EXPECT_EQ(convertCount(), before);
    press(T::Right);  // 末尾に戻したのは新しい操作として送る
    EXPECT_EQ(convertCount(), before + 1);
    EXPECT_EQ(lastKind(), "live");
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "きかい");
}

TEST_F(LiveTest, CursorKeyAfterFunctionKeyRestoresLive) {
    type("kikai");
    int64_t seq = lastSeq();
    reply(seq, {{"機械", 3}});
    press(T::F7);
    size_t before = convertCount();
    press(T::End);  // F7 を押す前の表示に戻る。送らない
    EXPECT_EQ(preedit(), "機械");
    EXPECT_EQ(convertCount(), before);
    press(T::Enter);  // 最新の結果のままなので学習する
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    EXPECT_EQ(*host.last("commit").get("seq")->asInt(), seq);
}

TEST_F(LiveTest, LateLiveResultIsIgnoredInConverting) {
    type("kikai");
    int64_t live = lastSeq();
    press(T::Space);
    reply(live, {{"機械", 3}});
    EXPECT_TRUE(state.candidates().empty());
    EXPECT_TRUE(state.awaiting());
}

TEST_F(LiveTest, TypingAfterCandidateSendsLiveForRest) {
    type("kikaiwo");
    press(T::Space);
    reply(lastSeq(), {{"機械", 3}, {"機会", 3}});
    size_t before = convertCount();
    type("t");  // 機械 を確定し、残りの「を」に続けて入力する（t はまだかなにならない）
    EXPECT_EQ(state.mode(), Mode::Composing);
    EXPECT_EQ(convertCount(), before + 1);  // かなが変わった（を だけになった）
    EXPECT_EQ(*lastConvert().get("reading")->asString(), "を");
}

TEST_F(LiveTest, FunctionKeysOverrideLive) {
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    size_t before = convertCount();
    press(T::F7);
    EXPECT_EQ(preedit(), "キカイ");
    EXPECT_EQ(convertCount(), before);
    press(T::Enter);
    EXPECT_EQ(host.commits, std::vector<std::string>{"キカイ"});
    for (const auto &t : host.sentTypes()) EXPECT_NE(t, "commit");
}

TEST_F(LiveTest, PasswordFieldHasNoLive) {
    host.password = true;
    type("kikai");
    EXPECT_TRUE(host.sent.empty());
}

TEST_F(LiveTest, DisconnectedSendsNothingAndKeepsDisplay) {
    type("ki");
    reply(lastSeq(), {{"木", 1}});
    host.isConnected = false;
    type("ka");
    EXPECT_EQ(preedit(), "木か");
}

TEST_F(LiveTest, EndInputCommitsLiveAndLearns) {
    type("kikai");
    int64_t seq = lastSeq();
    reply(seq, {{"機械", 3}});
    state.endInput(true);
    EXPECT_EQ(host.commits, std::vector<std::string>{"機械"});
    EXPECT_EQ(*host.last("commit").get("seq")->asInt(), seq);
    EXPECT_EQ(host.sentTypes().back(), "reset");
}

TEST_F(LiveTest, EndInputByFcitxStillLearns) {
    type("kikai");
    int64_t seq = lastSeq();
    reply(seq, {{"機械", 3}});
    state.endInput(false);
    EXPECT_TRUE(host.commits.empty());
    EXPECT_EQ(*host.last("commit").get("seq")->asInt(), seq);
}

TEST_F(LiveTest, DebugLogHasNoText) {
    host.debug = true;
    type("kikai");
    reply(lastSeq(), {{"機械", 3}});
    press(T::Enter);
    for (const auto &line : host.log) {
        EXPECT_EQ(line.find("機械"), std::string::npos) << line;
        EXPECT_EQ(line.find("きかい"), std::string::npos) << line;
    }
}
