# 設定仕様

設定は `$XDG_CONFIG_HOME/pazkey/config.json`（既定は `~/.config/pazkey/config.json`）の1ファイルにまとめる。スキーマは `config.schema.json`。

## 要件

1. 利用者が fcitx5-configtool から、ファイルを開かずに設定を変えられる。
2. ファイルを手で編集しても、保存すれば反映される。コマンドは要らない。
3. 設定を書き間違えても、入力は止まらない。

## 項目

「区分」は反映のされ方を表す。前: フロントエンドが読む。軽: サーバーが次の変換から使う。重: サーバーが Zenzai のモデルを読み直す。

| キー | 型 | 既定値 | 範囲 | 区分 | 内容 |
|---|---|---|---|---|---|
| `version` | 整数 | `1` | `1` | - | 設定ファイルの形式の版 |
| `candidates.page_size` | 整数 | `9` | 1〜9 | 前・軽 | 1ページの候補数 |
| `candidates.max_pages` | 整数 | `5` | 1〜20 | 軽 | 取得する最大ページ数 |
| `candidates.select_with_digits` | 真偽 | `true` | - | 前 | 数字キーで候補を確定する |
| `live_conversion.enabled` | 真偽 | `false` | - | 前 | ライブ変換 |
| `context.enabled` | 真偽 | `true` | - | 前 | 左文脈を送る |
| `context.max_chars` | 整数 | `64`※ | 0〜256 | 前・軽 | 送る左文脈の最大文字数（コードポイント数）。サーバーは `maxLeftSideContextLength` にも同じ値を使う。上限は、左文脈を上限まで使っても Zenzai のプロンプト（512 トークン）に読みを評価できる枠が残るように決めた |
| `timeout.full_ms` | 整数 | `1000` | 50〜5000 | 前 | 変換キーを押してから代用の候補を出すまでの時間。CPU で測った変換時間がすべて収まるように決めた |
| `zenzai.enabled` | 真偽 | `true` | - | 重 | Zenzai を使う |
| `zenzai.model_path` | 文字列 | `""` | - | 重 | モデル（gguf）のパス。空なら既定の場所 `$XDG_DATA_HOME/pazkey/zenz.gguf`（`docs/server.md`「Zenzai のモデルの取得」で取得する場所） |
| `zenzai.inference_limit` | 整数 | `10` | 1〜1000 | 軽 | 1回の変換での推論回数の上限（azooKey の既定値） |
| `zenzai.device` | 文字列 | `"auto"` | `auto`、`cpu`、または `pazkey-ctl devices` の名前 | 重 | 推論に使うデバイス。今は GPU に対応しないので、値を読んでも使わない（変えてもモデルは読み直さない）。検証は空でない文字列かどうかだけ |
| `learning.enabled` | 真偽 | `true` | - | 軽 | 学習する |
| `learning.save_every_commits` | 整数 | `20`※ | 1〜1000 | 軽 | 確定がこの回数たまったら保存する |
| `learning.save_idle_seconds` | 整数 | `60`※ | 5〜3600 | 軽 | 最後の確定からこの秒数たったら保存する |
| `fallback.max_crashes` | 整数 | `3` | 1〜100 | 起動時 | 退避モードに入るクラッシュ回数 |
| `fallback.window_seconds` | 整数 | `300` | 10〜86400 | 起動時 | 退避モードの判定に使う時間幅 |

※ 仮の値。実測や実際の使用を経て見直す。

## 設定に持たないもの

キー操作や字種など、Mozc の既定に合わせた入力の振る舞い（`docs/frontend.md`「字種」）は、設定に持たない。フロントエンドのコードの1か所に、Mozc の設定と同じ名前と選択肢の形で、既定値を決め打ちにして置く。切り替えたいという声が出たら、ここの項目の表に足す（`docs/frontend.md`「Mozc の設定との対応」）。

## 例

```json
{
  "version": 1,
  "candidates": { "page_size": 9, "max_pages": 5, "select_with_digits": true },
  "live_conversion": { "enabled": false },
  "context": { "enabled": true, "max_chars": 64 },
  "timeout": { "full_ms": 1000 },
  "zenzai": { "enabled": true, "model_path": "", "inference_limit": 10, "device": "auto" },
  "learning": { "enabled": true, "save_every_commits": 20, "save_idle_seconds": 60 },
  "fallback": { "max_crashes": 3, "window_seconds": 300 }
}
```

