# Maintainer: purutane <kopa1986 at gmail dot com>
# 入れる場所とビルドの設定は docs/server.md「ビルド」「入れる場所」に従う。

pkgname=fcitx5-pazkey
pkgver=0.2.0
pkgrel=1
pkgdesc='Japanese input method for fcitx5 using azooKey'
arch=('x86_64')
url='https://github.com/purutane/pazkey'
# pazkey は MIT。残りは同梱するもの（THIRD_PARTY_NOTICES）のライセンス。
license=('MIT' 'Apache-2.0' 'Apache-2.0 WITH Swift-exception' 'BSD-2-Clause' 'BSD-3-Clause'
         'Unicode-3.0' 'ICU' 'NAIST-2003')
depends=('fcitx5' 'systemd>=254' 'curl' 'glibc' 'libgcc' 'libstdc++' 'libgomp' 'hicolor-icon-theme')
# ビルド済みのものを入れる fcitx5-pazkey-bin と同じファイルを入れる。
conflicts=('fcitx5-pazkey-bin')
makedepends=('swift-language' 'cmake' 'git' 'gtest')
# llama.cpp は fkunn1326 版の b4846。タグは付け替えられるので、コミットで固定する（tools/build-llama.sh と同じ）。
_llama_commit=10131b23ee0dbd9d03dfc618cec9fe3402919277
source=("pazkey::git+https://github.com/purutane/pazkey.git#tag=v$pkgver"
        "llama.cpp::git+https://github.com/fkunn1326/llama.cpp.git#commit=$_llama_commit")
sha256sums=('SKIP'
            'SKIP')

# swift build / swift package を呼ぶ。SwiftPM の取得物と設定は、利用者のホームではなく $srcdir に置く。
_swift() {
    local sub=$1
    shift
    swift "$sub" --scratch-path "$srcdir/swift-build" --cache-path "$srcdir/swiftpm/cache" \
        --config-path "$srcdir/swiftpm/config" --security-path "$srcdir/swiftpm/security" "$@"
}

prepare() {
    # SwiftPM の依存は、Package.resolved のとおりにここで取得する。build() ではネットワークに出ない。
    cd pazkey/server
    _swift package --force-resolved-versions resolve
}

build() {
    # llama.cpp: CPU 版のライブラリだけを作る（設定は tools/build-llama.sh と同じ）。
    # ビルドする機械の CPU 向けに最適化する。GGML_NATIVE は明示する。makepkg が SOURCE_DATE_EPOCH を設定し、
    # ggml はそれを見ると既定を OFF にし、AVX などの命令も使わなくなる（Zenzai が約4倍遅くなる）。
    cmake -S llama.cpp -B build-llama -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_CURL=OFF \
        -DGGML_NATIVE=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN' -DCMAKE_INSTALL_PREFIX="$srcdir/llama" -DCMAKE_INSTALL_LIBDIR=lib
    cmake --build build-llama
    cmake --install build-llama

    # サーバーと pazkey-ctl。Swift のランタイムは /usr/lib/pazkey/swift/ に同梱するので、
    # ツールチェーンの場所と $ORIGIN を RUNPATH に入れず、同梱する場所を入れる。
    # SwiftPM は makepkg の CXXFLAGS と LDFLAGS を読まないので、要るものはここで渡す。
    # -fmacro-prefix-map: marisa-trie が例外のメッセージに埋める __FILE__ から $srcdir を除く。
    # -z now: FULL RELRO にする（makepkg の LDFLAGS と同じ）。
    cd pazkey/server
    local product
    for product in pazkey-server pazkey-ctl; do
        PAZKEY_LLAMA_DIR="$srcdir/llama/lib" PAZKEY_LLAMA_RPATH=/usr/lib/pazkey/llama \
            _swift build -c release --product "$product" \
            --disable-automatic-resolution --force-resolved-versions --disable-local-rpath \
            -Xswiftc -no-toolchain-stdlib-rpath -Xlinker -rpath -Xlinker /usr/lib/pazkey/swift \
            -Xcxx "-fmacro-prefix-map=$srcdir/=" -Xlinker -z -Xlinker now
    done
    cd "$srcdir"

    # fcitx5 のアドオン
    cmake -S pazkey/frontend -B build-frontend -DCMAKE_BUILD_TYPE=None -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build-frontend
}

check() {
    ctest --test-dir build-frontend --output-on-failure
}

package() {
    DESTDIR="$pkgdir" cmake --install build-frontend

    local bindir
    bindir=$(cd pazkey/server && _swift build -c release --show-bin-path)
    local libdir="$pkgdir/usr/lib/pazkey"

    # サーバーと辞書の bundle は同じ場所に置く（bundle は実行ファイルの隣から探される）。
    install -Dm755 "$bindir/pazkey-server" "$libdir/pazkey-server"
    cp -r --no-preserve=ownership \
        "$bindir/AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary.bundle" "$libdir/"
    install -Dm755 "$bindir/pazkey-ctl" "$pkgdir/usr/bin/pazkey-ctl"

    # llama.cpp のライブラリ
    install -d "$libdir/llama"
    install -m755 "$srcdir"/llama/lib/lib{llama,ggml,ggml-base,ggml-cpu}.so "$libdir/llama/"

    # Swift のランタイム: 2つの実行ファイルが使うものを、ツールチェーンの場所からたどって写す。
    local swiftlib
    swiftlib=$(dirname "$(readlink -f "$(command -v swiftc)")")/../lib/swift/linux
    install -d "$libdir/swift"
    local queue=("$libdir/pazkey-server" "$pkgdir/usr/bin/pazkey-ctl") file lib
    while ((${#queue[@]})); do
        file=${queue[0]}
        queue=("${queue[@]:1}")
        for lib in $(readelf -d "$file" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
            if [[ -e "$swiftlib/$lib" && ! -e "$libdir/swift/$lib" ]]; then
                install -m755 "$swiftlib/$lib" "$libdir/swift/$lib"
                queue+=("$libdir/swift/$lib")
            fi
        done
    done

    # systemd のユニット。ソケットは、パッケージが持つリンクで既定で有効にする。
    local unitdir="$pkgdir/usr/lib/systemd/user"
    install -Dm644 -t "$unitdir" pazkey/server/systemd/pazkey-{server.socket,server.service,fetch-model.service}
    install -d "$unitdir/sockets.target.wants"
    ln -s ../pazkey-server.socket "$unitdir/sockets.target.wants/pazkey-server.socket"

    install -Dm644 -t "$pkgdir/usr/share/licenses/$pkgname" pazkey/LICENSE pazkey/THIRD_PARTY_NOTICES
}
