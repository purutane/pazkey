// 設定ファイルをフロントエンドの規則で検証する。tests/config/check.py が使う。
// 使い方: pazkey-check-config <file>   正しければ終了コード 0、不正なら 1。
#include <cstdio>

#include "config.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "使い方: %s <config.json>\n", argv[0]);
        return 2;
    }
    auto result = pazkey::loadConfig(argv[1]);
    for (const auto &e : result.errors) std::printf("error %s\n", e.describe().c_str());
    for (const auto &k : result.unknownKeys) std::printf("unknown %s\n", k.c_str());
    return result.config ? 0 : 1;
}
