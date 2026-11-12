# プロトコル仕様

fcitx5 アドオン（以下クライアント）および `pazkey-ctl` と、変換サーバーの間のメッセージを定める。プロトコルの版は **1**。

## 伝送

- UNIX ドメインソケット（stream）。パスは `$XDG_RUNTIME_DIR/pazkey/server.sock`。systemd の `pazkey-server.socket` が待ち受ける。
- 1メッセージ = UTF-8 の JSON オブジェクト1つ + 改行（`\n`）。JSON の中に生の改行は含めない。
- 1行の上限は 1 MiB。超えたら、受け取った側は接続を閉じる。
- どのメッセージにも `type`（文字列）がある。知らないフィールドは無視する（後方互換のため）。

## 文字数の単位

`cursor`、`consumed`、文字数の上限などはすべて **Unicode のコードポイント数** で数える。バイト数でも、書記素クラスタ数でもない。

- クライアント（C++）は UTF-8 を復号して数える。`std::string::size()` を使ってはいけない。
- サーバー（Swift）は `String.unicodeScalars.count` で数える。`String.count` は書記素クラスタ数なので使わない。
- `reading` は NFC で、結合文字を含まず、どの文字（書記素クラスタ）も1つのコードポイントでできていること（ZWJ でつないだ絵文字、国旗、CRLF などを含まない）。満たさなければ、サーバーは `invalid_reading` のエラーを返す。ローマ字から作った読みなら、この条件は自然に満たされる。azooKey の `ComposingText` は書記素クラスタで数えるので、この条件があれば `cursor` と `consumed` の数え方がずれない。
- `reading` の長さの上限は **256** コードポイント。変換時間は読みの長さに比例して増え、変換キューは1本なので、長すぎる読みはほかの入力欄の変換も待たせるからである。超えたら、サーバーは `invalid_reading` のエラーを返す。クライアントは、上限を超える読みを送らない（`docs/frontend.md`「非同期の扱い」）。

## セッション

- セッションは「入力欄1つ分」の変換状態である。クライアントが整数の ID を決め、`session` に入れて送る。ID の範囲は接続ごとに独立している。
- サーバーは、初めて見た `session` で azooKey のセッションを作る（`createSession()`）。`open` は送らなくてよい。
- `close` を受けたとき、または接続が切れたとき、サーバーはそのセッションを破棄する（`removeSession()`）。
- サーバーが再起動するとセッションはすべて消える。クライアントは何もしなくてよい。次の `convert` に必要な情報がすべて載っているので、結果は変わらない（再起動直後の1回だけ遅くなる）。

## 連番

- クライアントは、セッションごとに `seq`（1以上の整数、単調増加）を付けて `convert` を送る。
- サーバーは、同じセッションの `convert` が処理待ちのうちに新しいものが届いたら、古いほうを捨てる。**捨てた `convert` には応答しない。** クライアントは、すべての `seq` に応答が来ることを前提にしてはいけない。
- クライアントは、そのセッションで最後に送った `seq` と一致する `result` だけを使い、それ以外は捨てる。

## メッセージ一覧

| type | 向き | 応答 |
|---|---|---|
| `hello` | クライアント → サーバー | `hello` |
| `convert` | クライアント → サーバー | `result`（捨てた場合はなし） |
| `commit` | クライアント → サーバー | なし |
| `reset` | クライアント → サーバー | なし |
| `close` | クライアント → サーバー | なし |
| `config_changed` | サーバー → クライアント | なし |
| `config_rejected` | サーバー → クライアント | なし |
| `reload` | ctl → サーバー | `reload_result` |
| `status` | ctl → サーバー | `status_result` |
| `devices` | ctl → サーバー | `devices_result` |
| `debug` | ctl → サーバー | `debug_result` |
| `error` | サーバー → クライアント | なし |

## 各メッセージ

### hello

接続したら、最初に必ず送る。

```json
{"type":"hello","protocol":1,"client":"fcitx5-pazkey","version":"0.1.0"}
```

```json
{"type":"hello","protocol":1,"server":"pazkey-server","version":"0.1.0","zenzai":"enabled","config_generation":3}
```

