#include "state.h"

#include <algorithm>
#include <format>

#include "mozc_defaults.h"
#include "romaji.h"
#include "utf8.h"

namespace pazkey {

namespace {

/// 左文脈の代わりに持っておく確定文字列の上限（コードポイント数）。context.max_chars の上限と同じ。
constexpr size_t kCommittedContextLimit = 256;

std::string u8(std::u32string_view s) { return encodeUtf8(s); }

/// UTF-8 の文字列のコードポイント数（デバッグログ用。中身の代わりに長さを書く）。
size_t codepoints(std::string_view s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xC0) != 0x80; }));
}

}  // namespace

PazkeyState::Form PazkeyState::formOf(Key::Type type) {
    switch (type) {
    case Key::Type::F7: return Form::Katakana;
    case Key::Type::F8: return Form::HalfKatakana;
    case Key::Type::F9: return Form::FullAlnum;
    case Key::Type::F10: return Form::HalfAlnum;
    default: return Form::Kana;
    }
}

const char *keyTypeName(Key::Type type) {
    using T = Key::Type;
    switch (type) {
    case T::Char: return "Char";
    case T::Space: return "Space";
    case T::ShiftSpace: return "ShiftSpace";
    case T::Convert: return "Convert";
    case T::ShiftConvert: return "ShiftConvert";
    case T::Enter: return "Enter";
    case T::Escape: return "Escape";
    case T::Backspace: return "Backspace";
    case T::Delete: return "Delete";
    case T::Left: return "Left";
    case T::Right: return "Right";
    case T::Home: return "Home";
    case T::End: return "End";
    case T::ShiftLeft: return "ShiftLeft";
    case T::ShiftRight: return "ShiftRight";
    case T::Up: return "Up";
    case T::Down: return "Down";
    case T::ShiftUp: return "ShiftUp";
    case T::ShiftDown: return "ShiftDown";
    case T::Tab: return "Tab";
    case T::ShiftTab: return "ShiftTab";
    case T::PageUp: return "PageUp";
    case T::PageDown: return "PageDown";
    case T::F6: return "F6";
    case T::F7: return "F7";
    case T::F8: return "F8";
    case T::F9: return "F9";
    case T::F10: return "F10";
    case T::Other: return "Other";
    }
    return "?";
}

const char *modeName(Mode mode) {
    switch (mode) {
    case Mode::Empty: return "Empty";
    case Mode::Composing: return "Composing";
    case Mode::Converting: return "Converting";
    }
    return "?";
}

std::string PreeditView::text() const {
    std::string out;
    for (const auto &s : segments) out += s.text;
    return out;
}

// ---- キー処理 ----

bool PazkeyState::keyEvent(const Key &key) {
    const Mode modeBefore = mode_;
    const uint64_t versionBefore = kanaVersion_;
    const bool atEndBefore = cursor_ == kana_.size();
    bool used = false;
    switch (mode_) {
    case Mode::Empty: used = keyEmpty(key); break;
    case Mode::Composing: used = keyComposing(key); break;
    case Mode::Converting: used = keyConverting(key); break;
    }
    if (mode_ == Mode::Composing) {
        // 前の部分が変わっていたら、前の結果は使えない。
        if (lastLive_ && std::u32string_view(kana_).substr(0, lastLive_->reading.size()) != lastLive_->reading) {
            lastLive_.reset();
        }
        // かなが変わったか、カーソルを動かして末尾に戻ったら live を送る。候補選択中から戻っただけでは送らない。
        const bool backToEnd = modeBefore == Mode::Composing && !atEndBefore && cursor_ == kana_.size();
        if (kanaVersion_ != versionBefore || backToEnd) requestLive();
    }
    return used;
}

bool PazkeyState::keyEmpty(const Key &key) {
    // 入力がないときの Space は space_character_form の空白、Shift+Space はもう一方の幅の空白を確定する
    // （Mozc の InsertSpace と InsertAlternateSpace。docs/frontend.md）。
    if (key.type == Key::Type::Space || key.type == Key::Type::ShiftSpace) {
        const std::string space = mozc::spaceForEmpty(mozc::kSpaceCharacterForm);
        commitToApp(key.type == Key::Type::Space ? space : space == " " ? "\u3000" : " ");
        return true;
    }
    if (key.type != Key::Type::Char || !isRomajiInputChar(static_cast<unsigned char>(key.ch))) return false;
    startContext();
    mode_ = Mode::Composing;
    insertKey(key);
    return true;
}

