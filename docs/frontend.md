# fcitx5 アドオン仕様

`fcitx5-pazkey` は、入力中の状態の持ち主であり、画面に出すものを決める。変換はサーバーに任せるが、待たない。メッセージの形は `docs/protocol.md` を参照。

## 構成

| 部品 | 置き場所 | 担うこと |
|---|---|---|
| `PazkeyEngine`（`InputMethodEngineV2`） | `src/addon/engine.*` | キーイベントの受け口、設定画面の項目、サーバーへの接続を1本持つ |
| `PazkeyContext`（入力欄ごと、`FactoryFor`） | `src/addon/engine.*` | `PazkeyState` を1つ持ち、fcitx5 とサーバーへのつなぎ（`StateHost`）になる |
| `PazkeyState` | `src/core/state.*` | 入力状態、モード、最後の要求と応答、最後に描いた内容。キー処理 |
| `RomajiConverter`（`feedRomaji`、`flushRomaji`） | `src/core/romaji.*` | ローマ字からかなへの変換。状態を持たない関数として作る |
| `ServerClient` | `src/addon/client.*` | ノンブロッキングのソケット、行の送受信、再接続 |
| `render(state)`（`PazkeyState::render`、`PazkeyContext::render`） | 両方 | 状態から preedit と候補ウィンドウを作り、前回と違えば送る |

置き場所はいずれも `frontend/` の下。`src/core/` は fcitx5 に依存しない部分（入力状態とキー処理、ローマ字、JSON、設定、プロトコル）で、単体テスト（`frontend/tests/`）で確かめる。`PazkeyState` は、アプリとサーバーとのやりとりをすべて `StateHost` を通して行う。

## 状態

入力欄ごとに次を持つ。

| 項目 | 内容 |
|---|---|
| `kana` | 確定済みのかな（文字列） |
| `romaji_tail` | まだかなにならないローマ字の末尾（例: `ky`） |
| `cursor` | `kana` の中のカーソル位置（コードポイント数）。`romaji_tail` はカーソルの直前にあるものとして扱う |
| `mode` | `Empty` / `Composing` / `Converting` |
| `ascii` | Shift で英字を打ってから確定するまでの、一時的な半角英字の入力の間か（「字種」） |
| `seq` | 最後に送った `convert` の連番 |
| `candidates` | 最後に受け取った `result`（その `seq` 付き）、または代用の候補。なければ空 |
| `selected` | `Converting` での選択中の候補番号 |
| `last_live` | 表示に使っている `live` の1位の候補（文字列と `consumed`）と、それを求めたときの読み。なければ空 |
| `committed_context` | この入力欄で直前に確定した文字列（周辺テキストが取れない場合の左文脈の代わり） |
| `context_base` | この入力で使う左文脈。入力を始めたときに決め、入力の中で確定した文字列を後ろに足していく（「左文脈」） |
| `last_rendered` | 最後にアプリへ送った preedit とカーソル、候補ウィンドウの内容 |

読み（`reading`）は、`kana` に `romaji_tail` をそのまま連結したものとして扱わない。サーバーに送る `reading` は `kana` だけで、`romaji_tail` は送らない。ただし変換キーを押したときは、先に `romaji_tail` を確定させる（下記）。`kana` は表示の形（記号、数字、英字は全角）で持ち、`reading` は全角の数字と英字を半角に直してから送る（「字種」）。

## ローマ字かな変換

- 表は Mozc の標準ローマ字表と同等にする。`data/romaji.tsv`（入力、出力、残すもの、の3列）として持ち、ビルド時に組み込む。表は Mozc の `src/data/preedit/romanji-hiragana.tsv` に手を加えずに写したもので、ライセンス（BSD-3-Clause）の条文は `frontend/data/romaji.tsv.LICENSE` にある。
- 規則の例: `ka`→か、`kya`→きゃ、`nn`→ん、`n` + 子音（`y` 以外）→ん + 子音、同じ子音の連続（`tt`）→っ + 子音、`xa` / `la`→ぁ、`-`→ー。
- `,` `.` `/` `[` `]` の規則は、表のものを Mozc の設定 `punctuation_method` と `symbol_method` の規則で置き換える（Mozc と同じ作り）。既定値なので、`,`→、`.`→。`/`→・`[`→「`]`→」になる（下記「Mozc の設定との対応」）。
- 変換キー、確定キー、カーソル移動のときに `romaji_tail` が残っていれば、`n` だけならんにし、それ以外は全角の英字にして `kana` に入れる。
- 「ローマ字で入力できる文字」は、ASCII の印字可能文字（`!`〜`~`。空白は含まない）である。表の規則にならない文字（数字、記号）は、全角にして `kana` に入れる。大文字は一時的な半角英字の入力を始める（「字種」）。
- 表は Mozc と同じく前方一致で読む。打った列がもっと長い規則の途中なら待ち、どの規則にもならなくなったら、先頭から最も長く規則に当たる部分を出して残りを読み直す。そのため `kanna` は「かんあ」になる（Mozc と同じ。「かんな」は `kannna`）。
- ローマ字の処理はサーバー（azooKey の `.roman2kana`）ではなくここで行う。サーバーが落ちても固まっても、かなの表示と入力を続けられ、プロトコルで送るのがかなの読みだけになるからである。引き換えに、azooKey の `.roman2kana` 入力で使える打ち間違いの補正は使えない。

