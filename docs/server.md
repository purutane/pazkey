# 変換サーバー仕様

`pazkey-server` は、読みと文脈を受け取って候補を返し、確定を学習する。入力中の状態は持たない（キャッシュは持つ）。メッセージの形は `docs/protocol.md` を参照。

## 依存

| 依存 | 版 | 用途 |
|---|---|---|
| AzooKeyKanaKanjiConverter | 本家 main の `25a6346` にパッチを当てたもの（下記） | 変換、学習。プロダクトは `KanaKanjiConverterModuleWithDefaultDictionary`。trait は `Zenzai` |
| llama.cpp | fkunn1326 版の b4846（コミット `10131b2`）、CPU | Zenzai の推論。azooKey は Linux では `libllama.so` をリンクする。zenz のモデルのトークナイザー（`gpt2-small-japanese-char`）に対応しているのはこの版だけで、上流の llama.cpp と Arch の `llama-cpp` ではモデルを読めない。また、azooKey が呼ぶ関数の一部は、上流の新しい版で消えている。パッケージはこの版をソースからビルドし、pazkey 専用の場所（「入れる場所」）に入れる（Arch の `llama-cpp` や `ggml` とファイルがぶつからないようにするため）。GPU には今は対応しない |
| Foundation、Dispatch | Swift 6.4 付属 | ソケット、キュー、JSON。パッケージは、使う Swift のランタイムを同梱する（「入れる場所」） |

ほかの依存は足さない（swift-nio、swift-protobuf も使わない）。

azooKey は、本家 main の `25a6346` から切ったブランチ `pazkey` にパッチを当てて使う。ブランチは GitHub のフォーク（`purutane/AzooKeyKanaKanjiConverter`）に置き、パッケージはそこを参照する。配るパッケージもここから取得するので、フォークは消さずに残す。push する前のパッチを試すときは、環境変数 `PAZKEY_AZOOKEY_URL` で手元のリポジトリを指せる。このとき `Package.resolved` が手元のパスに書き換わるので、そのままコミットしない。パッケージが固定する revision には、すべてフォークでタグ（`pazkey-<土台の短いハッシュ>-<連番>`）を付ける。ブランチ `pazkey` は版を上げるときに新しい土台へ rebase するので、タグがないと、過去の版や配ったパッケージが固定した revision を取得できなくなる。当てているパッチは次のとおり。

| パッチ | 理由 |
|---|---|
| Zenzai のプロンプトが 512 トークンを超えたら、評価をあきらめる | 超えるとサーバーが落ちる |

GPU のデバイスと層数を指定する仕組みは持たない。llama.cpp の既定（使えるデバイスに全層を載せる）で動かし、CPU 版の llama.cpp なので CPU で推論する。

## ビルド

Swift のパッケージは `server/` にある。プロダクトは `pazkey-server` と `pazkey-ctl`。

```sh
tools/build-llama.sh     # llama.cpp をソースからビルドして vendor/llama/ に入れる。git と cmake が要る
cd server
swift build -c release
```

`libllama.so` の場所は、環境変数 `PAZKEY_LLAMA_DIR` で指す（既定は `vendor/llama/lib`）。ビルドはその場所をサーバーに rpath として埋め込むので、systemd から起動しても `LD_LIBRARY_PATH` は要らない。llama.cpp は、版（「依存」の表）とビルドの設定をパッケージと同じにする。必要なライブラリ（`libllama.so`、`libggml.so`、`libggml-base.so`、`libggml-cpu.so`）だけを作り、各ライブラリの RUNPATH を `$ORIGIN` にする。こうすると `libllama.so` の依存（`libggml*.so`）も同じ場所から見つかる。

Zenzai ありのテストは、`tools/fetch-test-model.sh` で `vendor/zenz-v3.2-small.gguf` に取得したモデルを使う。取得するのは `pazkey-ctl fetch-model` と同じ版である（「Zenzai のモデルの取得」）。ファイルがなければ、Zenzai ありのテストを飛ばす。

パッケージ（リポジトリ直下の `PKGBUILD`）は次のようにビルドする。