bool PazkeyState::keyComposing(const Key &key) {
    using T = Key::Type;
    switch (key.type) {
    case T::Char:
        insertKey(key);
        break;
    case T::Backspace:
        form_ = Form::Kana;
        if (!tail_.empty()) {
            tail_.pop_back();
        } else if (cursor_ > 0) {
            eraseKana(cursor_ - 1, 1);
            --cursor_;
        }
        if (kana_.empty() && tail_.empty()) goEmpty();
        break;
    case T::Delete:
        form_ = Form::Kana;
        if (cursor_ < kana_.size()) eraseKana(cursor_, 1);
        if (kana_.empty() && tail_.empty()) goEmpty();
        break;
    case T::Left:
    case T::ShiftLeft:  // Mozc の MS-IME の表では、入力中の Shift+← / → も ← / → と同じ
    case T::Right:
    case T::ShiftRight:
    case T::Home:
    case T::End:
    case T::Down:  // Mozc と同じく末尾へ
        flushTail();
        form_ = Form::Kana;
        if ((key.type == T::Left || key.type == T::ShiftLeft) && cursor_ > 0) --cursor_;
        if ((key.type == T::Right || key.type == T::ShiftRight) && cursor_ < kana_.size()) ++cursor_;
        if (key.type == T::Home) cursor_ = 0;
        if (key.type == T::End || key.type == T::Down) cursor_ = kana_.size();
        break;
    case T::Space:
    case T::ShiftSpace:  // Mozc と同じく、Space と同じ
        if (ascii_) {
            // 一時的な半角英字の入力の間は、変換せずに半角の空白を入れる（Mozc と同じ）。
            form_ = Form::Kana;
            insertKana(U" ", " ");
            break;
        }
        startConverting();
        break;
    case T::Convert:
        startConverting();
        break;
    case T::Enter: {
        flushTail();
        if (auto live = liveDisplay()) {
            commitToApp(*live);
            sendLiveCommit();
        } else {
            commitToApp(u8(composingText()));
        }
        goEmpty();
        break;
    }
    case T::Escape:
        goEmpty();
        break;
    case T::F6:
    case T::F7:
    case T::F8:
    case T::F9:
    case T::F10:
        flushTail();
        form_ = formOf(key.type);
        break;
    default:
        // 入力中は、ほかのキーをアプリへ渡さない（preedit とアプリの内容がずれないようにする）。
        break;
    }
    return true;
}