## 字種

記号、数字、英字の幅は Mozc に合わせる。

- **入力中**: ローマ字表から出た文字のうち ASCII のもの（表にない数字や記号、`www` → `w` + `ww` の `w` など）は、全角にして `kana` に入れる。`romaji_tail` も全角で表示する（`k` → ｋ）。Mozc の既定（`CreateDefaultConfig` の preedit の字種がすべて全角）と同じ。
- **変換しないで確定する**（Enter、IME の切り替えなど）: 表示のまま、全角で確定する（`2026` → ２０２６、`honntou?` → ほんとう？）。
- **変換する**: `reading` では、全角の数字と英字を半角に直し、記号は全角のまま送る（`2026nenn` → `2026ねん`、`honntou?` → `ほんとう？`）。文字数は変わらないので、`cursor` と `consumed` はそのまま使える。azooKey は読みの字種のまま候補を出し、もう一方の幅の候補を出さない（全角の `２０２６ねん` からは 2026年 も 二千二十六年 も出ない）。数字と英字は、変換後は半角がほしいので、半角で送る。変換後の字種を覚える Mozc の仕組み（`LAST_FORM`）は作らない。
- **一時的な半角英字の入力**: 英字の大文字（Shift で打った英字）を打つと、`ascii` を真にする。残っていた `romaji_tail` は先に全角にして `kana` に入れる。`ascii` の間は、打った文字をローマ字にせず、半角のまま `kana` に入れる。Space は変換せず、半角の空白を入れる。変換キーでは変換する。`Empty` へ移ると `ascii` を偽に戻す。Mozc の既定（`shift_key_mode_switch` が `ASCII_INPUT_MODE`）と同じ。例: `Google` → Space → Enter で「Google 」を確定する。
- **テンキー**: テンキーの文字（数字、`.`、`+`、`-`、`*`、`/`、`=` など）は、ローマ字にせず半角のまま `kana` に入れる。打ちかけの `romaji_tail` は先に全角にして入れる。入力がないときは `Composing` へ移る。Mozc の既定（`numpad_character_form` が `NUMPAD_HALF_WIDTH`）と同じ。
- **代用の候補**: ひらがなの候補は `kana` そのもの（入力中の表示と同じ）。半角カタカナの候補では、全角の英数字と記号も半角にする。

## キーごとの動作

### Empty（入力なし）

| キー | 動作 |
|---|---|
| ローマ字で入力できる文字 | `Composing` へ移り、その文字を入力する |
| Space | 全角の空白を確定する（Mozc と同じ） |
| Shift+Space | 半角の空白を確定する（Mozc の `InsertAlternateSpace`） |
| それ以外 | 何もしない（キーをアプリへ渡す） |

### Composing（入力中）

| キー | 動作 |
|---|---|
| ローマ字で入力できる文字 | カーソル位置に入力する。ライブ変換が有効なら `live` の `convert` を送る（「ライブ変換の表示と確定」） |
| Backspace、Ctrl+H、Ctrl+Backspace | `romaji_tail` があればその末尾、なければカーソル直前のかなを1文字消す。空になったら `Empty` へ |
| Delete | カーソル直後のかなを1文字消す |
| ←、→、Shift+←、Shift+→、Home、End、↓ | カーソルを動かす。↓ は末尾へ |
| Space、Shift+Space、変換 | `full` の `convert` を送り、`Converting` へ。応答が来るまでは、かなを表示したまま待つ。ただし一時的な半角英字の入力の間（`ascii`）は、Space と Shift+Space は半角の空白を入れる（「字種」） |
| Enter、Ctrl+Enter | 表示している文字列（ライブ変換中ならその文字列、F6〜F10 で変えていればその表示、そうでなければかな）を確定し、`Empty` へ。ライブ変換の文字列を確定したときは、その候補の `commit` を送る（下記「ライブ変換の表示と確定」） |
| Escape | 入力を捨て、`Empty` へ |
| F6 / F7 / F8 | ひらがな / 全角カタカナ / 半角カタカナに変えて表示する。サーバーには問い合わせない。かなを打ち足す、消す、カーソルを動かすと、ひらがなの表示に戻る |
| F9 / F10 | 打ったとおりのキー（ローマ字のまま）を全角 / 半角の英数字にして表示する。ほかは F6〜F8 と同じ。かなが規則で出たときのキーを、かなの文字ごとに覚えておく。1回の規則で2文字以上出たとき（`kya` → きゃ）は先頭の文字にまとめて持つので、その一部を消してもキーは残る（`kya` から ゃ を消すと、F10 で `kya`） |
| それ以外 | 何もしない。キーはアプリへ渡さない（preedit とアプリの内容がずれないようにする）。修飾キーだけの押下は渡す |

