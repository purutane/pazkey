#!/usr/bin/env python3
"""記録用の偽サーバー（tests/frontend/README.md の 6〜8 で使う）。

受け取った行をそのまま書き出し、convert には決まった形の候補を返す。本物のサーバーは commit や文脈の
中身をログに書かないので、フロントエンドが何を送ったかはこれで確かめる。

使い方（本物のサーバーのユニットを止めてから、同じソケットで動かす）:

    systemctl --user stop pazkey-server.socket pazkey-server.service
    python tests/frontend/fake_server.py [--log 記録ファイル]
    # 終わったら Ctrl+C で止め、systemctl --user start pazkey-server.socket

返す候補（full のとき。cursor までを変換範囲とする）:

    0: 範囲全体をカタカナにしたもの（consumed = cursor）
    1: 範囲の先頭2文字だけをカタカナにしたもの（consumed = 2。範囲が3文字以上のとき）
    最後: 範囲全体のひらがな

1番の候補を選ぶと「文節を1つだけ確定して残りを変換する」を試せる（tests/frontend/README.md の 8）。

convert ごとに、left_context が正しい UTF-8 か、長さ（コードポイント数）がいくつかを書き出す（tests/frontend/README.md の 7）。

本物のサーバーと同じく、設定ファイル（$XDG_CONFIG_HOME/pazkey/config.json）が変わったら、
つながっているすべての接続へ config_changed を送る。アドオンはそれを受けて設定を読み直す。
（中身の検証はしない。不正な設定を送り込んだときの振る舞いは、本物のサーバーで確かめる。）
"""

import argparse
import json
import os
import pathlib
import signal
import socket
import sys
import threading
import time


def katakana(s):
    return "".join(chr(ord(c) + 0x60) if "ぁ" <= c <= "ゖ" else c for c in s)


def default_config():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return pathlib.Path(base) / "pazkey" / "config.json"


class Connections:
    """つながっている接続。config_changed を全員へ送るために持つ。"""

    def __init__(self):
        self.lock = threading.Lock()
        self.socks = set()

    def add(self, s):
        with self.lock:
            self.socks.add(s)

    def remove(self, s):
        with self.lock:
            self.socks.discard(s)

    def broadcast(self, msg):
        data = json.dumps(msg, ensure_ascii=False).encode() + b"\n"
        with self.lock:
            for s in list(self.socks):
                try:
                    s.sendall(data)
                except OSError:
                    pass


def watch_config(path, conns, rec):
    """設定ファイルの変更（作成、書き換え、置き換え、削除）を見張る。0.5 秒ごとに見る。"""

    def stamp():
        try:
            st = path.stat()
            return (st.st_mtime_ns, st.st_size, st.st_ino)
        except FileNotFoundError:
            return None

    last = stamp()
    generation = 1
    while True:
        time.sleep(0.5)
        now = stamp()
        if now != last:
            last = now
            generation += 1
            rec.write(f"# {path} が変わった。config_changed を送る（generation {generation}）")
            conns.broadcast({"type": "config_changed", "config_generation": generation})


def default_socket():
    runtime = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    return pathlib.Path(runtime) / "pazkey" / "server.sock"


class Recorder:
    def __init__(self, path):
        self.lock = threading.Lock()
        self.file = open(path, "a", encoding="utf-8") if path else None

    def write(self, text):
        with self.lock:
            print(text, flush=True)
            if self.file:
                self.file.write(text + "\n")
                self.file.flush()


def candidates_for(reading, cursor):
    target = reading[:cursor]
    cands = [{"text": katakana(target), "consumed": cursor}]
    if cursor >= 3:
        cands.append({"text": katakana(target[:2]), "consumed": 2})
    cands.append({"text": target, "consumed": cursor})
    return cands


def handle(conn, rec, conn_id, conns):
    buf = b""
    rec.write(f"# 接続 {conn_id}")
    conns.add(conn)

    def send(msg):
        conn.sendall(json.dumps(msg, ensure_ascii=False).encode() + b"\n")

    try:
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError:
                    rec.write(f"[{conn_id}] 不正な UTF-8: {raw!r}")
                    continue
                rec.write(f"[{conn_id}] {text}")
                try:
                    msg = json.loads(text)
                except ValueError:
                    rec.write(f"[{conn_id}] JSON として読めない")
                    continue
                t = msg.get("type")
                if t == "hello":
                    send({"type": "hello", "protocol": 1, "server": "fake-server", "version": "0",
                          "zenzai": "disabled", "config_generation": 1})
                elif t == "convert":
                    ctx = msg.get("left_context", "")
                    # json.loads が通った時点で、文字列は正しい UTF-8 だった（不正なら上で弾いている）。
                    rec.write(f"[{conn_id}]   left_context: 長さ {len(ctx)} コードポイント、"
                              f"末尾 {ctx[-10:]!r}")
                    reading, cursor = msg["reading"], msg["cursor"]
                    send({"type": "result", "session": msg["session"], "seq": msg["seq"],
                          "candidates": candidates_for(reading, cursor), "elapsed_ms": 0})
    finally:
        conns.remove(conn)
        rec.write(f"# 切断 {conn_id}")
        conn.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--socket", type=pathlib.Path, default=default_socket())
    ap.add_argument("--log", type=pathlib.Path)
    ap.add_argument("--config", type=pathlib.Path, default=default_config())
    args = ap.parse_args()

    args.socket.parent.mkdir(parents=True, exist_ok=True)
    if args.socket.exists():
        args.socket.unlink()
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(str(args.socket))
    srv.listen(16)
    rec = Recorder(args.log)
    rec.write(f"# {args.socket} で待ち受ける。{args.config} を見張る")
    conns = Connections()
    threading.Thread(target=watch_config, args=(args.config, conns, rec), daemon=True).start()
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
    n = 0
    try:
        while True:
            conn, _ = srv.accept()
            n += 1
            threading.Thread(target=handle, args=(conn, rec, n, conns), daemon=True).start()
    finally:
        srv.close()
        args.socket.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
