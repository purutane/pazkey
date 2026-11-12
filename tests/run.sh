#!/bin/sh
# 自動で確かめられるテストをすべて実行する。
# フロントエンドの単体テスト、設定の判定、サーバーのテスト。fcitx5 とアプリを使う確認は手で行う（tests/frontend/README.md）。
#   tests/run.sh
set -u
cd "$(dirname "$0")/.."
(cd server && swift build -c release) || exit 1
(cmake -S frontend -B frontend/build >/dev/null && cmake --build frontend/build -j) || exit 1
failed=0
run() {
    echo "== $*"
    "$@" || failed=$((failed + 1))
}
run frontend/build/pazkey-tests --gtest_brief=1    # 入力状態、ローマ字、JSON、設定
run frontend/build/pazkey-client-tests --gtest_brief=1  # サーバーへの接続
run frontend/build/pazkey-fcitx-config-tests --gtest_brief=1  # 設定画面とのやりとり
run python -I tests/check_versions.py             # 版の文字列がそろっている
run python -I tests/config/check.py                # 設定の判定がスキーマ、サーバー、フロントエンドで一致する
run python tests/server/test_learning.py           # 学習がその場で反映される
run python tests/server/test_restart.py            # 落ちても続けられる（systemd --user を使う）
run python tests/server/test_coalesce.py           # 古い要求は捨てられる
run python tests/server/test_config_reload.py      # 不正な設定で止まらない
run python tests/server/test_shutdown.py           # SIGTERM で学習を保存する
run python tests/server/test_memory_crash.py       # 学習データの書き込みの途中で落ちても壊れない
run python tests/server/test_reading_limit.py       # 読みの長さの上限
run python tests/server/test_debug_log.py          # デバッグログに中身を書かない
run python tests/server/test_zenzai.py             # Zenzai のモデルの読み込み（tools/fetch-test-model.sh が要る）
run python tests/server/test_fallback.py           # 退避モード（tools/fetch-test-model.sh が要る）
run python tests/server/test_fetch_model.py        # モデルの取得
# サーバーのテストを Zenzai ありでもう一度実行する。モデルがなければ飛ばす。
model=vendor/zenz-v3.2-small.gguf
if [ -e "$model" ]; then
    export PAZKEY_TEST_ZENZAI="$PWD/$model"
    echo "==== ここから Zenzai あり（$model）"
    for t in test_learning test_restart test_coalesce test_config_reload test_shutdown test_memory_crash test_reading_limit test_debug_log; do
        run python tests/server/$t.py              # Zenzai あり
    done
    unset PAZKEY_TEST_ZENZAI
else
    echo "== Zenzai ありのテストを飛ばした（$model がない。tools/fetch-test-model.sh で取得する）"
fi
echo
if [ "$failed" -eq 0 ]; then echo "すべて通った"; else echo "$failed 件が失敗した"; fi
exit "$failed"
