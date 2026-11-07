"""tests/server/ のテストが使う部品。サーバーの起動と、プロトコルのクライアント。"""

import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER = pathlib.Path(os.environ.get("PAZKEY_SERVER", ROOT / "server/.build/release/pazkey-server"))
CTL = pathlib.Path(os.environ.get("PAZKEY_CTL", ROOT / "server/.build/release/pazkey-ctl"))
# Zenzai ありで実行するときのモデル。設定すると、TempEnv の設定ファイルにこのモデルを書き、
# サーバーが Zenzai を有効にして起動したかを確かめる。tests/run.sh が設定する。
# 設定しなければ、設定ファイルで Zenzai を切る（既定の場所にモデルがあるかどうかで結果が変わらないように）。
ZENZAI_MODEL = os.environ.get("PAZKEY_TEST_ZENZAI") or None


class Client:
    """1本の接続。1行に JSON を1つ書き、1行ずつ読む。"""

    def __init__(self, path, timeout=10.0, hello=True):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(str(path))
        self.buf = b""
        self.notifications = []
        if hello:
            reply = self.request({"type": "hello", "protocol": 1, "client": "tests", "version": "0"})
            assert reply["type"] == "hello", reply
            self.hello = reply

    def send(self, msg):
        self.sock.sendall(json.dumps(msg, ensure_ascii=False).encode() + b"\n")

    def recv(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("サーバーが接続を閉じた")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def request(self, msg):
        self.send(msg)
        return self.recv()

    def convert(self, session, seq, reading, cursor=None, kind="full", left_context=""):
        if cursor is None:
            cursor = len(reading)  # Python の len はコードポイント数
        self.send({"type": "convert", "session": session, "seq": seq, "kind": kind,
                   "reading": reading, "cursor": cursor, "left_context": left_context})
        reply = self.recv()
        while reply["type"] in ("config_changed", "config_rejected"):  # 通知は読み飛ばす
            self.notifications.append(reply)
            reply = self.recv()
        assert reply["type"] == "result" and reply["seq"] == seq, reply
        return reply

    def close(self):
        self.sock.close()


class TempEnv:
    """XDG の場所を一時ディレクトリに向けた環境。本物の学習データや設定に触らない。"""

    def __init__(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="pazkey-test-"))
        self.env = dict(os.environ)
        for name in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_RUNTIME_DIR"):
            p = self.dir / name.lower()
            p.mkdir(mode=0o700)
            self.env[name] = str(p)
        self.socket = self.dir / "xdg_runtime_dir/pazkey/server.sock"
        self.config_dir = self.dir / "xdg_config_home/pazkey"
        self.config_file = self.config_dir / "config.json"
        self.memory_dir = self.dir / "xdg_state_home/pazkey/memory"
        self.default_model = self.dir / "xdg_data_home/pazkey/zenz.gguf"  # docs/config.md の zenzai.model_path
        self.config_dir.mkdir(parents=True)
        self.config_file.write_text(self.config_json({}))

    def config_json(self, obj):
        """設定ファイルの中身。Zenzai ありで実行しているときはモデルの指定を、そうでなければ Zenzai を切る指定を足す。"""
        zenzai = {"model_path": ZENZAI_MODEL} if ZENZAI_MODEL else {"enabled": False}
        return json.dumps({**obj, "zenzai": {**zenzai, **obj.get("zenzai", {})}})

    def cleanup(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc_type is None:
            self.cleanup()
        else:
            print(f"失敗したので {self.dir} を残す")


class ServerProcess:
    """--listen で直接起動したサーバー。systemd を使わないテスト用。"""

    def __init__(self, env: TempEnv, log_name="server.log", check_zenzai=True):
        self.env = env
        self.log_path = env.dir / log_name
        self.log = open(self.log_path, "ab")
        self.proc = subprocess.Popen([str(SERVER), "--listen", str(env.socket)], env=env.env,
                                     stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"サーバーが起動しなかった:\n{self.log_path.read_text()}")
            try:
                c = Client(env.socket)
            except OSError:
                time.sleep(0.05)
                continue
            if check_zenzai:
                expect_zenzai(c)
            c.close()
            return
        raise RuntimeError("サーバーが待ち受けを始めなかった")

    def stop(self, sig=15, timeout=10):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
            self.proc.wait(timeout)
        self.log.close()
        return self.proc.returncode


def expect_zenzai(client):
    """TempEnv の設定どおりに、サーバーが Zenzai を有効にした / 切ったことを確かめる。"""
    want = "enabled" if ZENZAI_MODEL else "disabled"
    if client.hello["zenzai"] != want:
        raise RuntimeError(f"サーバーの zenzai が {client.hello['zenzai']}（{want} のはず）")


def texts(result):
    return [c["text"] for c in result["candidates"]]
