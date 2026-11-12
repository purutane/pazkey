# pazkey

fcitx5 で使う日本語入力（かな漢字変換）です。変換エンジンには [azooKey](https://github.com/azooKey/AzooKeyKanaKanjiConverter) を使い、ニューラル変換の Zenzai にも対応しています。Arch Linux 向けに、AUR で配っています。

pazkey は次の方針で作っています。

- 変換サーバーが落ちても、固まっても、文字の入力は止まりません。サーバーは systemd が起動し直し、その間もかなで入力を続けられます。
- 入力中の状態は fcitx5 のアドオンが持ち、サーバーは変換と学習だけを受け持ちます。
- 設定は fcitx5-configtool の画面から変えられます。中身は `~/.config/pazkey/config.json` の1ファイルで、手で書き換えても保存すれば反映されます。設定を書き間違えても、直前の正しい設定で動き続けます。

## 状態

まだ試してもらう段階の版です。確かめているのは Arch Linux（x86_64）だけです。気づいたことは「[報告の仕方](#報告の仕方)」のとおりに知らせてもらえると助かります。

## 必要なもの

- Arch Linux（x86_64）と、fcitx5 を入力メソッドとして使える状態になっていること。設定画面を使うには fcitx5-configtool も要ります。
- systemd 254 以上のユーザーセッション。
- Zenzai を使うには、AVX2 に対応した CPU（Intel は 2013年の Haswell 以降、AMD は 2015年の Excavator 以降）。対応していなくても、Zenzai なしの変換は使えます。

## インストール

AUR に2つの版があります。ふつうはビルド済みの `fcitx5-pazkey-bin` を入れてください。

> [!NOTE]
> AUR には準備中です（AUR の新しいアカウントの登録が止まっているため）。それまでは、次の手順で `fcitx5-pazkey-bin` を入れてください。下の手順1 の代わりになります。
>
> ```sh
> git clone https://github.com/purutane/pazkey.git
> cd pazkey/packaging/fcitx5-pazkey-bin
> makepkg -si
> ```

| パッケージ | 中身 |
|---|---|
| [`fcitx5-pazkey-bin`](https://aur.archlinux.org/packages/fcitx5-pazkey-bin) | ビルド済みのもの。すぐに入ります |
| [`fcitx5-pazkey`](https://aur.archlinux.org/packages/fcitx5-pazkey) | 自分の機械でソースからビルドします。ビルドに Swift のツールチェーン（AUR の [`swift-bin`](https://aur.archlinux.org/packages/swift-bin)、約 3.5GB）が要ります |

1. `fcitx5-pazkey-bin` を入れます。AUR ヘルパーを使っているなら `paru -S fcitx5-pazkey-bin` などで、使っていなければ次のとおりです。

    ```sh
    git clone https://aur.archlinux.org/fcitx5-pazkey-bin.git
    cd fcitx5-pazkey-bin
    makepkg -si
    ```

2. ログインし直します。fcitx5 にアドオンを読み込ませ、変換サーバーを待ち受けさせるためです。
3. fcitx5-configtool の「入力メソッド」で「pazkey」を足します。

入力メソッドを pazkey に切り替えて、`kanji` と打って Space を押し、候補が出れば入っています。

ソースからビルドする `fcitx5-pazkey` を入れるときは、先に `swift-bin` を入れてから、同じ手順で `fcitx5-pazkey` を入れてください。`swift-bin` はビルドにだけ使います。pazkey は使う Swift のランタイムを同梱しているので、入れたあとは `sudo pacman -Rns swift-bin` で外してかまいません（版を上げるときにまた要ります）。2つの版は同時には入れられません。

版を上げたあとは、ログインし直してください。

## Zenzai のモデル

Zenzai のモデルは pazkey に含まれていません。使うときは、次のどちらかで取得してください。

- fcitx5-configtool で pazkey の設定を開き、Zenzai のグループの「モデルをダウンロード」を押す。終わるとデスクトップの通知で知らせます。
- 端末で `pazkey-ctl fetch-model` を実行する。

取得するのは [Miwa-Keita/zenz-v3.2-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf)（Apache-2.0）の決まった版で、約 74MB です。ハッシュを確かめてから `~/.local/share/pazkey/zenz.gguf` に置き、すぐに使い始めます。

使われているかは、設定画面の Zenzai のグループの「状態」の行（画面を開き直すと更新されます）か、端末で `pazkey-ctl status` を実行して見られます。`status` の `zenzai` が `enabled` なら使えています。

モデルがなくても、辞書だけで変換できます。自分で用意したモデルを使うときは、設定の「自分で用意したモデル」にそのファイルのパスを書いてください。

pazkey が外部と通信するのは、このモデルの取得だけです。変換サーバーは、systemd の設定でネットワークを使えないようにしてあります（[docs/server.md](docs/server.md) の「外部との通信を塞ぐ」）。

## 使い方

キーの動きは Mozc に近づけています。主なものは次のとおりです。

| キー | 動作 |
|---|---|
| Space、変換 | 変換する。候補が出ているときは次の候補 |
| Shift+Space、↑ | 前の候補 |
| 1〜9 | そのページの候補を確定する（設定で切れます） |
| Enter | 確定する |
| Escape | 変換をやめて読みに戻る。読みの入力中なら入力を捨てる |
| Shift+← / Shift+→ | 変換する範囲を縮める / 伸ばす |
| F6〜F10 | ひらがな、全角カタカナ、半角カタカナ、全角英数、半角英数にする |

すべてのキーの動きは [docs/frontend.md](docs/frontend.md) の「キーごとの動作」にあります。設定できる項目（候補の数、ライブ変換、学習など）は [docs/config.md](docs/config.md) にあります。

## うまく動かないとき

- **入力メソッドの一覧に pazkey が出ない、切り替えても変換されない（かなのまま）**: 入れたあとにログインし直したか確かめてください。ログインし直さずに使うなら、`systemctl --user start pazkey-server.socket` を実行し、fcitx5 を起動し直してください。
- **変換サーバーの状態を見る**: 端末で `pazkey-ctl status` を実行します。つながらないと出るときは、`systemctl --user status pazkey-server.socket pazkey-server.service` を見てください。
- **サーバーが落ち続けて、変換が戻らない**: systemd は間隔を延ばしながら（最長10秒）起動し直します。それでもソケットが止まったときは、次のコマンドで戻してください。

    ```sh
    systemctl --user reset-failed pazkey-server.socket pazkey-server.service
    systemctl --user start pazkey-server.socket
    ```

- **Zenzai を使わずに動いていると通知が出た**: モデルがないか、読み込めなかったときです。通知の文面のとおり、設定画面からモデルを取得するか、自分で指したモデルのパスを確かめてください。サーバーが続けて落ちたために Zenzai を一時的に止めたときは、設定画面で保存し直すと Zenzai をもう一度試します。「搭載の CPU をサポートしません」という通知のときは、CPU が AVX2 に対応していないので、その機械では Zenzai を使えません。

変換サーバーはログインすると起動します。止めたいときは `systemctl --user mask --now pazkey-server.socket` を実行してください（`disable` は使えません）。戻すときは `unmask` してからログインし直します。

## アンインストール

```sh
sudo pacman -Rns fcitx5-pazkey-bin
```

ソースからビルドした版なら `fcitx5-pazkey` です。

外してからログインし直してください。fcitx5-configtool の入力メソッドからも pazkey を外してください。次の利用者のデータは残るので、要らなければ消してください。

| 場所 | 中身 |
|---|---|
| `~/.config/pazkey/` | 設定 |
| `~/.local/share/pazkey/` | Zenzai のモデル |
| `~/.local/state/pazkey/` | 学習したデータ、起動の記録 |

## 報告の仕方

[GitHub の issue](https://github.com/purutane/pazkey/issues/new/choose) で知らせてください。テンプレートが2つあります。

- **使っていて気づいたこと**: 変換の結果、キーの動き、表示など。
- **インストールできない・動かない**: ビルドに失敗した、一覧に出ない、変換されない、モデルを取得できないなど。

どちらも、全部の欄を埋めなくてかまいません。issue は公開されるので、入力した文章を書くときは、公開してよいものだけにしてください。`pazkey-ctl status` の出力とサーバーのログには、入力した文章は含まれません。

## 文書

pazkey が約束する振る舞いと、その取り決めです。

| 文書 | 内容 |
|---|---|
| [docs/design.md](docs/design.md) | 全体像と設計原則 |
| [docs/frontend.md](docs/frontend.md) | fcitx5 アドオンの仕様（キーの動き、表示、設定画面） |
| [docs/server.md](docs/server.md) | 変換サーバーの仕様（依存、入れる場所、systemd のユニット、`pazkey-ctl`） |
| [docs/protocol.md](docs/protocol.md) | アドオンとサーバーの間のメッセージの仕様 |
| [docs/config.md](docs/config.md) | 設定の項目と反映の仕組み |

## 謝辞

pazkey は [hazkey](https://github.com/7ka-Hiira/hazkey) を大いに参考にして作りました。hazkey とその作者に感謝します。

変換には、[azooKey](https://github.com/azooKey/AzooKeyKanaKanjiConverter) の変換エンジンと辞書を使わせてもらっています。ニューラル変換には Zenzai と、[zenz](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf) のモデルを使わせてもらっています。それぞれの作者と貢献者に感謝します。

ローマ字かな変換の表は [Mozc](https://github.com/google/mozc) のものを使わせてもらっています。そのほかに使わせてもらっているソフトウェアとデータは、[`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES) にまとめてあります。

## 名前について

pazkey は purutane + azKey です。hazkey の名前の響きにならって付けました。paz はスペイン語で「平和」を意味するそうなので、その縁起にもあやかっています。

アイコンは、田んぼにいる鴫（しぎ）です。日本の原風景といえば田んぼ、田んぼにいる鳥といえば鴫で、しかも「鴫」は「田」と「鳥」を合わせた、日本で作られた漢字（国字）です。のどかな田んぼの風景は平和にも通じるので、日本語の入力にうってつけだと思って選びました。

## ライセンス

pazkey は [MIT](LICENSE) です。同梱する第三者のソフトウェアやデータのライセンスは [`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES) にまとめてあります。