- 取得は `source=()` と `prepare()` で済ませ、`build()` ではネットワークに出ない。SwiftPM の依存は、`prepare()` で `Package.resolved` のとおりに取得する（`--force-resolved-versions`）。SwiftPM の取得物と設定は、利用者のホームではなくビルドのディレクトリに置く。
- llama.cpp は、版と設定を `tools/build-llama.sh` と同じにしてビルドする。
- サーバーのビルドでは、`PAZKEY_LLAMA_DIR` をビルドした llama.cpp の場所に、`PAZKEY_LLAMA_RPATH`（rpath に埋め込む場所。既定は `PAZKEY_LLAMA_DIR` と同じ）を `/usr/lib/pazkey/llama` にする。
- Swift のランタイムを同梱するので、`pazkey-server` と `pazkey-ctl` の RUNPATH には、ツールチェーンの場所も `$ORIGIN` も入れず、同梱する場所（`/usr/lib/pazkey/swift`）だけを入れる。ツールチェーンの場所が残っていると、Swift を入れている機械ではそちらのランタイムが先に読まれる。
- `check()` はフロントエンドの単体テストを流す。サーバーのテスト（`tests/server/`）は systemd や入れた後の場所に頼るので、パッケージのビルドでは流さない。

普段動かすのは必ずリリースビルドにする。azooKey はデバッグビルド（`DEBUG`）で、変換した読みを標準出力に書く（`SwiftUtils/Debug.swift`）。「ログ」の節の取り決めに反するからである。

## 入れる場所

| 物 | 場所 |
|---|---|
| `pazkey-server` と辞書の bundle（`AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary.bundle`） | `/usr/lib/pazkey/` |
| llama.cpp のライブラリ（「ビルド」の4つ） | `/usr/lib/pazkey/llama/` |
| Swift のランタイム（`pazkey-server` と `pazkey-ctl` が使う `.so`） | `/usr/lib/pazkey/swift/` |
| `pazkey-ctl` | `/usr/bin/` |
| systemd のユニット（「systemd ユニット」） | `/usr/lib/systemd/user/` |
| `LICENSE` と `THIRD_PARTY_NOTICES`（同梱する第三者のソフトウェアとデータの著作権表示とライセンス） | `/usr/share/licenses/fcitx5-pazkey/` |
| fcitx5 のアドオン（フロントエンド） | fcitx5 の決まった場所（`/usr/lib/fcitx5/`、`/usr/share/fcitx5/addon/`、`/usr/share/fcitx5/inputmethod/`）とアイコンの場所（`/usr/share/icons/hicolor/`）。`frontend/` の CMake の install が入れる |

- 辞書の bundle は `pazkey-server` と同じディレクトリに置く。azooKey は実行ファイルのあるディレクトリから bundle を探し、ほかの場所を指す方法はない。ほかの bundle（`EfficientNGram`、`swift-transformers_Hub`）は使わないので入れない。
- llama.cpp は、パッケージのビルドで `/usr/lib/pazkey/llama` をサーバーの rpath に埋め込む（「ビルド」）。
- Swift のランタイムは、ビルドに使ったツールチェーンから、2つの実行ファイルが使うもの（依存をたどったもの）だけを写す。Linux の Swift は ABI が安定していないので、ツールチェーンのランタイムを実行時に使うと、ツールチェーンを上げたときにサーバーが起動しなくなる。同梱すれば、Swift はビルドにだけ要り、ビルドした後は外してよい。静的にリンクしないのは、C++ との相互運用に要る静的ライブラリが Swift のツールチェーン（`swift-bin`）にないからである。

## スレッド構成

| スレッド / キュー | 担うこと |
|---|---|
| 受付（I/O） | ソケットの accept、行の読み書き、JSON の解釈、`convert` のまとめ（下記）、ctl 系メッセージの応答、設定ディレクトリの監視（「設定の反映」） |
| 変換キュー（直列、1本） | azooKey へのアクセスすべて。`convert`、`commit`、`reset`、`close`、設定の適用、学習の保存 |

`KanaKanjiConverter` はスレッドセーフではない（`withSession` のドキュメントに明記されている）。azooKey のオブジェクトには、変換キューの外から触らない。

### convert のまとめ

- 受付スレッドは、セッションごとに「処理待ちの `convert`」を1件だけ持つ。新しい `convert` が来たら上書きする。上書きされた古いほうには応答しない。
- 変換キューは、空いたら処理待ちの `convert` を1件取り出して処理する。
- `commit`、`reset`、`close` は上書きせず、届いた順に変換キューへ積む。ただし同じセッションの処理待ち `convert` より後に処理する。順序が入れ替わると、学習の対象がずれるからである。

## 起動