bool PazkeyState::keyConverting(const Key &key) {
    using T = Key::Type;
    const size_t n = candidates_.size();
    const auto &cfg = host_.config();
    const size_t pageSize = static_cast<size_t>(std::max(1, cfg.candidates.pageSize));

    if (n == 0) {
        // 結果を待っている間。かなを表示している。
        switch (key.type) {
        case T::Enter: {
            std::string text = u8(kana_);
            commitToApp(text);
            goEmpty();
            return true;
        }
        case T::Escape:
        case T::Backspace:  // Mozc と同じく、変換を取り消すだけ
        case T::Delete:
            cancelConverting();
            return true;
        case T::F6:
        case T::F7:
        case T::F8:
        case T::F9:
        case T::F10:
            cancelConverting();
            form_ = formOf(key.type);
            return true;
        case T::Char: {
            // 表示しているかなを確定してから、その文字の入力を始める。
            std::string text = u8(kana_);
            commitToApp(text);
            goEmpty();
            return keyEmpty(key);
        }
        case T::ShiftLeft:
        case T::ShiftRight:
            break;  // 下で扱う
        default:
            return true;
        }
    }

    switch (key.type) {
    case T::Space:
    case T::Convert:
    case T::Down:
    case T::Tab:
        selected_ = (selected_ + 1) % n;
        break;
    case T::ShiftSpace:
    case T::Up:
    case T::ShiftTab:
    case T::ShiftConvert:
        selected_ = (selected_ + n - 1) % n;
        break;
    case T::PageDown:
    case T::ShiftDown: {
        size_t next = (selected_ / pageSize + 1) * pageSize;
        if (next < n) selected_ = next;
        break;
    }
    case T::PageUp:
    case T::ShiftUp: {
        size_t page = selected_ / pageSize;
        if (page > 0) selected_ = (page - 1) * pageSize;
        break;
    }
    case T::Enter:
        commitCandidate(selected_);
        break;
    case T::ShiftLeft:
        if (cursor_ > 1) {
            --cursor_;
            requestFull();
        }
        break;
    case T::ShiftRight:
        if (cursor_ < kana_.size()) {
            ++cursor_;
            requestFull();
        }
        break;
    case T::Escape:
    case T::Backspace:  // Mozc と同じく、変換を取り消すだけ（かなは消さない）
    case T::Delete:
        cancelConverting();
        break;
    case T::F6:
    case T::F7:
    case T::F8:
    case T::F9:
    case T::F10:
        // 変換を取り消し、読み全体を表示する字種にする（pazkey は文節を分けないので、読み全体に当てる）。
        cancelConverting();
        form_ = formOf(key.type);
        break;
    case T::Char: {
        if (cfg.candidates.selectWithDigits && key.ch >= '1' && key.ch <= '9') {
            size_t index = (selected_ / pageSize) * pageSize + static_cast<size_t>(key.ch - '1');
            if (static_cast<size_t>(key.ch - '1') < pageSize && index < n) commitCandidate(index);
            break;
        }
        // 選択中の候補を確定してから、その文字の入力を始める。読みが残っていれば、その後ろに続けて入力する。
        commitCandidateText(selected_);
        if (kana_.empty()) {
            goEmpty();
            return keyEmpty(key);
        }
        mode_ = Mode::Composing;
        cursor_ = kana_.size();
        insertKey(key);
        break;
    }
    default:
        break;
    }
    return true;
}

// ---- 入力 ----

void PazkeyState::insertKey(const Key &key) {
    if (!key.numpad) {
        insertChar(key.ch);
        return;
    }
    // テンキーの文字は、ローマ字にせず半角のまま入れる（Mozc の numpad_character_form）。
    static_assert(mozc::kNumpadCharacterForm == mozc::NumpadCharacterForm::NUMPAD_HALF_WIDTH);
    form_ = Form::Kana;
    flushTail();
    insertKana(std::u32string(1, static_cast<unsigned char>(key.ch)), std::string(1, key.ch));
}

void PazkeyState::insertKana(std::u32string_view text, std::string raw) {
    if (text.empty()) return;
    ++kanaVersion_;
    kana_.insert(cursor_, text);
    std::vector<std::string> r(text.size());
    r[0] = std::move(raw);
    raw_.insert(raw_.begin() + static_cast<std::ptrdiff_t>(cursor_), r.begin(), r.end());
    cursor_ += text.size();
}

void PazkeyState::eraseKana(size_t pos, size_t n) {
    n = std::min(n, kana_.size() - std::min(pos, kana_.size()));
    if (n == 0) return;
    ++kanaVersion_;
    kana_.erase(pos, n);
    raw_.erase(raw_.begin() + static_cast<std::ptrdiff_t>(pos), raw_.begin() + static_cast<std::ptrdiff_t>(pos + n));
}

std::u32string PazkeyState::rawText() const {
    std::u32string out;
    for (const auto &r : raw_) {
        for (char c : r) out.push_back(static_cast<unsigned char>(c));
    }
    return out;
}

void PazkeyState::insertChar(char c) {
    static_assert(mozc::kShiftKeyModeSwitch == mozc::ShiftKeyModeSwitch::ASCII_INPUT_MODE);
    static_assert(mozc::kPreeditCharacterForm == mozc::CharacterForm::FULL_WIDTH);
    form_ = Form::Kana;
    if (!ascii_ && c >= 'A' && c <= 'Z') {
        // Shift で英字を打ったら、確定するまで半角英字の入力にする（docs/frontend.md「字種」）。
        flushTail();
        ascii_ = true;
        debug([] { return std::string("ascii input on"); });
    }
    if (ascii_) {
        insertKana(std::u32string(1, static_cast<unsigned char>(c)), std::string(1, c));
        return;
    }
    std::string typed = tail_ + c;
    auto step = feedRomaji(RomajiTable::builtin(), tail_, c);
    // ローマ字表にならない ASCII の文字は、入力中は全角にする（Mozc と同じ）。
    // 残ったローマ字（step.tail）は、打った列の末尾である。かなになった分のキーは、その前までになる。
    insertKana(toFullWidthAscii(step.output), typed.substr(0, typed.size() - std::min(typed.size(), step.tail.size())));
    tail_ = step.tail;
}

