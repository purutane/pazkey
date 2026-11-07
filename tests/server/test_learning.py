#!/usr/bin/env python3
"""学習がその場で反映される（docs/server.md「commit の処理」）。

「せいさん」を full で変換し、「清算」の番号で commit する。
同じ接続のまま「せいさん」をもう一度変換すると、「清算」が0番になる。
"""

import sys

from pazkey import Client, ServerProcess, TempEnv, texts

TARGET = "清算"

with TempEnv() as env:
    server = ServerProcess(env)
    try:
        c = Client(env.socket)
        before = texts(c.convert(1, 1, "せいさん"))
        assert TARGET in before, f"{TARGET} が候補にない: {before}"
        index = before.index(TARGET)
        assert index > 0, f"{TARGET} が最初から0番なので、テストにならない: {before}"
        c.send({"type": "commit", "session": 1, "seq": 1, "index": index})
        after = texts(c.convert(1, 2, "せいさん"))
        print(f"確定前: {TARGET} は {index} 番 / 確定後: {after[:3]}")
        ok = after[0] == TARGET
    finally:
        server.stop()

print("OK" if ok else "NG: 学習がその場で反映されていない")
sys.exit(0 if ok else 1)