1. 設定を読む（`docs/config.md`）。読めない、または不正な場合は既定値で起動し、ログに残す。
2. systemd のソケット起動で渡されたソケットを受け取る。環境変数 `LISTEN_PID` が自分の PID と一致し、`LISTEN_FDS` が 1 以上なら、fd 3 を使う。そうでなければ、開発用に自分でソケットを作る（`--listen <path>`）。
3. 変換器を作る: `KanaKanjiConverter.withDefaultDictionary(preloadDictionary: false)`。
4. 退避モードの判定（下記）をしてから、Zenzai のモデルを読み込む（「Zenzai のモデルの読み込み」）。
5. 受付を始める。

## convert の処理

```swift
var ct = ComposingText()
ct.insertAtCursorPosition(reading, inputStyle: .direct)
_ = ct.moveCursorFromCursorPosition(count: cursor - readingLength)  // cursor の位置へ
let result = try converter.withSession(sid) {
    converter.requestCandidates(ct.prefixToCursorPosition(), options: options(leftContext))
}
```

- `reading` と `cursor` の検証（コードポイント数、NFC、結合文字なし）は `docs/protocol.md` のとおり。
- 変換範囲は、カーソルより前（`prefixToCursorPosition()`）。`AncoSession` と同じ。文節の区切り直しは、クライアントがカーソルを動かして表す。
- 候補は `result.mainResults` を使う。予測変換（`predictionResults`）は今は使わない。
- `live` なら1位だけ、`full` なら設定の `candidates.page_size × candidates.max_pages` 件までを返す。
- 各候補の `consumed` は、`ct` の複製に `prefixComplete(composingCount: candidate.composingCount)` を適用し、減った文字数（コードポイント数）から求める。`.direct` 入力では、入力の要素数と読みの文字数が一致することを確認済み。
- セッションごとに、最後に返した `result` の `seq`、候補（`Candidate` の配列）、`ct` を保持する。`commit` で使う。

`ComposingText` は要求ごとに作り直す。azooKey の差分計算はセッションに残る前回の入力と比べるので、作り直しても効く（使い回した場合と変換時間は変わらない）。

## commit の処理

`AncoSession.execute(.selectCandidate)` と同じ順序で呼ぶ。

```swift
guard seq == session.lastSeq, session.lastCandidates.indices.contains(index) else { return }  // 黙って無視
let candidate = session.lastCandidates[index]
try converter.withSession(sid) {
    converter.setCompletedData(candidate)
    converter.updateLearningData(candidate)
    session.composingText.prefixComplete(composingCount: candidate.composingCount)
    if session.composingText.isEmpty {
        converter.stopComposition()
    }
}
session.lastSeq = nil  // 同じ result で二度学習しない
learningSaver.noteCommit()
```

## reset、close

- `reset`: そのセッションで `stopComposition()` を呼び、保持している候補と `ct` を捨てる。
- `close`、接続の切断: `removeSession()` を呼び、セッションの情報を捨てる。

## 変換のオプション

`ConvertRequestOptions` は設定から作る。リクエストごとに変わるのは左文脈だけである。

| オプション | 値 |
|---|---|
| `N_best` | 設定の `candidates.page_size × candidates.max_pages` |
| `requireJapanesePrediction` | `.disabled` |
| `requireEnglishPrediction` | `.disabled` |
| `learningType` | 設定 `learning.enabled` が真なら `.inputAndOutput`、偽なら `.nothing` |
| `memoryDirectoryURL` | `$XDG_STATE_HOME/pazkey/memory/` |
| `sharedContainerURL` | `$XDG_CACHE_HOME/pazkey/shared/` |
| `specialCandidateProviders` | `nil`（azooKey の既定。日付、時刻、カンマ区切りの数字、メールアドレス、Unicode、バージョン）。独自に足したり、設定で切り替えたりはしない |
| `zenzaiMode` | Zenzai を使っている（`zenzai` が `enabled`）なら `.on(weight:inferenceLimit:personalizationMode: nil, versionDependentMode: .v3(...))`。`.v3` には `leftSideContext`（`left_context`。空なら渡さない）と `maxLeftSideContextLength`（設定の `context.max_chars`）を渡す。それ以外は `.off` |

## 学習の保存

`commitUpdateLearningData()` を、次のどれかのときに変換キューで呼ぶ。

- 前回の保存から確定が `learning.save_every_commits` 回たまったとき
- 最後の確定から `learning.save_idle_seconds` 秒、確定がないとき（未保存の確定がある場合だけ）
- SIGTERM を受けたとき