void PazkeyState::flushTail() {
    if (tail_.empty()) return;
    insertKana(toFullWidthAscii(flushRomaji(tail_)), tail_);
    tail_.clear();
}

void PazkeyState::cancelConverting() {
    mode_ = Mode::Composing;
    candidates_.clear();
    candidatesSeq_.reset();
    awaiting_ = false;
    host_.cancelFullTimer();
    cursor_ = kana_.size();
}

std::u32string PazkeyState::composingText() const {
    switch (form_) {
    case Form::Katakana: return toKatakana(kana_);
    case Form::HalfKatakana: return toHalfKatakana(kana_);
    case Form::FullAlnum: return toFullWidthAscii(rawText());
    case Form::HalfAlnum: return rawText();
    case Form::Kana: break;
    }
    std::u32string out = kana_;
    out.insert(cursor_, toFullWidthAscii(std::u32string(tail_.begin(), tail_.end())));
    return out;
}

// ---- 変換 ----

void PazkeyState::startConverting() {
    flushTail();
    clearLive();
    form_ = Form::Kana;
    mode_ = Mode::Converting;
    cursor_ = kana_.size();  // 変換範囲は読み全体から始める
    requestFull();
}

void PazkeyState::requestFull() {
    candidates_.clear();
    candidatesSeq_.reset();
    selected_ = 0;
    host_.cancelFullTimer();
    if (!host_.connected() || kana_.size() > kMaxReadingLength) {
        awaiting_ = false;
        showFallback(!host_.connected() ? "disconnected" : "too_long");
        return;
    }
    ++seq_;
    std::string context = leftContext();
    debug([&] {
        return std::format("send convert seq={} kind=full len={} cursor={} ctx={}", seq_, kana_.size(), cursor_,
                           codepoints(context));
    });
    // 読みでは数字と英字を半角にする。azooKey は読みの字種のまま候補を出すので、2026年 などを出すため。
    sendLine(convertMessage(session_, seq_, ConvertKind::Full, u8(toReadingForm(kana_)), cursor_, context));
    awaiting_ = true;
    host_.startFullTimer(host_.config().timeout.fullMs);
}

void PazkeyState::showFallback(const char *reason) {
    // この seq は期限切れにする。後から result が届いても捨てる（docs/frontend.md「非同期の扱い」）。
    awaiting_ = false;
    host_.cancelFullTimer();
    candidates_.clear();
    for (auto &text : {u8(kana_), u8(toKatakana(kana_)), u8(toHalfKatakana(kana_))}) {
        bool dup = std::any_of(candidates_.begin(), candidates_.end(), [&](const auto &c) { return c.text == text; });
        if (!dup) candidates_.push_back({text, kana_.size()});
    }
    candidatesSeq_.reset();
    candidatesDisconnected_ = !host_.connected();
    selected_ = 0;
    debug([&] { return std::format("fallback reason={} seq={} candidates={}", reason, seq_, candidates_.size()); });
}

