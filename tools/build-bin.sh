#!/bin/bash
# ビルド済みのものを入れるパッケージ（fcitx5-pazkey-bin）が入れるファイルを、クリーンな chroot でビルドして
# 1つの tar にする。Release に置くのはこの tar で、-bin の PKGBUILD はこれを展開して写すだけにする。
#   tools/build-bin.sh <版> <ビルド番号> <swift-bin のパッケージ>
#   例: tools/build-bin.sh 0.2.0 1 ~/.cache/yay/swift-bin/swift-bin-6.4.2-2-x86_64.pkg.tar.zst
#
# - リポジトリ直下の PKGBUILD（HEAD のもの）を写し、llama.cpp の設定だけを、ビルドした機械の CPU 向けから
#   命令の下限を決めたもの（docs/server.md「ビルド」）に替えてビルドする。
# - ソースは GitHub のタグ v<版> から取る（PKGBUILD の source のとおり）。試しにビルドするときだけ、環境変数 PAZKEY_SOURCE で
#   pazkey の取得元を差し替えられる（例: git+file:///path/to/pazkey#commit=<コミット>）。
# - ビルドに使う Swift は AUR の swift-bin なので、公式のリポジトリからは入らない。そのパッケージを
#   makechrootpkg -I で chroot に入れる。
# - 出力は dist/<版>-<ビルド番号>/ の pazkey-<版>-<ビルド番号>-x86_64.tar.zst と、その .sha256。
#   ビルド番号は、ソースを変えずに作り直したとき（fcitx5 の ABI が変わったときなど）に上げる。
# - devtools（extra-x86_64-build）と namcap が要る。chroot を作るので sudo を求められる。
# - アップロードはしない。tar の中身を確かめてから、手で Release に置く。
set -euo pipefail