SIGTERM（と SIGINT）を受けたら、受付を止め、処理待ちの `convert` を捨て、学習を保存してから終わる。保存にかけられる時間は systemd の `TimeoutStopSec` で区切る。ログアウトやシャットダウンを遅らせないように、この値は短くする（下記）。

azooKey の保存処理（`LongTermLearningMemory.merge`）は、書きかけで落ちてもデータを壊さない。落ちて失うのは、最後の保存より後の確定だけである。読めないほど壊れた場合は、azooKey の処理で破棄される。

保存にかかる時間は、学習データが上限（65536語）に達した状態で最大 0.44 秒である。

## 設定の反映

`docs/config.md` の手順で設定を読み直したあと、変換キューで次を行う。

- 軽い項目（候補数、文脈の長さ、推論回数の上限、学習の保存条件）: 次の変換から新しいオプションを使う。
- `learning.enabled`: `ConvertRequestOptions.learningType` を変える。azooKey は変換のたびに、オプションから学習の設定を作り直している（`DicdataStoreState.updateIfRequired`）。`updateLearningConfig` は使えない（引数の `LearningConfig` のイニシャライザが azooKey の外から呼べない）。
- 重い項目（Zenzai の有無、モデル、デバイス）: Zenzai のモデルを読み直す（「Zenzai のモデルの読み込み」）。

反映が終わったら `config_generation` を1増やし、全クライアントへ `config_changed` を送る。内容が前と同じでも、正しく読めたら増やす。設定ファイルを消したときは、既定値を読んだものとして扱う。不正で反映しなかったときは、`config_generation` を変えずに、全クライアントへ `config_rejected` を送る（`docs/protocol.md`）。

監視は `ConfigWatcher`（inotify）が受付キューの上で行う。設定ディレクトリがなければ起動時に作る（ファイルは作らない）。

## Zenzai のモデルの読み込み

azooKey には、モデルを読み込むだけの公開の API がない。Zenzai を有効にして初めて変換したときに読み込み、失敗しても変換はエラーにならず、Zenzai なしの結果を返す（失敗は `KanaKanjiConverter.zenzStatus` に残る）。そのため、次のようにする。

- 起動時と、重い項目を反映するときに、使い捨てのセッションで1回変換してモデルを読み込ませ、`zenzStatus` で成否を確かめる。最初のキー入力で読み込みを待たせないためでもある。
- 起動時に失敗したら、Zenzai なしで動く。`hello` と `status` の `zenzai` は `unavailable` を返し、ログに残す。
- 重い項目の反映で失敗したら、Zenzai の部分（有無、モデル、デバイス）だけ前の状態のまま動き続け、ほかの項目は反映する。前の状態が Zenzai なしなら `unavailable` にする。エラーはログと `reload_result`（`pazkey-ctl reload` を使った場合）で報告する。
- 設定画面だけを使う利用者には、フロントエンドが fcitx5 の通知で知らせる（`docs/frontend.md`「Zenzai の状態を知らせる」）。サーバーは `hello` と `config_changed` の `zenzai` で状態を伝える。

## Zenzai のモデルの取得

pazkey はモデルを同梱しない。利用者の指示で、`pazkey-ctl fetch-model` が取得する。サーバーと fcitx5 のアドオンは通信しない。

- 取得するもの: Hugging Face の `Miwa-Keita/zenz-v3.2-small-gguf`、リビジョン `c67e03e07d215c869f591b274c1631170d3e11fe` の `ggml-model-Q5_K_M.gguf`。sha256 は `29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673`。ライセンスは Apache-2.0。
- 置く場所: 常に既定の場所 `$XDG_DATA_HOME/pazkey/zenz.gguf`。`zenzai.model_path` は見ない（利用者が指した任意のファイルを上書きしないため）。
- 手順: `zenz.gguf.lock` をロックし（取れなければ「取得中」として終了コード 1）、すでにハッシュの合うファイルがあれば取得しない。なければ `zenz.gguf.part` に `curl` で取得し、`sha256sum` で確かめてから rename する。合わなければ一時ファイルを消して失敗にする。
- テストのときだけ、環境変数 `PAZKEY_TEST_MODEL_URL`、`PAZKEY_TEST_MODEL_SHA256` で取得元とハッシュを差し替えられる。
- 置いたら（すでにあった場合も）サーバーへ `reload` を送り、モデルを読み込ませる。サーバーにつながらなければ、次の起動で読み込まれるので、失敗にはしない。
- `--notify` を付けると、始めたときと終わったとき（成否と理由）に、デスクトップの通知（`org.freedesktop.Notifications`。`busctl --user` で送る）で知らせる。設定画面のボタンから起動したときは端末がないので、これで結果を知らせる。
- 設定画面のボタンは、ユーザーのユニット `pazkey-fetch-model.service`（「systemd ユニット」）を `systemctl --user start --no-block` で起動する。設定画面のプロセスの `PATH` に左右されず、二重に押しても systemd が1つにまとめ、ログは journald に残る。

