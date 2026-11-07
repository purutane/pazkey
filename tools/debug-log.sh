#!/bin/sh
# デバッグログ（操作と状態だけ。入力した文章は書かない）を切り替える。
# docs/frontend.md「デバッグログ」、docs/server.md「デバッグログ」。
#   tools/debug-log.sh on     # fcitx5 アドオンとサーバーの両方で書き始める
#   tools/debug-log.sh off    # やめる
#   tools/debug-log.sh show   # 両方のログを流し見する（Ctrl+C で終わる）
# どちらも、起動し直すと切れる。
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
# リポジトリにリリースビルドがあればそれを使い、なければ PATH のもの。
ctl=${PAZKEY_CTL:-$here/server/.build/release/pazkey-ctl}
[ -x "$ctl" ] || ctl=pazkey-ctl

set_rule() {
    busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1 SetLogRule s "$1"
}

case "${1:-}" in
on)
    set_rule pazkey=5 && echo "fcitx5: デバッグログを書き始めた"
    "$ctl" debug on || echo "サーバーには入れられなかった（動いていない？）"
    ;;
off)
    set_rule pazkey=4 && echo "fcitx5: デバッグログをやめた"
    "$ctl" debug off || echo "サーバーでは切れなかった（動いていない？）"
    ;;
show)
    pid=$(pgrep -x fcitx5 | head -n1 || true)
    if [ -z "$pid" ]; then
        echo "fcitx5 が動いていない" >&2
        exit 1
    fi
    # fcitx5 の出力（KDE の Wayland では KWin が起動した fcitx5 のもの）と、サーバーのユニットのログ。
    exec journalctl --user -f -o short-precise -n 0 _PID="$pid" + _SYSTEMD_USER_UNIT=pazkey-server.service
    ;;
*)
    sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
