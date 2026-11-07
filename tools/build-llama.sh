#!/bin/sh
# 開発用: サーバーのビルドに要る llama.cpp を、ソースから vendor/llama/ にビルドする。
# 版と設定は PKGBUILD と同じにする（docs/server.md「依存」「ビルド」）。git、cmake、C++ コンパイラーが要る。
#   tools/build-llama.sh
# 取得物とビルドの途中のものは vendor/ に置き、Git には入れない（.gitignore）。
set -eu
cd "$(dirname "$0")/.."

# fkunn1326 版の b4846。タグは付け替えられるので、コミットで固定する。
LLAMA_REPO=https://github.com/fkunn1326/llama.cpp.git
LLAMA_COMMIT=10131b23ee0dbd9d03dfc618cec9fe3402919277

src=vendor/llama-src
dest=$PWD/vendor/llama
stamp=$dest/.commit

if [ -e "$stamp" ] && [ "$(cat "$stamp")" = "$LLAMA_COMMIT" ] && [ -e "$dest/lib/libllama.so" ]; then
    echo "あり: $dest（$LLAMA_COMMIT）"
    exit 0
fi

if [ ! -d "$src/.git" ]; then
    rm -rf "$src"
    git init -q "$src"
    git -C "$src" remote add origin "$LLAMA_REPO"
fi
if [ "$(git -C "$src" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$LLAMA_COMMIT" ]; then
    echo "取得: $LLAMA_REPO $LLAMA_COMMIT"
    git -C "$src" fetch -q --depth 1 origin "$LLAMA_COMMIT"
    git -C "$src" checkout -q --detach FETCH_HEAD
fi

# CPU 版のライブラリだけを作る。GGML_NATIVE は既定（ON）のままで、この機械の CPU 向けに最適化する。
# RUNPATH を $ORIGIN にして、libllama.so の依存（libggml*.so）が同じ場所から見つかるようにする。
rm -rf "$src/build" "$dest"
cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_CURL=OFF \
    '-DCMAKE_INSTALL_RPATH=$ORIGIN' -DCMAKE_INSTALL_PREFIX="$dest" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$src/build" -j"$(nproc)"
cmake --install "$src/build"
echo "$LLAMA_COMMIT" > "$stamp"
echo "完了: $dest/lib"
