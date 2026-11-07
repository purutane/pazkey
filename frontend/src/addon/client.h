// サーバーへの接続（docs/frontend.md「サーバーとの接続」）。
// ソケットはノンブロッキングで扱い、fcitx5 のイベントループで読み書きする。キー処理の中で待たない（不変条件1）。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>

#include "protocol.h"

namespace pazkey {

class ServerClient {
public:
    struct Callbacks {
        std::function<void()> connected;
        std::function<void()> disconnected;
        std::function<void(const Incoming &)> message;
    };

    ServerClient(fcitx::EventLoop &loop, std::string socketPath, Callbacks callbacks);
    ~ServerClient();
    ServerClient(const ServerClient &) = delete;
    ServerClient &operator=(const ServerClient &) = delete;

    /// 接続を試み始める。失敗したらタイマーで再試行する。
    void start();
    /// 再試行をやめて切断する（protocol_mismatch のとき）。
    void stop();
    bool connected() const { return state_ == State::Connected; }
    /// 1行を送る。送り切れない分はバッファにためて、書けるようになったら送る。
    void send(const std::string &line);

    /// `$XDG_RUNTIME_DIR/pazkey/server.sock`。
    static std::string defaultSocketPath();

private:
    enum class State { Idle, Connecting, Connected, Stopped };

    void tryConnect();
    void onConnected();
    void scheduleRetry();
    void disconnect();
    void watch(fcitx::IOEventFlags flags);
    void closeSocket();
    bool onIO(fcitx::IOEventFlags flags);
    bool readAvailable();
    bool flushWrite();
    void updateIOEvents();

    fcitx::EventLoop &loop_;
    std::string path_;
    Callbacks callbacks_;
    State state_ = State::Idle;
    int fd_ = -1;
    std::unique_ptr<fcitx::EventSourceIO> io_;
    std::unique_ptr<fcitx::EventSourceTime> retryTimer_;
    uint64_t retryMs_ = 100;
    std::string readBuf_;
    std::string writeBuf_;
    bool writeFailed_ = false;  // 書けなかった。次の onIO で切断する
};

}  // namespace pazkey