void PazkeyState::onResult(const Incoming &msg) {
    if (mode_ == Mode::Composing) {
        const bool use = liveSeq_ && *liveSeq_ == seq_ && msg.seq == seq_;
        debug([&] {
            std::string consumed;
            for (const auto &c : msg.candidates) consumed += (consumed.empty() ? "" : ",") + std::to_string(c.consumed);
            return std::format("recv result seq={} latest={} candidates={} consumed=[{}] len={} {}",
                               msg.seq.value_or(-1), seq_, msg.candidates.size(), consumed, liveReading_.size(),
                               use ? "use(live)" : "drop(stale)");
        });
        if (!use) return;
        liveSeq_.reset();
        // 1位の候補だけを使う。読みを1文字も使わない候補や、読みより長く使う候補は表示しない。
        if (!msg.candidates.empty() && msg.candidates[0].consumed >= 1 &&
            msg.candidates[0].consumed <= liveReading_.size()) {
            lastLive_ = Live{msg.candidates[0].text, msg.candidates[0].consumed, liveReading_, seq_};
        } else {
            lastLive_.reset();
        }
        return;
    }
    debug([&] {
        std::string consumed;
        for (const auto &c : msg.candidates) consumed += (consumed.empty() ? "" : ",") + std::to_string(c.consumed);
        const char *verdict = mode_ != Mode::Converting ? "drop(mode)"
                              : !awaiting_              ? "drop(not_awaiting)"
                              : msg.seq != seq_         ? "drop(stale)"
                                                        : "use";
        return std::format("recv result seq={} latest={} candidates={} consumed=[{}] len={} {}", msg.seq.value_or(-1),
                           seq_, msg.candidates.size(), consumed, kana_.size(), verdict);
    });
    if (mode_ != Mode::Converting || !awaiting_ || msg.seq != seq_) return;  // 最新の seq でなければ捨てる
    awaiting_ = false;
    host_.cancelFullTimer();
    candidates_.clear();
    for (const auto &c : msg.candidates) {
        // 読みを1文字も使わない候補や、読みより長く使う候補は確定できないので使わない。
        if (c.consumed >= 1 && c.consumed <= kana_.size()) candidates_.push_back(c);
    }
    if (candidates_.empty()) {
        showFallback("no_usable");
        return;
    }
    candidatesSeq_ = seq_;
    candidatesDisconnected_ = false;
    selected_ = 0;
}

void PazkeyState::onError(const Incoming &msg) {
    if (mode_ == Mode::Composing) {
        const bool use = liveSeq_ && *liveSeq_ == seq_ && msg.seq == seq_;
        debug([&] {
            return std::format("recv error code={} seq={} latest={} {}", msg.code, msg.seq.value_or(-1), seq_,
                               use ? "use(live)" : "drop");
        });
        if (use) clearLive();  // かなを表示する
        return;
    }
    const bool use = mode_ == Mode::Converting && awaiting_ && msg.seq == seq_;
    debug([&] {
        return std::format("recv error code={} seq={} latest={} {}", msg.code, msg.seq.value_or(-1), seq_,
                           use ? "use" : "drop");
    });
    if (!use) return;
    showFallback("error");
}

void PazkeyState::onFullTimeout() {
    if (mode_ != Mode::Converting || !awaiting_) return;
    showFallback(host_.connected() ? "timeout" : "disconnected");
}

void PazkeyState::commitCandidateText(size_t index) {
    if (index >= candidates_.size()) return;
    const Candidate c = candidates_[index];
    commitToApp(c.text);
    sendCommit(index);
    size_t consumed = std::min(c.consumed, kana_.size());
    eraseKana(0, consumed);
    cursor_ = cursor_ > consumed ? cursor_ - consumed : 0;
    candidates_.clear();
    candidatesSeq_.reset();
    selected_ = 0;
}

void PazkeyState::commitCandidate(size_t index) {
    if (index >= candidates_.size()) return;
    commitCandidateText(index);
    if (kana_.empty()) {
        goEmpty();
        return;
    }
    // 残りを変換する。変換範囲が残っていなければ、残り全体を範囲にする。
    if (cursor_ == 0) cursor_ = kana_.size();
    requestFull();
}

void PazkeyState::selectCandidate(size_t index) {
    if (mode_ != Mode::Converting || index >= candidates_.size()) return;
    commitCandidate(index);
}

// ---- ライブ変換 ----

bool PazkeyState::liveAllowed() const { return host_.config().liveConversion.enabled && !host_.isPassword(); }

void PazkeyState::requestLive() {
    if (!liveAllowed() || mode_ != Mode::Composing || form_ != Form::Kana || kana_.empty() ||
        cursor_ != kana_.size()) {
        return;
    }
    if (!host_.connected() || kana_.size() > kMaxReadingLength) return;  // 送らず、今の表示のまま
    ++seq_;
    liveSeq_ = seq_;
    liveReading_ = kana_;
    std::string context = leftContext();
    debug([&] {
        return std::format("send convert seq={} kind=live len={} cursor={} ctx={}", seq_, kana_.size(), cursor_,
                           codepoints(context));
    });
    sendLine(convertMessage(session_, seq_, ConvertKind::Live, u8(toReadingForm(kana_)), kana_.size(), context));
}