## 退避モード

Zenzai（llama.cpp）が原因と思われるクラッシュが続いたとき、Zenzai を切って起動する。

- 起動のたびに `$XDG_STATE_HOME/pazkey/crash.json` に起動時刻を追記し、正常終了したら自分の起動の分を消す。残っている記録が「正常終了しなかった起動」である。
- 落ちた原因が Zenzai かどうかは切り分けられないので、区別せずに数える。辞書による変換で落ちた場合や、`SIGKILL`（`TimeoutStopSec` を超えた場合を含む）も数える。
- 起動時、直近 `fallback.window_seconds` 秒以内に「正常終了しなかった起動」が `fallback.max_crashes` 回以上あれば、退避モードで起動する。時間幅の外の記録と、未来の時刻の記録（時計が戻った場合）は数えずに捨てる。記録のファイルが壊れていたら、記録がないものとして扱い、ログに残す。
- 退避モードでは `zenzaiMode = .off` で動き、`hello` と `status` の `zenzai` は `fallback` を返す。設定で Zenzai を切っていれば、退避モードでも `disabled` を返す。
- 退避モードは、設定の反映（監視による読み直しと `reload` のどちらでも）か、サーバーの再起動で解除を試みる。解除するときも記録は消さない。原因が直っていなければ、次に落ちたときにすぐ退避モードへ戻る。
- 辞書による変換で落ちる場合は、退避モードでも落ち続ける。退避モードはこれを防がない。systemd が間隔を延ばしながら起動し直し続ける（「再起動回数の上限」）。

既定の閾値は、systemd の再起動の間隔（「再起動回数の上限」）と合わせて決めた。

## systemd ユニット

`pazkey-server.socket`:

```ini
[Unit]
Description=pazkey conversion server socket

[Socket]
ListenStream=%t/pazkey/server.sock
SocketMode=0600
DirectoryMode=0700
```

`pazkey-server.service`:

```ini
[Unit]
Description=pazkey conversion server
Requires=pazkey-server.socket
StartLimitIntervalSec=0

[Service]
ExecStart=/usr/lib/pazkey/pazkey-server
Restart=on-failure
RestartSec=200ms
RestartSteps=5
RestartMaxDelaySec=10s
TimeoutStopSec=3s
```

`pazkey-fetch-model.service`（「Zenzai のモデルの取得」。有効にはせず、設定画面のボタンが起動する）:

```ini
[Unit]
Description=pazkey: fetch the Zenzai model

[Service]
Type=oneshot
ExecStart=/usr/bin/pazkey-ctl fetch-model --notify
```

ユニットのファイルは `server/systemd/` に置く。

ソケットのユニットは、既定で有効にする。パッケージが `/usr/lib/systemd/user/sockets.target.wants/pazkey-server.socket`（`../pazkey-server.socket` へのリンク）を持つ。プリセットや `systemctl --global enable` を使わないのは、Arch ではユーザーのユニットのプリセットが入れても適用されず、`--global enable` が作る `/etc` のリンクは pacman が管理しない（外したときに残る）からである。ソケットのユニットには `[Install]` を書かない。書くと、`/etc` と `~/.config` にリンクがないので、`systemctl --user is-enabled` や `status` が、ログインで起動するのに `disabled` と出て紛らわしい。書かなければ、同じ形で入る `dbus.socket` などと同じく `static` と出る。`enable` と `disable` は使えないので、利用者が止めるときは `systemctl --user mask pazkey-server.socket` を使う。入れた直後のセッションではソケットは待ち受けていないので、ログインし直す（または `systemctl --user start pazkey-server.socket`）。`RestartSteps` を使うので、systemd 254 以上が要る。

`StartLimitIntervalSec`、`RestartSec`、`RestartSteps`、`RestartMaxDelaySec` は「再起動回数の上限」で決めた。`TimeoutStopSec` の 3s は、終わるときにかかる時間（処理中の変換を待つ分と学習の保存で、最悪で約1秒）の3倍ほどにした。

