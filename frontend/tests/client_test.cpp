// ServerClient のテスト。fcitx5 のイベントループを実際に回し、同じスレッドで偽のサーバーを動かす。
// 確かめること: 接続を待たない、つながらなければ再試行する、切断の前の行を送り直さない、1行ずつ届く。
#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>

#include <fcitx-utils/event.h>

#include "client.h"

using namespace pazkey;

namespace {

class Listener {
public:
    explicit Listener(const std::string &path) {
        fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        ::unlink(path.c_str());
        EXPECT_EQ(::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
        EXPECT_EQ(::listen(fd, 4), 0);
    }
    ~Listener() {
        if (conn >= 0) ::close(conn);
        ::close(fd);
    }
    bool acceptOne() {
        if (conn >= 0) return true;
        conn = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        return conn >= 0;
    }
    std::string readAll() {
        char buf[4096];
        while (conn >= 0) {
            ssize_t n = ::recv(conn, buf, sizeof buf, 0);
            if (n <= 0) break;
            received.append(buf, n);
        }
        return received;
    }
    void write(const std::string &s) { ASSERT_EQ(::send(conn, s.data(), s.size(), MSG_NOSIGNAL), (ssize_t)s.size()); }
    void drop() {
        ::close(conn);
        conn = -1;
        received.clear();
    }
    int fd = -1;
    int conn = -1;
    std::string received;
};

struct Step {
    std::function<bool()> until;           // これが真になったら
    std::function<void()> then = nullptr;  // これを行って次へ進む
};

/// 手順を順に進めながらイベントループを回す。すべて済めば真、期限が来たら偽。
/// sd-event のループは一度 exit すると再開できないので、1回の exec の中で全部を行う。
bool runSteps(fcitx::EventLoop &loop, std::vector<Step> steps, int timeoutMs = 5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t i = 0;
    auto ticker = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
                                    [&](fcitx::EventSourceTime *src, uint64_t) {
                                        while (i < steps.size() && steps[i].until()) {
                                            if (steps[i].then) steps[i].then();
                                            ++i;
                                        }
                                        if (i == steps.size() || std::chrono::steady_clock::now() > deadline) {
                                            loop.exit();
                                            return true;
                                        }
                                        src->setNextInterval(10 * 1000);
                                        src->setOneShot();
                                        return true;
                                    });
    loop.exec();
    return i == steps.size();
}

class ClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = std::filesystem::temp_directory_path() / ("pazkey-client-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        path = (dir / "server.sock").string();
    }
    void TearDown() override { std::filesystem::remove_all(dir); }

    std::filesystem::path dir;
    std::string path;
    fcitx::EventLoop loop;
    int connects = 0;
    int disconnects = 0;
    std::vector<Incoming> messages;

    std::unique_ptr<ServerClient> makeClient() {
        return std::make_unique<ServerClient>(loop, path,
                                              ServerClient::Callbacks{
                                                  [this] { ++connects; },
                                                  [this] { ++disconnects; },
                                                  [this](const Incoming &m) { messages.push_back(m); },
                                              });
    }
};

}  // namespace

TEST_F(ClientTest, RetriesUntilServerAppears) {
    auto client = makeClient();
    auto t0 = std::chrono::steady_clock::now();
    client->start();
    // つながらなくても、start はすぐ戻る。
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(50));
    EXPECT_FALSE(client->connected());
    // 送っても、つながっていなければ捨てる。
    client->send("{\"type\":\"reset\",\"session\":1}\n");

    std::unique_ptr<Listener> server;
    ASSERT_TRUE(runSteps(loop, {
        {[] { return true; }, [&] { server = std::make_unique<Listener>(path); }},
        {[&] { return client->connected(); }},
        {[&] { return server->acceptOne() && server->readAll().find('\n') != std::string::npos; }},
    }));
    // 最初の行は hello。つながる前に送ろうとした reset は届かない。
    EXPECT_EQ(server->received.rfind("{\"type\":\"hello\"", 0), 0u);
    EXPECT_EQ(server->received.find("reset"), std::string::npos);
    EXPECT_EQ(connects, 1);
}

TEST_F(ClientTest, DeliversLinesAndReconnectsWithoutResending) {
    Listener server(path);
    auto client = makeClient();
    client->start();
    ASSERT_TRUE(runSteps(loop, {
        {[&] { return client->connected() && server.acceptOne(); },
         [&] {
             server.write("{\"type\":\"result\",\"session\":1,\"seq\":1,"
                          "\"candidates\":[{\"text\":\"機\",\"consumed\":1}]}\n{\"type\":");
         }},
        {[&] { return messages.size() == 1; },
         [&] { server.write("\"config_changed\",\"config_generation\":2}\n"); }},
        {[&] { return messages.size() == 2; }, [&] { server.drop(); }},  // サーバーが落ちる
        {[&] { return disconnects == 1; },
         [&] { client->send("{\"type\":\"convert\",\"lost\":true}\n"); }},  // 切断中に送ろうとしたもの
        {[&] { return client->connected() && server.acceptOne(); }},
        {[&] { return server.readAll().find('\n') != std::string::npos; }},
    }));
    EXPECT_EQ(messages[0].candidates[0].text, "機");
    EXPECT_EQ(messages[1].type, "config_changed");
    EXPECT_EQ(server.received.rfind("{\"type\":\"hello\"", 0), 0u);
    EXPECT_EQ(server.received.find("lost"), std::string::npos);
    EXPECT_EQ(connects, 2);
}

TEST_F(ClientTest, ClosesOnOverlongLine) {
    Listener server(path);
    auto client = makeClient();
    client->start();
    std::string chunk(65536, 'x');
    ASSERT_TRUE(runSteps(loop, {
        {[&] { return client->connected() && server.acceptOne(); }},
        {[&] {
            // 改行のない行を 1 MiB を超えるまで送り続ける。受け取った側が閉じる。
            server.readAll();
            while (::send(server.conn, chunk.data(), chunk.size(), MSG_NOSIGNAL | MSG_DONTWAIT) > 0) {
            }
            return disconnects >= 1;
        }},
    }));
}

TEST_F(ClientTest, StopPreventsRetry) {
    auto client = makeClient();
    client->start();
    client->stop();
    Listener server(path);
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    EXPECT_FALSE(runSteps(loop, {
        {[&] { return client->connected() || std::chrono::steady_clock::now() > until; }},
        {[&] { return client->connected(); }},
    }, 600));
    EXPECT_FALSE(client->connected());
}
