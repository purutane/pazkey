// 設定画面に出す Zenzai の状態（docs/frontend.md「設定画面」）。
#pragma once

#include <string>

namespace pazkey {

struct ZenzaiStatusInput {
    bool fetching = false;   // pazkey-ctl fetch-model が取得している
    bool connected = false;  // サーバーにつながっている
    std::string state;       // サーバーから最後に受けた zenzai（hello、config_changed）
    bool modelExists = false;  // 使うモデルのファイルがある
    bool customModel = false;  // zenzai.model_path で自分で用意したモデルを指している
};

/// 状態の文言（「使用中」など）。
std::string zenzaiStatusText(const ZenzaiStatusInput &in);

/// `zenzai.model_path` が空のときに使うモデル: `$XDG_DATA_HOME/pazkey/zenz.gguf`（既定は `~/.local/share/...`）。
std::string defaultModelPath();

/// 使うモデルのパス（model_path が空なら既定の場所）。
std::string effectiveModelPath(const std::string &modelPath);

/// 既定の場所へ取得しているあいだ（pazkey-ctl が `zenz.gguf.lock` をロックしている）なら真。待たない。
bool modelFetching();

}  // namespace pazkey