## 読み込みと検証

- ファイルがなければ、すべて既定値で動く。ファイルは作らない（画面から保存したときに初めて作られる）。
- 書かれていない項目は既定値を使う。知らないキーは無視し、警告をログに残す。
- 次のどれかに当たったら、**ファイル全体を不正**とみなす。直前に読めた正しい設定（起動直後なら既定値）を使い続け、エラーを報告する。
    - JSON として読めない
    - 型が違う、範囲外、選べない値
    - `version` が知らない値
- 一部の項目だけを反映することはしない。どの設定で動いているかが分かりにくくなるからである。
- エラーの報告先: サーバーはログと `reload_result`（`pazkey-ctl reload` を使った場合）。フロントエンドは fcitx5 の通知（同じエラーで何度も出さない）。

ファイルは RFC 8259 の JSON を UTF-8 で書いたものとする。BOM、末尾のカンマ、`NaN` などの拡張、不正な UTF-8、対になっていないサロゲート（`"\ud800"`）は「JSON として読めない」に当たる。同じキーが2回あれば後のほうを使う。整数の項目は、小数部のない数値（`5.0`、`5e0`）も整数として認める（JSON Schema の `integer` と同じ）。

検証はサーバー（Swift）とフロントエンド（C++）の両方に書く。2つの実装がずれないように、`tests/config/` に正しい例と不正な例のファイルを置き、両方の実装がすべての例で同じ判定をすることをテストで確かめる。`config.schema.json` はその判定の正である。

```sh
cd server && swift build -c release && cd ..
cmake -S frontend -B frontend/build && cmake --build frontend/build
python -I tests/config/check.py   # 置き場所、スキーマ、サーバー、フロントエンドの4つの判定が一致することを確かめる
```

サーバーの判定は `pazkey-server --check-config <file>`、フロントエンドの判定は `frontend/build/pazkey-check-config <file>` で見られる（どちらも、正しければ終了コード 0、不正なら 1）。フロントエンドをビルドしていなければ、`check.py` はフロントエンドの判定を飛ばす。

## 反映の経路

1. **手で編集した場合**: サーバーが `$XDG_CONFIG_HOME/pazkey/` を inotify で監視している。`config.json` が作られたり、書き換えられたり、rename で置き換えられたりしたら、200ms 待って（連続した書き込みをまとめるため）読み直す。正しければ反映し、全クライアントへ `config_changed` を送る。不正なら反映せず、`config_rejected` を送る。フロントエンドはどちらを受けても読み直す（不正なら通知で知らせる）。
2. **画面から変えた場合**: アドオンの `setConfig` が `config.json` を書き換える。あとは1と同じ。
3. **監視が効かない場合の予備**: `pazkey-ctl reload`。

監視するのはファイルではなくディレクトリである。多くの編集器は一時ファイルに書いてから rename するので、ファイルを監視すると置き換えを見落とすからである。

アドオンが書き込むときも、一時ファイルに書いてから rename する。インデントは2文字で、キーの順番は上の表のとおりにする。手で書いたコメントや書式は残らない（JSON にはコメントがない）。

- 書き込むのは表のすべての項目で、画面に出さない項目（`fallback.*`、`zenzai.device`）は、メモリ上の設定の値をそのまま書く。
- 知らないキーは書かない（捨てる）。書式やコメントが残らないのと揃える。
- `config.json` が不正なまま画面から保存したときは、画面の内容（直前に読めた正しい設定に、画面で変えた分を足したもの）で上書きする。画面だけを使う利用者には、それで直るのが自然だからである。不正だったことは、読んだときに通知で知らせている。

## fcitx5-configtool での表示

`fallback.*` 以外の項目を表示する。

| グループ | 項目 |
|---|---|
| 候補 | `candidates.page_size`、`candidates.select_with_digits` |
| 入力 | `live_conversion.enabled` |
| 文脈 | `context.enabled`、`context.max_chars` |
| Zenzai | `zenzai.enabled`、`zenzai.model_path`、`zenzai.inference_limit`（ほかに、状態の行とモデルを取得するボタン。`docs/frontend.md`「設定画面」） |
| 学習 | `learning.enabled` |
| 詳細 | `candidates.max_pages`、`timeout.full_ms`、`learning.save_every_commits`、`learning.save_idle_seconds` |

`zenzai.device` は、GPU に対応するまでは設定画面に出さない。