- `zenzai` は `enabled`、`disabled`（設定で無効）、`fallback`（退避モード）、`unavailable`（設定では有効だが、モデルを読み込めなかった。`docs/server.md`「Zenzai のモデルの読み込み」）、`unsupported`（設定では有効だが、llama.cpp が使う命令に CPU が対応していないので、モデルを読み込まない。同じ節）のいずれか。フロントエンドは知らない値を無視する。
- `config_generation` は、サーバーが設定を読み込むたびに1ずつ増える数。
- `protocol` が一致しない場合、サーバーは `error`（`code`: `protocol_mismatch`）を返して接続を閉じる。

### convert

```json
{"type":"convert","session":1,"seq":42,"kind":"full","reading":"きかいをつくる","cursor":7,"left_context":"新しい"}
```

| フィールド | 型 | 内容 |
|---|---|---|
| `session` | 整数 | セッション ID |
| `seq` | 整数 | 連番 |
| `kind` | 文字列 | `live`（ライブ変換。1位の候補だけ返す）か `full`（候補の一覧を返す） |
| `reading` | 文字列 | 未確定の読み（かな）全体。空文字列は不可 |
| `cursor` | 整数 | 読みの中のカーソル位置（コードポイント数）。`0` 以上、`reading` の長さ以下 |
| `left_context` | 文字列 | カーソルより前の確定済みの文字列。なければ空文字列（フィールドを省いても空文字列とみなす）。長さの上限はクライアント側で適用済み |

### result

```json
{"type":"result","session":1,"seq":42,"candidates":[{"text":"機械を作る","consumed":7},{"text":"機会を","consumed":4}],"elapsed_ms":12}
```

| フィールド | 型 | 内容 |
|---|---|---|
| `candidates` | 配列 | 候補を順位の順に並べたもの。`live` なら要素は1つ以下 |
| `candidates[].text` | 文字列 | 表記 |
| `candidates[].consumed` | 整数 | この候補を確定すると、`reading` の先頭から何文字（コードポイント）を使うか |
| `elapsed_ms` | 整数 | サーバー内で変換にかかった時間。測定用 |

候補の番号（`commit` の `index`）は、この配列の添字（0始まり）である。

### commit

候補を確定したときに送る。応答はない。クライアントは応答を待たずに、確定文字列をアプリへ送ってよい。

```json
{"type":"commit","session":1,"seq":42,"index":1}
```

- `seq` は、確定した候補が入っていた `result` の `seq`。
- サーバーは、`seq` がそのセッションで最後に返した `result` と一致し、`index` が範囲内のときだけ学習する。一致しなければ黙って無視する。入力には影響せず、学習が1回抜けるだけである。
- 1つの `result` で学習するのは1回だけ。同じ `seq` の `commit` が2回届いたら、2回目は無視する。
- `commit`、`reset`、`close` の `session` をサーバーが知らない（その接続で `convert` を受けていない）場合も、黙って無視する。

### reset

入力が空になったときに送る。取り消し（Escape など）でも確定でも、経路を問わず送る（`docs/frontend.md`「Empty へ移るとき」）。サーバーはそのセッションの変換途中の状態を捨てる（`stopComposition()`）。何度届いても害はない。

```json
{"type":"reset","session":1}
```

### close

入力欄が破棄されたときに送る。

```json
{"type":"close","session":1}
```

### config_changed

サーバーが設定を読み直して反映したとき（監視による読み直しと `reload` のどちらでも）、接続中のすべての接続（`pazkey-ctl` を含む）へ送る。設定が不正で反映しなかったときは送らない。クライアントは `config.json` を読み直す。

```json
{"type":"config_changed","config_generation":4,"zenzai":"enabled"}
```

- `zenzai` は、反映したあとの Zenzai の状態。値は `hello` と同じ。重い項目の反映でモデルを読み込めなかったことや、退避モードが解除されたことを、クライアントが知るために使う。

### config_rejected

サーバーが設定を読み直したが、不正だったので反映しなかったとき（監視による読み直しと `reload` のどちらでも）、接続中のすべての接続へ送る。`config_generation` は増えない。誤りの中身は載せない。クライアントは `config.json` を読み直して、自分で誤りを見つける（フロントエンドは fcitx5 の通知で知らせる。`docs/frontend.md`「サーバーとの接続」）。`config_changed`（反映した）とは種類を分け、意味を混ぜない。

```json
{"type":"config_rejected","config_generation":3}
```

### reload / reload_result

`pazkey-ctl reload` が送る。サーバーは設定を読み直す。

```json
{"type":"reload"}
```