Space と変換キーでは、変換範囲（`cursor`）を読み全体にしてから `full` を送る。カーソルを動かして入力していた途中でも、読み全体を変換する（Mozc と同じ）。

### Converting（候補選択中）

| キー | 動作 |
|---|---|
| Space、変換、↓、Tab | 次の候補 |
| Shift+Space、↑、Shift+Tab、Shift+変換 | 前の候補 |
| PageDown、PageUp、Shift+↓、Shift+↑ | 次 / 前のページ |
| 1〜9（設定で有効なとき） | そのページの n 番目の候補を確定する |
| Enter、Ctrl+Enter | 選択中の候補を確定する |
| Shift+← / Shift+→ | 変換範囲（カーソル位置）を1文字縮める / 伸ばし、`full` の `convert` を送り直す |
| Escape | `Composing` へ戻る（カーソルは読みの末尾） |
| Backspace、Ctrl+H、Ctrl+Backspace、Delete | Escape と同じ。かなは消さず、変換し直しもしない（Mozc の表で `Cancel`） |
| F6〜F10 | Escape と同じく `Composing` へ戻り、読み全体を `Composing` の F6〜F10 と同じ表示にする。Mozc は選んでいる文節だけを変えるが、pazkey は文節を分けないので読み全体に当てる |
| ローマ字で入力できる文字 | 選択中の候補を確定してから、その文字の入力を始める。読みが残っていれば、残りを `Composing` のかなとして持ち、その末尾に入力する（残りを変換しないまま確定しない） |
| それ以外 | 何もしない。キーはアプリへ渡さない |

`result` を待っている間（候補がまだない間）は、Enter で表示しているかなを確定して `Empty` へ、ローマ字で入力できる文字でかなを確定してからその文字の入力を始める。Escape、Backspace、Delete、F6〜F10 は上と同じ。Shift+← / Shift+→ も上と同じ。それ以外のキーは何もしない。

### 候補を確定したとき

1. 候補の文字列を `commitString` でアプリへ送る（空文字列なら送らない）。
2. `commit`（その `result` の `seq` と候補番号）をサーバーへ送る。応答は待たない。
3. `kana` の先頭から `consumed` 文字を取り除く。カーソルもその分だけ動かす。カーソルが 0 になったら（Shift+← で縮めた範囲を使い切ったら）、残り全体を変換範囲にする。
4. `committed_context` に候補の文字列を足す。
5. `kana` が空なら `Empty` へ。残っていれば、残りについて `full` の `convert` を送り、`Converting` のままにする。

代用の候補を確定したときは、2 を行わない（下記「非同期の扱い」）。

### ライブ変換の表示と確定

`live_conversion.enabled` が真のときに行う。

`live` を送るのは、`Composing` で次をすべて満たすときである。

- かなを表示している（F6〜F10 で表示を変えていない）。
- カーソルが読みの末尾にある。`reading` は `kana`、`cursor` は読みの長さにする。
- パスワード欄でない。読みをキーごとにサーバーへ送らないためである。
- つながっていて、読みが上限以下である（「非同期の扱い」）。

送るきっかけは、キーを処理した結果、`kana` が変わったとき（文字を打つ、Backspace、Delete、候補を確定してから入力を続ける）と、カーソルを動かして読みの末尾に戻ったときである。`romaji_tail` だけが変わったとき（`kak` の `k`）は送らない。候補選択中から Escape などで `Composing` に戻ったときも送らない（変換を取り消した意図を優先し、次に `kana` が変わるまでかなを表示する）。ただし、そのあとカーソルを動かして読みの末尾に戻したときは、新しい操作として送る。F6〜F10 で表示を変えたあと、カーソルを動かしてかなの表示に戻ったときは送らない。`last_live` は F6〜F10 では捨てないので、カーソルが末尾にあれば、下の規則のとおり F6〜F10 を押す前の表示に戻る。

`live` の結果は、表示と確定とで扱いを分ける。キーを打つたびに `seq` が進むので、「最新の `seq` の結果だけを表示する」とすると、結果が届くまでの間かなに戻り、表示がちらつくからである（例: 木 → きか → 価格 → きかい → 機械）。