std::optional<std::string> PazkeyState::liveDisplay() const {
    if (!lastLive_ || mode_ != Mode::Composing || !liveAllowed() || form_ != Form::Kana || cursor_ != kana_.size()) {
        return std::nullopt;
    }
    const Live &l = *lastLive_;
    if (std::u32string_view(kana_).substr(0, l.reading.size()) != l.reading) return std::nullopt;
    return l.text + u8(std::u32string_view(kana_).substr(l.consumed)) +
           u8(toFullWidthAscii(std::u32string(tail_.begin(), tail_.end())));
}

void PazkeyState::sendLiveCommit() {
    // 前の結果を表示し続けている間は、サーバーの最後の result と一致しないので学習しない。
    if (!lastLive_ || lastLive_->seq != seq_ || host_.isPassword() || !host_.connected()) return;
    debug([&] { return std::format("send commit seq={} index=0 (live)", lastLive_->seq); });
    sendLine(commitMessage(session_, lastLive_->seq, 0));
}

void PazkeyState::clearLive() {
    lastLive_.reset();
    liveSeq_.reset();
}

// ---- 入力の終わり ----

void PazkeyState::endInput(bool commitText) {
    debug([&] { return std::format("endInput mode={} commitText={}", modeName(mode_), commitText); });
    std::string text;
    switch (mode_) {
    case Mode::Empty: return;  // 入力中の文字がなければ何もしない
    case Mode::Composing:
        if (auto live = liveDisplay()) {
            text = *live;
            sendLiveCommit();
        } else {
            text = u8(composingText());
        }
        break;
    case Mode::Converting:
        if (!candidates_.empty()) {
            const Candidate &c = candidates_[selected_];
            text = c.text + u8(std::u32string_view(kana_).substr(std::min(c.consumed, kana_.size())));
            sendCommit(selected_);
        } else {
            text = u8(kana_);
        }
        break;
    }
    if (commitText) {
        commitToApp(text);
    } else if (!text.empty()) {
        debug([&] { return std::format("commit by fcitx5 len={}", codepoints(text)); });
        noteCommitted(text);  // fcitx5 が確定する
    }
    goEmpty();
}

void PazkeyState::discardInput() {
    debug([&] { return std::format("discardInput mode={}", modeName(mode_)); });
    if (mode_ != Mode::Empty) goEmpty();
}

void PazkeyState::clearContext() {
    debug([] { return std::string("clearContext"); });
    committedContext_.clear();
    pending_ = {};
}

void PazkeyState::goEmpty() {
    mode_ = Mode::Empty;
    kana_.clear();
    raw_.clear();
    tail_.clear();
    cursor_ = 0;
    form_ = Form::Kana;
    ascii_ = false;
    awaiting_ = false;
    host_.cancelFullTimer();
    candidates_.clear();
    candidatesSeq_.reset();
    candidatesDisconnected_ = false;
    selected_ = 0;
    clearLive();
    // どの経路で入力が空になっても reset を送る（docs/frontend.md「Empty へ移るとき」）。
    if (host_.connected()) debug([] { return std::string("send reset"); });
    sendLine(resetMessage(session_));
}

void PazkeyState::close() {
    if (used_ && host_.connected()) {
        debug([] { return std::string("send close"); });
        host_.send(closeMessage(session_));
    }
}

void PazkeyState::sendCommit(size_t index) {
    if (!candidatesSeq_ || host_.isPassword() || !host_.connected()) return;  // 代用の候補とパスワード欄では学習しない
    debug([&] { return std::format("send commit seq={} index={}", *candidatesSeq_, index); });
    sendLine(commitMessage(session_, *candidatesSeq_, static_cast<int64_t>(index)));
}

void PazkeyState::sendLine(const std::string &line) {
    if (!host_.connected()) return;
    used_ = true;
    host_.send(line);
}

void PazkeyState::commitToApp(const std::string &text) {
    if (text.empty()) return;  // 空の commit は送らない
    debug([&] { return std::format("commitString len={}", codepoints(text)); });
    noteCommitted(text);
    host_.commitString(text);
}

// ---- 左文脈 ----

