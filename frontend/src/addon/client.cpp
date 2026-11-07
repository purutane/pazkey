#include "client.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/log.h>

namespace pazkey {

namespace {
constexpr uint64_t kRetryMinMs = 100;
constexpr uint64_t kRetryMaxMs = 2000;
}  // namespace

ServerClient::ServerClient(fcitx::EventLoop &loop, std::string socketPath, Callbacks callbacks)
    : loop_(loop), path_(std::move(socketPath)), callbacks_(std::move(callbacks)) {}

ServerClient::~ServerClient() {
    retryTimer_.reset();
    closeSocket();
    io_.reset();
}

std::string ServerClient::defaultSocketPath() {
    const char *dir = std::getenv("XDG_RUNTIME_DIR");
    std::string base = dir && dir[0] == '/' ? dir : "/run/user/" + std::to_string(::getuid());
    return base + "/pazkey/server.sock";
}

void ServerClient::start() {
    if (state_ == State::Idle) tryConnect();
}

void ServerClient::stop() {
    bool wasConnected = connected();
    if (retryTimer_) retryTimer_->setEnabled(false);
    closeSocket();
    state_ = State::Stopped;
    if (wasConnected && callbacks_.disconnected) callbacks_.disconnected();
}

// イベントソースは一度作ったら使い回し、壊すのはデストラクタだけにする。
// 自分のコールバックの中でイベントソースを壊すと、実行中の関数オブジェクトが消えるからである。
void ServerClient::watch(fcitx::IOEventFlags flags) {
    if (!io_) {
        io_ = loop_.addIOEvent(fd_, flags,
                               [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags revents) { return onIO(revents); });
        return;
    }
    io_->setFd(fd_);
    io_->setEvents(flags);
    io_->setEnabled(true);
}

void ServerClient::closeSocket() {
    if (io_) io_->setEnabled(false);  // fd を閉じる前に監視を外す
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    readBuf_.clear();
    writeBuf_.clear();
    writeFailed_ = false;
}

void ServerClient::tryConnect() {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path_.size() >= sizeof(addr.sun_path)) {
        FCITX_ERROR() << "pazkey: ソケットのパスが長すぎる: " << path_;
        state_ = State::Stopped;
        return;
    }
    std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        scheduleRetry();
        return;
    }
    int r = ::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (r == 0) {
        onConnected();
        return;
    }
    if (errno == EINPROGRESS || errno == EAGAIN) {
        // 接続の完了は書けるようになったことで知る。ここでは待たない。
        state_ = State::Connecting;
        watch(fcitx::IOEventFlag::Out);
        return;
    }
    closeSocket();
    scheduleRetry();
}

void ServerClient::onConnected() {
    state_ = State::Connected;
    retryMs_ = kRetryMinMs;
    if (retryTimer_) retryTimer_->setEnabled(false);
    readBuf_.clear();
    writeBuf_.clear();
    watch(fcitx::IOEventFlag::In);
    send(helloMessage(PAZKEY_VERSION));
    if (state_ == State::Connected && callbacks_.connected) callbacks_.connected();
}

void ServerClient::scheduleRetry() {
    state_ = State::Idle;
    uint64_t at = fcitx::now(CLOCK_MONOTONIC) + retryMs_ * 1000;
    retryMs_ = std::min(retryMs_ * 2, kRetryMaxMs);
    if (!retryTimer_) {
        retryTimer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, at, 0, [this](fcitx::EventSourceTime *, uint64_t) {
            if (state_ == State::Idle) tryConnect();
            return true;
        });
        return;
    }
    retryTimer_->setTime(at);
    retryTimer_->setOneShot();
}

void ServerClient::disconnect() {
    if (state_ != State::Connected && state_ != State::Connecting) return;
    bool wasConnected = connected();
    // 切断の前に送れなかった行は送り直さない（落ちる原因になった入力を送り直さないため）。
    closeSocket();
    state_ = State::Idle;
    if (wasConnected && callbacks_.disconnected) callbacks_.disconnected();
    if (state_ == State::Idle) scheduleRetry();
}

void ServerClient::send(const std::string &line) {
    if (!connected() || writeFailed_) return;
    writeBuf_ += line;
    if (!flushWrite()) {
        // ここで切断すると、呼び出し元（キー処理の途中の PazkeyState）の中から disconnected が呼ばれる。
        // 切断はイベントループに戻ってから、onIO で行う。
        writeFailed_ = true;
        writeBuf_.clear();
        if (io_) io_->setEvents(fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Out});
        return;
    }
    updateIOEvents();
}

bool ServerClient::flushWrite() {
    while (!writeBuf_.empty()) {
        ssize_t n = ::send(fd_, writeBuf_.data(), writeBuf_.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            writeBuf_.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;  // 書けるようになったら続きを送る
        return false;
    }
    return true;
}

void ServerClient::updateIOEvents() {
    if (!io_ || !connected()) return;
    fcitx::IOEventFlags flags = fcitx::IOEventFlag::In;
    if (!writeBuf_.empty()) flags |= fcitx::IOEventFlag::Out;
    if (io_->events() == flags) return;
    try {
        io_->setEvents(flags);
    } catch (const fcitx::EventLoopException &e) {
        // fcitx5 の終了時（イベントループが終わった後に入力欄が破棄され、close を送るとき）に起きる。
        // 送れなくても害はないので、例外を外へ出さない（デストラクタから呼ばれるため）。
        FCITX_DEBUG() << "pazkey: イベントループが使えない: " << e.what();
    }
}

bool ServerClient::onIO(fcitx::IOEventFlags flags) {
    if (state_ == State::Connecting) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            disconnect();
            return true;
        }
        onConnected();
        return true;
    }
    if (state_ != State::Connected) return true;
    if (writeFailed_) {
        disconnect();
        return true;
    }
    if (flags & fcitx::IOEventFlag::In || flags & fcitx::IOEventFlag::Hup || flags & fcitx::IOEventFlag::Err) {
        if (!readAvailable()) {
            disconnect();
            return true;
        }
    }
    if (state_ == State::Connected && (flags & fcitx::IOEventFlag::Out)) {
        if (!flushWrite()) {
            disconnect();
            return true;
        }
        updateIOEvents();
    }
    return true;
}

bool ServerClient::readAvailable() {
    char buf[65536];
    while (true) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) return false;  // サーバーが閉じた
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        }
        readBuf_.append(buf, static_cast<size_t>(n));
        size_t pos;
        while ((pos = readBuf_.find('\n')) != std::string::npos) {
            std::string line = readBuf_.substr(0, pos);
            readBuf_.erase(0, pos + 1);
            if (auto msg = parseIncoming(line)) {
                if (callbacks_.message) callbacks_.message(*msg);
                if (state_ != State::Connected) return true;  // コールバックの中で stop された
            }
        }
        if (readBuf_.size() > kMaxLineBytes) return false;  // 1行の上限を超えたら閉じる
    }
    return true;
}

}  // namespace pazkey