- 最新の `seq` の `live` の結果が届いたら、`last_live` に入れる。候補がない、使えない（`consumed` が 0 か読みの長さを超える）、または `error` が返ったときは、`last_live` を空にしてかなを表示する。
- 表示には `last_live` を使う。新しい結果を待っている間も、前の結果を表示し続ける。ただし、`last_live` を求めたときの読みが、今の `kana` の先頭と一致している場合（後ろに打ち足しただけの場合）に限る。Backspace やカーソル移動で前の部分が変わっていたら、`last_live` を捨て、かなを表示する。カーソルが読みの末尾にないときも、（`last_live` は捨てずに）かなを表示する。
- 表示する文字列は、候補の文字列のあとに、使われなかった残り（`kana` の `consumed` 文字目以降と `romaji_tail`）を続けたものにする。カーソルは末尾に置く。待っている間に打ったかなも、この「残り」に入る（例: 木 → 木か → 価格 → 価格い → 機械）。1位の候補が読みの一部しか使わない場合（`consumed` が読みの長さより短い）も、同じ規則で表示する。
- Enter では、表示をそのまま1回の `commitString` で送る。`last_live` が最新の `seq` の結果なら、その候補について `commit` を送る。前の結果を表示し続けている間なら、`commit` は送らない（学習が1回抜けるだけである）。残りのかなは学習しない。
- フォーカスが外れたときと IME を切り替えたとき（「IME の切り替えとフォーカス」）も、表示を確定し、`commit` は Enter と同じ条件で送る。
- そのあと `Empty` へ移る（`reset` も送る。下記）。`Empty` へ移るときと `Converting` へ移るときは、`last_live` を空にする。

### Empty へ移るとき

どの経路で入力が空になっても、`Empty` へ移るときは必ず `reset` を送る。経路は、Backspace と Escape、Enter での確定（かな、F6〜F10 の表示、ライブ変換の文字列）、候補の確定（代用の候補を含む）、IME の切り替えとフォーカスの喪失である。

- 理由: サーバーが `stopComposition()` を呼ぶのは、`commit` で読みがすべて使われたときと、`reset` を受けたときだけである。`commit` を送らない経路（かなのままの確定、代用の候補）や、読みの一部しか使わない `commit` で入力を終えると、サーバーのセッションに変換途中の状態が残る。azooKey の手本（`AncoSession`）は、入力が空になったら `stopComposition()` を呼ぶ。
- `commit` で読みがすべて使われた直後にも送る。サーバーは `stopComposition()` を2回呼ぶことになるが、セッションの状態を初期化するだけなので害はない。条件で分けないほうが送り忘れが起きない。
- 実装では、`Empty` へ移る処理を1か所にまとめ、そこで `reset` を送る。接続していないときは送らない（再接続したサーバーのセッションは空である）。

## Mozc の設定との対応

入力の振る舞いは、Linux での Mozc の既定に合わせる。設定では切り替えない。Mozc の設定と同じ名前と選択肢の形で、既定値を決め打ちにして、フロントエンドのコードの1か所（`frontend/src/core/mozc_defaults.h`）に置く。切り替えたいという声が出たときに、設定の項目に足せるようにするためである。比べた Mozc の既定値は、上流の `src/protocol/config.proto` と `config_handler.cc` の `CreateDefaultConfig` にある。

| Mozc の設定 | 既定値 | pazkey での扱い |
|---|---|---|
| `session_keymap` | `MSIME`（Linux の既定） | 「キーごとの動作」の表。下に、合わせていないキーを挙げる |
| `preedit_method` | `ROMAN` | ローマ字だけ（設計原則7） |
| `custom_roman_table` | なし（`romanji-hiragana.tsv`） | `frontend/data/romaji.tsv`（「ローマ字かな変換」） |
| `punctuation_method` | `TOUTEN_KUTEN` | 表の `,` `.` を置き換える（「ローマ字かな変換」） |
| `symbol_method` | `CORNER_BRACKET_MIDDLE_DOT` | 表の `/` `[` `]` を置き換える（「ローマ字かな変換」） |
| `space_character_form` | `FUNDAMENTAL_INPUT_MODE` | 入力がないときの Space は全角（入力モードはひらがなだけなので） |
| `shift_key_mode_switch` | `ASCII_INPUT_MODE` | 一時的な半角英字の入力（「字種」） |
| `numpad_character_form` | `NUMPAD_HALF_WIDTH` | テンキーは半角（「字種」） |
| `character_form_rules` | 入力中はすべて全角。変換後は英数と記号が `LAST_FORM` | 入力中は全角（「字種」）。`LAST_FORM` は作らない |
| `selection_shortcut` | `SHORTCUT_123456789` | `candidates.select_with_digits`（`docs/config.md`） |
| `use_auto_conversion`、`use_typing_correction`、`use_kana_modifier_insensitive_conversion` | `false` | 作らない |

キーは、Mozc の MS-IME の表（上流の `src/data/keymap/ms-ime.tsv`。`/usr/lib/mozc/mozc_server` に埋め込まれたものと同じ）に合わせる。次のキーは合わせていない。