```json
{"type":"reload_result","ok":false,"config_generation":3,"errors":[{"path":"/zenzai/inference_limit","message":"1〜1000 の整数でなければならない"}]}
```

`ok` が `false` のとき、サーバーは直前の設定を使い続けている。

`ok` が `true` でも、Zenzai のモデルを読み込めなかったときは、`errors` に `path` が `/zenzai` の1件が入る。このとき Zenzai の部分（有無、モデル、デバイス）は前の状態のままで、ほかの項目は反映している（`docs/server.md`「Zenzai のモデルの読み込み」）。

### status / status_result

```json
{"type":"status"}
```

```json
{"type":"status_result","version":"0.1.0","zenzai":"enabled","device":"Vulkan0","config_generation":3,"sessions":2,"uptime_s":5123,"debug":false}
```

`device` は推論に使っているデバイスの名前。Zenzai を使っていない（`zenzai` が `enabled` 以外）ときは `none`。`sessions` は、接続中のクライアントが使っているセッションの数。`debug` は、デバッグログを書いているか（下記）。

### devices / devices_result

```json
{"type":"devices"}
```

```json
{"type":"devices_result","devices":[{"index":0,"name":"Vulkan0","description":"AMD Radeon RX 9070 (RADV)"},{"index":-1,"name":"CPU","description":"CPU"}]}
```

今は GPU に対応しないので、CPU だけを返す。

### debug / debug_result

`pazkey-ctl debug on` / `off` が送る。サーバーはデバッグログ（`docs/server.md`「ログ」）を書き始める / やめる。この状態は設定ファイルに持たず、サーバーを起動し直すと切れる。

```json
{"type":"debug","enabled":true}
```

```json
{"type":"debug_result","enabled":true}
```

`enabled` が真偽値でなければ、`malformed` のエラーを返す。

### error

```json
{"type":"error","session":1,"seq":42,"code":"invalid_reading","message":"reading に結合文字が含まれている"}
```

| code | 意味 | 接続 |
|---|---|---|
| `protocol_mismatch` | プロトコルの版が違う | 閉じる |
| `malformed` | JSON として読めない、必須フィールドがない | 維持する |
| `unknown_type` | 知らない `type` | 維持する |
| `invalid_reading` | `reading` が空、長すぎる、または条件を満たさない | 維持する |
| `invalid_cursor` | `cursor` が範囲外 | 維持する |
| `internal` | サーバー内部のエラー | 維持する |

`session` と `seq` は、原因になったメッセージにあった場合だけ付ける。

## やりとりの例

### 入力して、全部を確定する

```
C→S {"type":"convert","session":1,"seq":1,"kind":"live","reading":"き","cursor":1,"left_context":""}
C→S {"type":"convert","session":1,"seq":2,"kind":"live","reading":"きか","cursor":2,"left_context":""}
     （seq 1 がまだ処理待ちなら、サーバーは seq 1 を捨てる）
S→C {"type":"result","session":1,"seq":2,"candidates":[{"text":"機下","consumed":2}],"elapsed_ms":4}
C→S {"type":"convert","session":1,"seq":3,"kind":"full","reading":"きかい","cursor":3,"left_context":""}
S→C {"type":"result","session":1,"seq":3,"candidates":[{"text":"機会","consumed":3},{"text":"機械","consumed":3}],"elapsed_ms":9}
C→S {"type":"commit","session":1,"seq":3,"index":1}
     （クライアントは「機械」をアプリへ確定。サーバーは学習し、読みが残っていないので stopComposition）
C→S {"type":"reset","session":1}
     （入力が空になったので送る。docs/frontend.md「Empty へ移るとき」）
```

### 先頭の文節だけを確定する

```
C→S {"type":"convert","session":1,"seq":10,"kind":"full","reading":"きかいをつくる","cursor":7,"left_context":""}
S→C {"type":"result","session":1,"seq":10,"candidates":[{"text":"機械を","consumed":4}, …],"elapsed_ms":15}
C→S {"type":"commit","session":1,"seq":10,"index":0}
     （クライアントは「機械を」を確定し、読みを「つくる」にする）
C→S {"type":"convert","session":1,"seq":11,"kind":"full","reading":"つくる","cursor":3,"left_context":"機械を"}
```

ここで例に挙げた候補の文字列は説明用であり、実際の変換結果ではない。