### 再起動回数の上限

- systemd の既定（`StartLimitIntervalSec=10s`、`StartLimitBurst=5`）のままだと、10秒の間に起動が5回を超えると上限に当たる。上限に当たると、サービスだけでなくソケットのユニットも失敗状態（`service-start-limit-hit`）になって待ち受けをやめ、`systemctl --user reset-failed` で戻すまで変換できない。キーを打つたびに落ちる場合は、打つ速さによらず5回ほどで当たる。
- そのため上限はなくし（`StartLimitIntervalSec=0`）、代わりに再起動の間隔を段々に延ばす（`RestartSec=200ms` から `RestartSteps=5` で `RestartMaxDelaySec=10s` まで）。起動直後に落ち続けても、10秒に1回の起動で済む。原因の入力が止まれば、最長10秒で変換が戻る。`RestartSteps` は systemd 254 以上が要る。
- systemd の再起動の回数は、サービスが動き続けている間は 0 に戻らない。前に何度か落ちていると、後でたまたま1回落ちたときも、延びた間隔だけ待つ（最長10秒）。待っている間もソケットは接続を受け付けるが、返事はしない。
- 起動直後に落ち続ける場合、退避モード（既定の閾値）には4回目の起動、2秒ほどで入る。

## 設定ファイルの検証

`pazkey-server --check-config <file>` は、設定ファイルを検証して結果を表示し、終わる（正しければ終了コード 0、不正なら 1）。サーバーは起動しない。`tests/config/check.py` が使う。

## pazkey-ctl

サーバーと同じパッケージに入れる、小さな CLI。

| コマンド | 送るメッセージ | 出力 |
|---|---|---|
| `pazkey-ctl reload` | `reload` | 成否と、エラーがあればその一覧。設定が不正なとき、設定は反映したが Zenzai のモデルを読み込めなかったときは、終了コード 1 |
| `pazkey-ctl status` | `status` | 状態の一覧 |
| `pazkey-ctl devices` | `devices` | 使えるデバイスの一覧。設定の `zenzai.device` に書く値もあわせて表示する |
| `pazkey-ctl debug on` / `off` | `debug` | デバッグログ（「ログ」）を書き始める / やめる |
| `pazkey-ctl fetch-model [--notify]` | `reload`（取得のあと） | Zenzai のモデルを取得する（「Zenzai のモデルの取得」）。失敗したら終了コード 1 |

接続先は `$XDG_RUNTIME_DIR/pazkey/server.sock`。サーバーにつながらないとき、応答が5秒以内に来ないときも、終了コード 1 で終わる。

## ログ

標準エラー出力に書く（journald が拾う）。変換した読みや文脈の中身はログに書かない。入力した文章が漏れるからである。書くのは、長さ、時間、エラーの種類だけにする。

llama.cpp のログは、警告とエラーだけを通す。そのままでは、モデルを読み込むたびにモデルの情報を 150 行ほど書き、journald が埋まる。モデルを読み込めないときの理由はエラーとして残る。

### デバッグログ

不具合の究明のために、操作と状態を1つずつ書くログ。ふだんは書かない。`pazkey-ctl debug on` で書き始め、`off` でやめる。設定ファイルには持たず、サーバーを起動し直すと切れる（入れたまま忘れないようにするため）。

上の取り決め（中身を書かない）はデバッグログでも同じである。読み、文脈、候補の文字列は書かず、次だけを書く。行の先頭は `debug:`。

| 出来事 | 書くこと |
|---|---|
| 接続、切断 | 接続の番号、切断のときはその接続のセッションの数 |
| メッセージを受けた | 接続、`type`、`session`、`seq`。`convert` では `kind`、読みの長さ、`cursor`、左文脈の長さ。`commit` では `index` |
| `convert` を捨てた | 接続、`session`、捨てた `seq` と理由（新しい `convert` で上書き、切断、終了） |
| `result` を返した | 接続、`session`、`seq`、候補の数、変換にかかった時間 |
| `commit` を処理した | 学習したか、黙って無視したか |
| `error` を返した | `code`、`session`、`seq` |

フロントエンドのデバッグログは `docs/frontend.md`「デバッグログ」。中身が要るときは、記録用の偽サーバー（`tests/frontend/fake_server.py`）を、その目的で意図して動かす。