- 作らない: 文節の操作（変換中の ← / → で文節を移る、Ctrl+↓ で最初の文節だけ確定する）。pazkey は変換範囲を1つだけ持つ（変換中の Shift+← / → で変える）。Emacs 風の Ctrl キー（Ctrl+A / E / D / S / K / L / Z / G など）と Ctrl+I / U / O / P / T。F2（学習を使わない変換）。半角/全角キーと漢字キーでのオフ（fcitx5 の切り替えキーが受け持つ）。F10 を続けて押したときの大文字と小文字の切り替え。
- 今は作らない（あとで考える）: Tab の予測変換（pazkey の変換中の Tab は次の候補）。入力モードの切り替え（カタカナ、英数、無変換、Shift+無変換）。入力がないときの変換キーでの再変換、Backspace での確定の取り消し、Ctrl+Backspace での元に戻す。Ctrl+Space と Ctrl+Shift+Space で空白の幅を選ぶ。

## 非同期の扱い

- ソケットはノンブロッキングにし、fcitx5 のイベントループ（`Instance::eventLoop().addIOEvent`）で読み書きする。キー処理の中でソケットを待たない。
- `result` を受け取ったら、そのセッションの最新の `seq` と一致するときだけ状態に入れ、`render` する。一致しなければ捨てる。
- `full` を送ってから `timeout.full_ms` ミリ秒以内に `result` が来なければ、代用の候補を出す。代用の候補は、ひらがな、全角カタカナ、半角カタカナの3つで、`consumed` はすべて読みの全長。代用の候補を確定したときは `commit` を送らない（サーバーの `result` ではないため）。
- 代用の候補を出したら、その `seq` を期限切れにする。後から同じ `seq` の `result` が届いても捨てる。選んでいる途中で候補の一覧が差し替わらないようにするためである。本物の候補が欲しければ、Escape で戻って変換し直せば、新しい `seq` で問い合わせる。
- `live` には期限を設けない。応答が来なければ、かなを表示し続ける。
- 読みが上限（`docs/protocol.md`「文字数の単位」、256 コードポイント）を超えている間は、`convert` を送らない。接続していないときと同じく、`live` は送らず、変換キーではすぐ代用の候補を出す。入力そのものは止めない。
- `convert` に `error` が返り、その `seq` が最新なら、`full` のときはすぐ代用の候補を出し（その `seq` は期限切れにする）、`live` のときはかなを表示する。
- `result` の候補のうち、`consumed` が 0 のものと読みの長さを超えるものは使わない（確定できないため）。使える候補が1つもなければ、代用の候補を出す。
- 代用の候補は、同じ文字列になるもの（読みが英数字だけの場合など）を1つにまとめる。
- 結果を待っている間に接続が切れたら、期限を待たずにすぐ代用の候補を出す。

## サーバーとの接続

- 起動時と切断時に、ノンブロッキングで接続を試みる。失敗したら、イベントループのタイマーで再試行する。間隔は 100ms から倍々にし、上限は 2秒。
- 書き込みに失敗しても、その場では切断しない。イベントループに戻ってから切断する（キー処理の途中で、切断の処理が入力状態を書き換えないようにする）。
- イベントループのタイマーと監視は、作ったら使い回し、自分のコールバックの中で壊さない。fcitx5 の終了時はイベントループが終わってからアドオンが壊されるので、そのときは接続を先に捨て、`close` は送らない。
- 接続したら `hello` を送る。`protocol_mismatch` が返ったら再試行を止め、fcitx5 の通知で知らせる。
- 接続していない間は、`live` を送らず、`full` は送らずにすぐ代用の候補を出す。
- 再接続しても、切断の前に送った要求や送れなかった要求（応答の来ていない `convert`、送信待ちのバッファにあった行）は送り直さない。送信待ちのバッファは切断の時点で捨てる。次のキー入力で、新しい `convert` を送る。落ちる原因になった入力を送り直すと、サーバーが落ち続けるからである。
- サーバーを起動するのは systemd である。アドオンはサーバーのプロセスを起動しない。
- 起動時と、`config_changed` か `config_rejected` を受けたときに `config.json` を読み直す。読めない、または不正なら、前の設定を使い続け、fcitx5 の通知で知らせる（同じ誤りでは2回知らせない）。知らないキーはログに残す（`docs/config.md`「読み込みと検証」）。

### つながらないことを知らせる

ソケットのユニットを有効にし忘れた場合、再起動回数の上限に当たった場合（`docs/server.md`「再起動回数の上限」）、更新でユニットが消えた場合のどれでも、見た目は「変換されず、かなのまま」になる。利用者が原因に気づけるように、次の2つを行う。

- つながらない状態が 30秒（仮の値）続いたら、fcitx5 の `notifications` モジュールの `sendNotification` で一度だけ知らせる。本文には、`systemctl --user status pazkey-server.socket` で状態を確かめることと、上限に当たったときの戻し方（`systemctl --user reset-failed pazkey-server.socket pazkey-server.service` のあと `systemctl --user start pazkey-server.socket`）を書く。つながったら、次に切れたときにまた知らせられるようにする。
- `showTip` は使わない。「今後表示しない」を選ばれると、二度と出なくなるからである。
- アドオンの `.conf` の `[Addon/OptionalDependencies]` に `notifications` を書く。`notifications()` が `nullptr`（モジュールがない）なら通知しない。
- 通知デーモンがない環境に備えて、つながっていないときに代用の候補を出す場合は、候補ウィンドウの補助テキスト（`setAuxUp`）に「サーバー未接続」と出す。
- `protocol_mismatch` の通知（上記）も同じ方法で送る。

