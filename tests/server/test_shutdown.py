#!/usr/bin/env python3
"""SIGTERM で学習を保存して終わる（docs/server.md「学習の保存」）。

確定してから SIGTERM を送る。終了コードが 0 で、起動し直しても学習が残っている。
"""

import sys

from pazkey import Client, ServerProcess, TempEnv, texts

TARGET = "清算"

with TempEnv() as env:
    server = ServerProcess(env)
    c = Client(env.socket)
    first = texts(c.convert(1, 1, "せいさん"))
    c.send({"type": "commit", "session": 1, "seq": 1, "index": first.index(TARGET)})
    c.convert(1, 2, "せいさん")  # commit が処理されたことを、次の応答で確かめる
    code = server.stop(sig=15)
    log = server.log_path.read_text()
    saved = sorted(p.name for p in env.memory_dir.iterdir())

    server = ServerProcess(env)
    try:
        after = texts(Client(env.socket).convert(1, 1, "せいさん"))
    finally:
        server.stop()

print(f"終了コード {code} / 学習データ {saved} / 再起動後: {after[:3]}")
print("\n".join(l for l in log.splitlines() if "保存" in l))
ok = code == 0 and saved and after[0] == TARGET
print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
