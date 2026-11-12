#include "zenzai_status.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

namespace pazkey {

std::string zenzaiStatusText(const ZenzaiStatusInput &in) {
    if (in.fetching) return "ダウンロード中";
    if (!in.connected) return "変換サーバーに接続していません";
    if (in.state == "enabled") {
        // 反映で読み込めず、前のモデルのまま動いている（docs/server.md「Zenzai のモデルの読み込み」）
        if (in.customModel && !in.modelExists) return "使用中（指定したモデルがないため、前のモデルを使用）";
        return "使用中";
    }
    if (in.state == "disabled") return "無効";
    if (in.state == "fallback") return "一時停止中";
    if (in.state == "unsupported") return "搭載の CPU をサポートしません";
    if (in.state == "unavailable") {
        if (in.modelExists) return "モデルを読み込めません";
        // 自分で指したモデルがないときは、ダウンロードしても直らない
        return in.customModel ? "指定したモデルが見つかりません" : "モデルがありません（ダウンロードできます）";
    }
    return "不明";
}

std::string defaultModelPath() {
    const char *xdg = std::getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && xdg[0] == '/') {
        base = xdg;
    } else {
        const char *home = std::getenv("HOME");
        base = std::string(home ? home : "") + "/.local/share";
    }
    return base + "/pazkey/zenz.gguf";
}

std::string effectiveModelPath(const std::string &modelPath) {
    return modelPath.empty() ? defaultModelPath() : modelPath;
}

bool modelFetching() {
    int fd = ::open((defaultModelPath() + ".lock").c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool locked = ::flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    ::close(fd);  // 取れたロックは閉じれば外れる
    return locked;
}

}  // namespace pazkey