## 画面への反映（render）

- アプリへの反映は `render(state)` だけが行う。キー処理や応答の処理は状態を変えるだけで、直接 `updatePreedit` や `commitString` を呼ばない（確定の `commitString` を除く）。
- `render` は、新しい preedit、カーソル、候補ウィンドウの内容を作り、`last_rendered` と比べる。違うときだけ `updatePreedit()` と `updateUserInterface()` を呼ぶ。
- `commitString` は、引数が空文字列のときは呼ばない。
- preedit の表示: `Composing` ではかな（ライブ変換中は「ライブ変換の表示と確定」の規則で作った文字列）に下線。`Converting` では選択中の候補を強調し、残りの読みに下線。
- 候補ウィンドウの補助テキストは、つながっていないときの「サーバー未接続」だけに使う（「つながらないことを知らせる」）。
- アプリが preedit を表示できない（`CapabilityFlag::Preedit` がない）場合は、fcitx5 の入力パネル側に表示する。

空の commit や変化のない preedit の更新を送ると、LibreOffice、Wayland ネイティブのアプリ、Electron で入力内容が消えることがある。上の規則はその対策である。

## IME の切り替えとフォーカス

| 出来事 | 動作 |
|---|---|
| IME をオフにする、別の IME に切り替える | 入力中の文字があれば、表示している文字列を確定する。候補選択中なら選択中の候補（とその後ろの残りのかな）を確定する（`commit` も送る）。どちらの場合も、そのあと `Empty` へ移り、`reset` を送る。入力中の文字がなければ何もしない |
| フォーカスが外れる | 上と同じ。ただし、アプリに preedit を表示していた（`CapabilityFlag::Preedit` がある）ときは、確定文字列をアドオンからは送らない。fcitx5 自身が、フォーカスが外れたときにアプリの preedit を確定するからである（送ると二重になる）。`commit` と `reset` は送る |
| アプリからのリセット（`reset`。クリックでカーソルが動いたときなど） | 入力を捨てて `Empty` へ移り、`reset` を送る。入力中の文字がなければ何もしない |
| 入力欄が破棄される | 一度でもサーバーへ送っていれば `close` を送る |
| IME をオンにする | 何もしない（`Empty` のまま。preedit の更新も送らない） |

## 左文脈

左文脈は、入力を始めたとき（`Empty` で最初の文字を受けたとき。まだ preedit がない）に決めて `context_base` に入れる。入力の途中では周辺テキストを読み直さない。入力の途中で確定した文字列（部分確定）は、`context_base` の後ろに足す。`convert` の `left_context` は、いつも `context_base` から作る。

入力の途中で周辺テキストを読み直さないのは、周辺テキストに preedit を含めて渡すアプリがあるからである（KWrite では、左文脈の末尾に変換しようとしている読みが入る）。確定した直後にアプリがまだ周辺テキストを更新していない問題も、入力の途中ではこれで起きなくなる。

入力を始めたときの決め方:

- アプリが周辺テキストを渡す場合（`CapabilityFlag::SurroundingText` があり、`surroundingText().isValid()`）: カーソルより前の文字列を使う。選択範囲があるときは、その前（`cursor` と `anchor` の小さいほうより前）を使う。選択範囲は preedit で置き換わるからである。
- 渡さない場合: `committed_context` を使う。フォーカスが外れたら空にする。
- 確定した直後に次の入力を始めると、アプリがまだ周辺テキストを更新していないことがある。そこで、確定したときの周辺テキスト（文字列、`cursor`、`anchor`）と、確定した後の `context_base` を覚えておく。入力を始めたときの周辺テキストがそれと同じなら、まだ反映されていないとみなし、覚えておいた `context_base` を使う。違っていれば、アプリが更新したとみなして周辺テキストから作る。fcitx5 は `commitString` で周辺テキストを更新しない。

どちらの場合も、`left_context` には末尾から `context.max_chars` 文字（コードポイント数）だけを送る。UTF-8 の文字の途中で切らない。
- fcitx5 の `SurroundingText::cursor()` の単位はコードポイント数である（`surroundingtext.cpp` で `utf8::lengthValidated` と比べている）。
- パスワード欄（`CapabilityFlag::Password` または `Sensitive`）では、左文脈を送らず、`commit` も送らない（学習しない）。確定した文字列を `committed_context` と `context_base` に足さず、確定した直後の記録（上記）も残さない。fcitx5 の全体の設定でパスワード欄に入力メソッドを使えるようにしても（`AllowInputMethodForPassword`。既定はオフ）、パスワードが後の左文脈に入らないようにするためである。ライブ変換もしない（「ライブ変換の表示と確定」）。変換キーを押せば `reading` はサーバーへ送る（変換に要るため。サーバーはログに中身を書かない。`docs/server.md`「ログ」）。

