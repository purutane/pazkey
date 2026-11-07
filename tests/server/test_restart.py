#!/usr/bin/env python3
"""落ちても続けられる（docs/server.md「systemd ユニット」）。

変換の途中で kill -9 でサーバーを落とす。次の接続がそのまま通り（systemd のソケット起動）、
同じ convert に同じ result が返る。

本物のユニットや学習データに触れないよう、テスト専用の名前のユニットを
$XDG_RUNTIME_DIR/systemd/user/（実行時だけのユニットの置き場所）に作り、終わったら消す。
ソケットと XDG の場所は一時ディレクトリに向ける。
"""

import os
import pathlib
import signal
import subprocess
import sys
import time

from pazkey import SERVER, Client, TempEnv, expect_zenzai, texts

READING = "きょうはいいてんきなのでさんぽにいきましょう"


def systemctl(*args, check=True):
    return subprocess.run(["systemctl", "--user", *args], check=check, capture_output=True, text=True)


def main_pid(unit):
    out = systemctl("show", "-p", "MainPID", "--value", unit).stdout.strip()
    return int(out or 0)


def wait_for_pid(unit, not_equal=0, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pid = main_pid(unit)
        if pid and pid != not_equal:
            return pid
        time.sleep(0.05)
    raise RuntimeError(f"{unit} が起動しなかった")


name = f"pazkey-test-{os.getpid()}"
unit_dir = pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / "systemd/user"

with TempEnv() as env:
    env.socket.parent.mkdir(mode=0o700, parents=True)
    xdg = "\n".join(f"Environment={k}={env.env[k]}" for k in
                    ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME"))
    unit_dir.mkdir(parents=True, exist_ok=True)
    (unit_dir / f"{name}.socket").write_text(f"""[Socket]
ListenStream={env.socket}
SocketMode=0600
""")
    # 再起動の行は docs/server.md のユニットと同じにする。
    (unit_dir / f"{name}.service").write_text(f"""[Unit]
Requires={name}.socket
StartLimitIntervalSec=0

[Service]
ExecStart={SERVER}
{xdg}
Restart=on-failure
RestartSec=200ms
RestartSteps=5
RestartMaxDelaySec=10s
TimeoutStopSec=3s
""")
    service = f"{name}.service"
    ok = False
    try:
        systemctl("daemon-reload")
        systemctl("start", f"{name}.socket")

        c = Client(env.socket)
        expect_zenzai(c)
        expected = c.convert(1, 1, READING)
        pid1 = wait_for_pid(service)

        # 変換の途中で落とす: convert を送った直後に SIGKILL。
        c.send({"type": "convert", "session": 1, "seq": 2, "kind": "full", "reading": READING + "ね",
                "cursor": len(READING) + 1, "left_context": ""})
        os.kill(pid1, signal.SIGKILL)
        try:
            while True:
                c.recv()  # 結果が間に合っていても、いずれ EOF になる
        except (EOFError, ConnectionResetError):
            pass
        c.close()

        # 次の接続がそのまま通る。サーバーの再起動を待たずに、すぐ接続する。
        t0 = time.monotonic()
        c2 = Client(env.socket)
        again = c2.convert(1, 1, READING)
        elapsed = time.monotonic() - t0
        pid2 = wait_for_pid(service, not_equal=pid1)
        c2.close()

        print(f"落とす前の PID {pid1} / 後の PID {pid2} / 再接続から結果まで {elapsed * 1000:.0f}ms")
        print(f"1位: {texts(expected)[0]} / {texts(again)[0]}、候補数: {len(expected['candidates'])} / {len(again['candidates'])}")
        ok = pid1 != pid2 and expected["candidates"] == again["candidates"]
    finally:
        systemctl("stop", f"{name}.socket", service, check=False)
        for suffix in ("socket", "service"):
            (unit_dir / f"{name}.{suffix}").unlink(missing_ok=True)
        systemctl("daemon-reload", check=False)
        systemctl("reset-failed", service, check=False)

print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
