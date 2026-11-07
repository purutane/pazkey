#!/bin/sh
# 開発用: Zenzai ありのテストに使うモデルを vendor/zenz-v3.2-small.gguf に取得する。curl が要る。
#   tools/fetch-test-model.sh
# 取得するのは pazkey-ctl fetch-model と同じ版で、ハッシュも同じ（docs/server.md「Zenzai のモデルの取得」）。
# 利用者のデータの場所（~/.local/share/pazkey/）にはテストから触らないので、別に置く。
set -eu
cd "$(dirname "$0")/.."

# server/Sources/PazkeyCtl/FetchModel.swift の値と揃える。
MODEL_URL=https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/c67e03e07d215c869f591b274c1631170d3e11fe/ggml-model-Q5_K_M.gguf
MODEL_SHA256=29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673
dest=vendor/zenz-v3.2-small.gguf

mkdir -p vendor
if [ -e "$dest" ]; then
    echo "あり: $dest"
else
    echo "取得: $MODEL_URL"
    curl -fL --retry 3 -o "$dest.part" "$MODEL_URL"
    mv "$dest.part" "$dest"
fi
echo "$MODEL_SHA256  $dest" | sha256sum -c --quiet || { echo "ハッシュが一致しない: $dest" >&2; exit 1; }
echo "完了: $dest"