## アイコン

入力メソッドのアイコンは `fcitx_pazkey`（`data/icons/fcitx_pazkey.svg`）。田んぼにいる鴫（しぎ）をデフォルメしたもの（由来は README の「名前について」）。鳥を使うので、青地に白い鳥（昔の Twitter のロゴ）に見える形と色は避ける。`hicolor` のテーマの `scalable/apps` に入れ、`pazkey-im.conf` の `Icon` と通知のアイコンに使う。汎用の `input-keyboard` を使わないのは、トレイでキーボード配列（英字）のときと同じに見え、pazkey が有効か分からないからである。トレイでは 16〜22px で表示されるので、細かい描き込みはしない。

名前の区切りはハイフンにしない（Mozc の `fcitx_mozc` と同じく下線にする）。アイコンの名前は、見つからなければ最後の `-` から後ろを削って探し直される。KDE では、hicolor にある `fcitx-pazkey` より先に、Breeze のテーマにある汎用の `fcitx` が見つかり、それが表示される。

## 設定画面

- fcitx5 の設定機構（`FCITX_CONFIGURATION`、`Option<>`）で項目を宣言し、fcitx5-configtool に表示させる。項目は `docs/config.md` のとおり。
- `getConfig()` は、メモリ上の設定（`config.json` から読んだもの）を返す。
- `setConfig(const RawConfig&)` は、受け取った値を `config.json` の形に直し、一時ファイルに書いてから rename する。サーバーへの通知はしない（サーバーがファイルの変更を監視している）。書いたら、その場で `config.json` を読み直す（サーバーにつながっていなくても、フロントエンドが読む項目をすぐ効かせるため）。書けなかったら、fcitx5 の通知で知らせ、メモリ上の設定は変えない。
- `reloadConfig()`（fcitx5 の設定の読み直し）でも `config.json` を読み直す。
- `getConfig` / `setConfig` は `AddonInstance` の仮想関数で、保存先はアドオンが決められる（ヘッダーで確認済み）。fcitx5 側の ini ファイルは使わない。
- 読み直す時と誤りの扱いは「サーバーとの接続」のとおり。
- 検証の規則はサーバーと同じ（`docs/config.md`）。`frontend/build/pazkey-check-config <file>` でフロントエンドの判定を見られ、`tests/config/check.py` がサーバーとスキーマの判定と一致することを確かめる。
- 書き込むときの扱い（画面に出さない項目、知らないキー、不正なファイルへの上書き）は `docs/config.md`「反映の経路」のとおり。
- 項目の名前と説明は日本語で書き込む。翻訳（gettext）は今はしない。
- 利用者に見せる文（状態、ツールチップ、通知）は敬体で書く。項目の名前は名詞句にし、チェックボックスは「〜する」「〜を使う」の形にする。仕様の用語（左文脈、退避モードなど）は画面に出さず、言い換える（左文脈は「直前の文章」、退避モードは「一時停止中」）。
- Zenzai のグループは、上から「状態」「Zenzai を使う」「モデルをダウンロード」「自分で用意したモデル（空ならダウンロードしたものを使う）」（`zenzai.model_path`）「推論回数の上限」の順にする。
- 「状態（画面を開き直すと更新されます）」の行に、Zenzai の状態を出す（下記）。値は選択肢が今の状態1つだけのドロップダウンにして、書き換えられないようにする。`config.json` には書かない。fcitx5-configtool は開いているあいだ設定を問い合わせ直さず、「リセット」でも更新されないので、開き直すことを行の名前に書く。
- 「モデルをダウンロード」のボタン（`ExternalOption`）は、`systemctl --user start --no-block pazkey-fetch-model.service` を起動する（`docs/server.md`「Zenzai のモデルの取得」）。結果は `pazkey-ctl` が通知で知らせる。ボタンのツールチップに、取得先、ライセンス、置く場所を書く。
- 状態は、設定画面を開いたときに作る。サーバーから最後に受けた `zenzai`（`hello`、`config_changed`）と、使うモデルのファイル（`zenzai.model_path`、空なら `$XDG_DATA_HOME/pazkey/zenz.gguf`）があるかどうかから、表の上から順に当てはめて決める。ハッシュは確かめない（メインスレッドで 74MB を読まないため）。