if (($# != 3)); then
    echo "使い方: $0 <版> <ビルド番号> <swift-bin のパッケージ>" >&2
    exit 2
fi
version=$1
build=$2
swift_pkg=$(realpath "$3")
here=$(cd "$(dirname "$0")/.." && pwd)

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "版は 0.2.0 の形で指定する: $version" >&2
    exit 2
fi
if [[ ! $build =~ ^[1-9][0-9]*$ ]]; then
    echo "ビルド番号は 1 以上の整数で指定する: $build" >&2
    exit 2
fi
if [[ ! -f $swift_pkg ]]; then
    echo "swift-bin のパッケージがない: $swift_pkg" >&2
    exit 1
fi
for tool in extra-x86_64-build namcap readelf python3 zstd; do
    if ! command -v "$tool" > /dev/null; then
        echo "$tool がない（devtools、namcap を入れる）" >&2
        exit 1
    fi
done
if [[ -n $(git -C "$here" status --porcelain) ]]; then
    echo "コミットしていない変更がある。HEAD の PKGBUILD を使うので、先にコミットする" >&2
    exit 1
fi

name=pazkey-$version-$build-x86_64
out=$here/dist/$version-$build
rm -rf "$out"
mkdir -p "$out/build" "$out/tree"

# PKGBUILD を写し、版、llama.cpp の設定、（試すときは）取得元を替える。置き換えは、それぞれ1か所だけのはず。
pkgbuild=$out/build/PKGBUILD
git -C "$here" show HEAD:PKGBUILD > "$pkgbuild"
git -C "$here" show HEAD:pazkey.install > "$out/build/pazkey.install"
replace() {
    local from=$1 to=$2
    if [[ $(grep -cF -- "$from" "$pkgbuild") != 1 ]]; then
        echo "PKGBUILD に「$from」がちょうど1か所ない。このスクリプトを PKGBUILD に合わせる" >&2
        exit 1
    fi
    python3 -c 'import sys; p, a, b = sys.argv[1:]; s = open(p).read(); open(p, "w").write(s.replace(a, b))' \
        "$pkgbuild" "$from" "$to"
}
replace "$(grep -m1 '^pkgver=' "$pkgbuild")" "pkgver=$version"
replace "$(grep -m1 '^pkgrel=' "$pkgbuild")" "pkgrel=1"
# 下限は SSE4.2、AVX、AVX2、FMA、F16C、BMI2。SSE4.2 は GGML_NATIVE=OFF のとき常に付く。
replace "-DGGML_NATIVE=ON" \
    "-DGGML_NATIVE=OFF -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON"
if [[ -n ${PAZKEY_SOURCE:-} ]]; then
    echo "注意: pazkey の取得元を $PAZKEY_SOURCE に差し替える（試しのビルド。Release には置かない）"
    replace 'pazkey::git+https://github.com/purutane/pazkey.git#tag=v$pkgver' "pazkey::$PAZKEY_SOURCE"
fi

# クリーンな chroot でビルドする（-c: 毎回 chroot を作り直す）。check() もここで流れる。
(cd "$out/build" && extra-x86_64-build -c -- -I "$swift_pkg")

pkg=$out/build/fcitx5-pazkey-$version-1-x86_64.pkg.tar.zst
if [[ ! -f $pkg ]]; then
    echo "パッケージができていない: $pkg" >&2
    exit 1
fi

# パッケージの中身（usr/ 以下）だけを取り出す。.PKGINFO などのメタデータは -bin の PKGBUILD が持つ。
tar -xp -C "$out/tree" -f "$pkg" usr

failed=0
ng() {
    echo "NG  $1" >&2
    failed=1
}

# llama.cpp が、決めた下限の命令だけを使うビルドになっていること（ネイティブのビルドが紛れていないこと）。
# ggml_cpu_has_* はビルドの設定を返すだけで、その命令を実行しない。
python3 - "$out/tree/usr/lib/pazkey/llama/libggml-cpu.so" << 'EOF' || failed=1
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
want = {"sse3": 1, "ssse3": 1, "avx": 1, "avx2": 1, "fma": 1, "f16c": 1, "bmi2": 1,
        "avx_vnni": 0, "avx512": 0, "avx512_vbmi": 0, "avx512_vnni": 0, "avx512_bf16": 0, "amx_int8": 0}
bad = [f"{name}={getattr(lib, 'ggml_cpu_has_' + name)()}（期待は {v}）"
       for name, v in want.items() if getattr(lib, "ggml_cpu_has_" + name)() != v]
print(("NG  llama.cpp の命令: " + "、".join(bad)) if bad else "OK  llama.cpp の命令は下限のとおり",
      file=sys.stderr if bad else sys.stdout)
sys.exit(1 if bad else 0)
EOF

# RUNPATH には、入れる場所と $ORIGIN だけが入っていること（ビルドの場所やツールチェーンの場所が残っていないこと）。
allowed=':/usr/lib/pazkey/swift:/usr/lib/pazkey/llama:$ORIGIN:'
runpath_ok=1
while IFS= read -r -d '' file; do
    if ! head -c4 "$file" | grep -q $'\x7fELF'; then
        continue
    fi
    runpath=$(readelf -d "$file" | sed -n 's/.*(R\(UN\)\?PATH).*\[\(.*\)\]/\2/p')
    IFS=: read -ra parts <<< "$runpath"
    for part in "${parts[@]}"; do
        if [[ $allowed != *":$part:"* ]]; then
            ng "RUNPATH: ${file#"$out/tree/"} に $part"
            runpath_ok=0
        fi
    done
done < <(find "$out/tree" -type f -print0)
if ((runpath_ok)); then
    echo "OK  RUNPATH は入れる場所と \$ORIGIN だけ"
fi

# namcap の指摘は止めずに表示する（直すか、直さない理由を記録するかを人が決める）。
echo "--- namcap"
namcap "$pkg" || true
echo "---"

if ((failed)); then
    echo "確認に失敗したので tar を作らない" >&2
    exit 1
fi

tar --sort=name --owner=0 --group=0 --numeric-owner -C "$out/tree" -cf - usr | zstd -19 -T0 -q -o "$out/$name.tar.zst"
(cd "$out" && sha256sum "$name.tar.zst" > "$name.tar.zst.sha256")
echo "完了: $out/$name.tar.zst"
cat "$out/$name.tar.zst.sha256"
