# fcitx5 アドオンを手で確かめる手順

fcitx5 と実際のアプリを使うので、自動のテストにできない確認の手順。サーバーが止まったり固まったりしても入力を続けられること、アプリの入力内容を消さないこと、左文脈を正しく送ることを確かめる。

## 準備

pazkey を入れ、ソケットのユニットが動いている状態にする（`systemctl --user start pazkey-server.socket`）。4、5 はこのユニットを止めたり起こしたりする。

動いている fcitx5 は、一度読み込んだアドオンを持ち続ける。直したアドオンを入れ直したときは、fcitx5 を起動し直す。KDE Plasma の Wayland では fcitx5 を KWin が起動しているので、`fcitx5 -r` では入れ替わらない。「システム設定 → キーボード → 仮想キーボード」で「なし」にして適用し、「Fcitx 5」に戻して適用する。

fcitx5-configtool で入力メソッドに「pazkey」を足す。比べるときは Mozc に切り替える。

自動のテスト（単体テスト、接続のテスト、設定の判定）は `tests/run.sh` に入っている。手で確かめる前に通しておく。

アドオンの記録は fcitx5 のログで見られる。KWin が起動している場合は `journalctl --user -t kwin_wayland_wrapper | grep pazkey`。pazkey が書くのは、警告と誤り、通知を送ったこと（「pazkey: 通知を送る」）だけである。

## 1. LibreOffice Calc

1. Calc で、文字の入ったセルを選択する（編集状態にはしない）。
2. Ctrl+Tab（または Alt+Tab）で別のウィンドウへ移り、戻る。何度か繰り返す。
3. IME のオンとオフ（全角/半角キーなど）を何度か繰り返す。

通る条件: セルの内容が消えない。

## 2. Wayland ネイティブのアプリ

GTK4 のアプリ（gnome-text-editor など）か、Qt の Wayland ネイティブのアプリで行う。

1. テキストを入力し、一部を選択する。
2. IME を切り替える（オン/オフ、別の入力メソッドへ）。

通る条件: 選択したテキストが消えない。

## 3. Electron / ブラウザの入力欄

Chromium 系（Chrome、VS Code、Discord など）と Firefox の両方で行う。

1. 文字の入った入力欄にフォーカスを移す。
2. 何も入力していない状態で Ctrl+A を押す。

通る条件: 入力内容が消えない（全選択になるだけ）。

## 4. サーバーが止まっていても入力できる

```sh
systemctl --user stop pazkey-server.socket pazkey-server.service
```

1. ひらがなを入力する。
2. Space を押す。代用の候補（ひらがな、カタカナ、半角カタカナ）がすぐ出て、候補ウィンドウに「サーバー未接続」と出る。
3. 30秒ほどそのままにすると、「変換サーバーにつながらない」の通知が1回だけ出る。見落としたときは、KDE の通知の履歴（タスクトレイのベル）か、fcitx5 のログの「pazkey: 通知を送る」で確かめる。

```sh
systemctl --user start pazkey-server.socket
```

4. 入力して Space を押すと、変換の候補が出る（2秒以内に再接続する）。

## 5. サーバーが固まっても入力できる

```sh
systemctl --user start pazkey-server.socket
pazkey-ctl status            # 動いていることを確かめる（サーバーが起動する）
kill -STOP "$(systemctl --user show -p MainPID --value pazkey-server.service)"
```

1. ひらがなを入力できる（キーの反応が遅れない）。
2. Space を押すと、`timeout.full_ms`（既定 1000ms）の後に代用の候補が出る。「サーバー未接続」は出ない（つながってはいる）。

```sh
kill -CONT "$(systemctl --user show -p MainPID --value pazkey-server.service)"
```

3. 入力し直して Space を押すと、変換の候補が出る。

## 6〜8. 記録用の偽サーバーで確かめる

本物のサーバーは `commit` や文脈の中身をログに書かないので、受け取った行をそのまま書き出す偽サーバーを、同じソケットで動かす。

```sh
systemctl --user stop pazkey-server.socket pazkey-server.service
python tests/frontend/fake_server.py --log /tmp/pazkey-fake.log
```

アドオンは 2秒以内に偽サーバーへつなぎ直す。偽サーバーは、本物のサーバーと同じく `config.json` が変わったら `config_changed` を送るので、設定を変えても fcitx5 を起動し直さなくてよい。偽サーバーの候補は、0番が変換範囲全体のカタカナ、1番が範囲の先頭2文字だけのカタカナ（`consumed` = 2）、最後がひらがなである。終わったら Ctrl+C で止め、`systemctl --user start pazkey-server.socket` で戻す。

### 6. パスワード欄

fcitx5 は既定の設定で、パスワード欄では入力メソッドを使わない（全体の設定の「パスワード欄で入力メソッドを使う」が既定でオフ。`instance.cpp:133`）。そのため既定では pazkey は呼ばれず、`commit` も送られない。pazkey 側の処理まで確かめるときは、この設定を一時的にオンにして、ブラウザのログイン画面などのパスワード欄で、ローマ字を入力して Space、Enter で確定する。終わったら設定をオフに戻す。

通る条件: 記録に `"type":"commit"` の行がない。`convert` の `left_context` が空。

### 7. 文字数の単位

周辺テキストを渡すアプリ（KWrite、Firefox など）で行う。

1. 次の文章を書く（ZWJ でつないだ絵文字、結合文字、国旗、é、サロゲートペアになる漢字を含む）。

    ```
    👨‍👩‍👧 が゙ 🇯🇵 é 𠮷野家
    ```

2. 上限なしの確認: 文章の後ろにカーソルを置き、「あいうえお」と入力して Space を押す。記録の `left_context` が文章全体（「…𠮷野家」で終わり、「あいうえお」を含まない）で、長さが合っていること。
3. 切れ目の確認: `config.json`（`$XDG_CONFIG_HOME/pazkey/config.json`。既定は `~/.config/pazkey/config.json`。なければ作る）を次の内容で保存する（偽サーバーの記録に「config_changed を送る」と出る）。

    ```json
    { "context": { "max_chars": 3 } }
    ```

    文章を「👨‍👩‍👧」の直後まで消してから（ZWJ の途中で切れるように）、もう一度入力して Space を押す。
4. `config.json` を元に戻す（ファイルがなかったなら消す）。

通る条件: 3 の記録の `left_context` の行が「長さ 3 コードポイント」以下で、偽サーバーが「不正な UTF-8」や「JSON として読めない」を出さない。単位はコードポイントなので、絵文字の ZWJ の途中で切れることはある（UTF-8 としては正しい）。

### 8. 部分確定の直後の左文脈

周辺テキストを渡すアプリ（GTK のアプリ、Firefox など）で行う。

1. 「きょうは」と打ち、Enter で確定する（周辺テキストに入る）。
2. 「きかいをつくる」と打ち、Space を押す。
3. ↓ で1番の候補（「キカ」、`consumed` = 2）を選び、Enter で確定する。
4. 残りの「いをつくる」について `convert` が送られる。

通る条件: 4 の `convert` の `left_context` が「キカ」で終わっている（「…きょうはキカ」）。周辺テキストを渡さないアプリでも同じ（`committed_context` を使う）。