| 状態 | 条件 |
|---|---|
| ダウンロード中 | `zenz.gguf.lock` が `pazkey-ctl` にロックされている |
| 変換サーバーに接続していません | 接続していない |
| 使用中（指定したモデルがないため、前のモデルを使用） | `enabled` で、`zenzai.model_path` が指すファイルがない（反映で読み込めず、前のモデルのまま動いている） |
| 使用中 | `enabled` |
| 無効 | `disabled` |
| 一時停止中 | `fallback` |
| 搭載の CPU をサポートしません | `unsupported` |
| モデルがありません（ダウンロードできます） | `unavailable` で、`zenzai.model_path` が空で、既定の場所にファイルがない |
| 指定したモデルが見つかりません | `unavailable` で、`zenzai.model_path` が指すファイルがない（ダウンロードしても直らない） |
| モデルを読み込めません | `unavailable` で、ファイルがある |

- `zenzai.model_path` は、自分で用意したモデルを使うときだけ書く欄で、文字を打ち込む欄にする。fcitx5 の設定機構には、ファイルを選ぶ画面を出す注釈がない（5.1.23 の `option.h` にあるのは、ツールチップ、フォント、選択肢、一覧の表示名だけ）。空のときにダウンロードしたもの（既定の場所）を使うことは、欄の名前で示す。

## Zenzai の状態を知らせる

設定画面だけを使う利用者が、Zenzai が使われていないことに気づけるようにする。

- `hello` と `config_changed` の `zenzai`（`docs/protocol.md`）を覚えておく。値が `unavailable`、`fallback`、`unsupported` のどれかに変わったら、fcitx5 の通知（「つながらないことを知らせる」と同じ方法）で1回知らせる。
- 同じ状態が続くあいだは2回知らせない。再接続して `hello` で同じ状態を受けたときも知らせない（落ちて起動し直すたびに知らせないため）。`enabled` か `disabled` を受けたら、次にこの3つのどれかになったときにまた知らせられるようにする。
- `unavailable` の本文には、モデルを読み込めなかったので Zenzai なしで変換していることと、モデルがなければ設定画面の Zenzai のグループからダウンロードできること、自分で用意したモデルならパスを確認してほしいことを書く。
- `unsupported` の本文には、AVX2 に対応した CPU が要ることと、Zenzai を使わずに変換を続けることを書く。
- `fallback` の本文には、サーバーの異常終了が続いたので Zenzai を使わずに動いていること（退避モード。画面では「一時的に止めている」と言う）と、設定画面で保存し直すと Zenzai を試し直すことを書く（`docs/server.md`「退避モード」）。

## デバッグログ

不具合の究明のために、操作と状態を1つずつ書くログ。fcitx5 のログの分類 `pazkey` の Debug の水準で書く。ふだんの水準（Info）では書かない。

- 入れる / 切る: fcitx5 の D-Bus のメソッド `org.fcitx.Fcitx.Controller1.SetLogRule` に `pazkey=5`（入れる）/ `pazkey=4`（切る）を渡す。fcitx5 を起動し直さずに切り替えられる。fcitx5 を起動し直すと切れる。サーバーの分（`pazkey-ctl debug on` / `off`）とまとめて、`tools/debug-log.sh on` / `off` で切り替えられる。
- 見る: fcitx5 の標準エラー出力に出る。KDE の Wayland では KWin が起動した fcitx5 の出力として journald に入る。`tools/debug-log.sh show` で、fcitx5 とサーバーのログを合わせて流し見できる。

入力した文章は書かない。読み、候補、確定した文字列、文脈、周辺テキストの中身、打った文字は書かず、次だけを書く。キーは種類（`Char`、`Space`、`Backspace` など）までで、`Char` がどの文字かは書かない（キーの列から文章が復元できるため）。入力欄は `session` の番号で区別する。

| 出来事 | 書くこと |
|---|---|
| キー | キーの種類、アプリへ渡したか、モードの移り変わり、`kana` の長さ、`romaji_tail` の長さ、`cursor`、候補の数と選択中の番号 |
| サーバーへ送った | `type`、`seq`。`convert` では `kind`、読みの長さ、`cursor`、左文脈の長さ。`commit` では `index` |
| サーバーから受けた | `type`、`seq`、候補の数と各候補の `consumed`、使ったか捨てたか（捨てたならその理由）。`hello` と `config_changed` では `zenzai` |
| 代用の候補 | 出した理由（期限切れ、`error`、未接続、読みが長すぎる、使える候補がない）と、候補の数 |
| アプリへ確定した | 確定した文字列の長さ |
| 表示 | preedit と候補ウィンドウを送り直したか |
| 左文脈 | 周辺テキストを使ったか、確定直後の左文脈を使ったか、`committed_context` を使ったか。左文脈の長さ |
| 入力欄の出来事 | IME のオン（アプリのプログラム名と fcitx5 のフロントエンド名も書く） / オフ、フォーカスが外れた、アプリからのリセット、候補のクリック、入力欄の作成と破棄 |
| 接続 | つながった、切れた、`hello`、`config_changed`、`config_rejected` |

中身が要るときは、記録用の偽サーバー（`tests/frontend/fake_server.py`）を、その目的で意図して動かす。