void PazkeyState::startContext() {
    Surrounding s = host_.surrounding();
    if (!s.valid) {
        contextBase_ = committedContext_;
        debug([&] { return std::format("context source=committed len={}", codepoints(contextBase_)); });
        return;
    }
    if (pending_.active && pending_.snapshot == s) {
        // 確定した後、アプリがまだ周辺テキストを更新していない。確定した後の左文脈をそのまま使う。
        contextBase_ = pending_.context;
        debug([&] { return std::format("context source=pending len={}", codepoints(contextBase_)); });
        return;
    }
    pending_ = {};
    contextBase_.clear();
    if (auto decoded = decodeUtf8(s.text)) {
        size_t end = std::min<size_t>(std::min(s.cursor, s.anchor), decoded->size());
        contextBase_ = lastCodepoints(u8(std::u32string_view(*decoded).substr(0, end)), kCommittedContextLimit);
    }
    debug([&] {
        return std::format("context source=surrounding cursor={} anchor={} len={}", s.cursor, s.anchor,
                           codepoints(contextBase_));
    });
}

void PazkeyState::noteCommitted(const std::string &text) {
    if (host_.isPassword()) {
        // パスワード欄で確定した文字列は、左文脈に入れない（docs/frontend.md「左文脈」）。
        pending_ = {};
        return;
    }
    committedContext_ = lastCodepoints(committedContext_ + text, kCommittedContextLimit);
    contextBase_ = lastCodepoints(contextBase_ + text, kCommittedContextLimit);
    Surrounding s = host_.surrounding();
    if (s.valid) {
        pending_ = {true, s, contextBase_};
    } else {
        pending_ = {};
    }
}

std::string PazkeyState::leftContext() const {
    const auto &cfg = host_.config();
    if (!cfg.context.enabled || host_.isPassword()) return {};
    return lastCodepoints(contextBase_, static_cast<size_t>(std::max(0, cfg.context.maxChars)));
}

// ---- 表示 ----

RenderUpdate PazkeyState::render() {
    PreeditView preedit;
    PanelView panel;
    const auto &cfg = host_.config();
    switch (mode_) {
    case Mode::Empty: break;
    case Mode::Composing: {
        if (auto live = liveDisplay()) {
            preedit.segments.push_back({*live, false});
            preedit.cursorBytes = live->size();
            break;
        }
        std::u32string text = composingText();
        preedit.segments.push_back({u8(text), false});
        size_t cursor = form_ == Form::Kana ? cursor_ + tail_.size() : text.size();
        preedit.cursorBytes = u8(std::u32string_view(text).substr(0, cursor)).size();
        break;
    }
    case Mode::Converting:
        if (candidates_.empty()) {
            // 結果を待っている。かなを表示したまま、変換範囲を強調する。
            std::u32string_view k(kana_);
            if (cursor_ < kana_.size()) {
                preedit.segments.push_back({u8(k.substr(0, cursor_)), true});
                preedit.segments.push_back({u8(k.substr(cursor_)), false});
            } else {
                preedit.segments.push_back({u8(k), false});
            }
            preedit.cursorBytes = u8(k.substr(0, cursor_)).size();
        } else {
            const Candidate &c = candidates_[selected_];
            std::string rest = u8(std::u32string_view(kana_).substr(std::min(c.consumed, kana_.size())));
            if (!c.text.empty()) preedit.segments.push_back({c.text, true});
            if (!rest.empty()) preedit.segments.push_back({rest, false});
            preedit.cursorBytes = c.text.size();
            for (const auto &cand : candidates_) panel.candidates.push_back(cand.text);
            panel.selected = static_cast<int>(selected_);
            panel.pageSize = std::max(1, cfg.candidates.pageSize);
            panel.digitLabels = cfg.candidates.selectWithDigits;
            if (!candidatesSeq_ && candidatesDisconnected_) panel.aux = kDisconnectedAux;
        }
        break;
    }
    RenderUpdate update;
    update.preeditChanged = !(preedit == lastPreedit_);
    update.panelChanged = !(panel == lastPanel_);
    lastPreedit_ = preedit;
    lastPanel_ = panel;
    update.preedit = std::move(preedit);
    update.panel = std::move(panel);
    return update;
}

}  // namespace pazkey
