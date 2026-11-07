#!/usr/bin/env python3
"""読みの長さの上限（docs/protocol.md「文字数の単位」）。

上限ちょうどの読みは変換でき、1文字でも超えると invalid_reading が返り、
接続はそのまま使える。あわせて、読みの長さごとの変換時間を表示する。
"""

import sys

from pazkey import Client, ServerProcess, TempEnv

LIMIT = 256
UNIT = "きょうはいいてんきなのでさんぽにいきましょう"


def reading(n):
    return (UNIT * (n // len(UNIT) + 1))[:n]


ok = True
with TempEnv() as env:
    server = ServerProcess(env)
    try:
        c = Client(env.socket)
        seq = 0
        for n in (32, 64, 128, LIMIT):
            seq += 1
            r = c.convert(1, seq, reading(n))
            print(f"{n} 文字: {r['elapsed_ms']}ms、候補 {len(r['candidates'])} 件")
            c.send({"type": "reset", "session": 1})
        seq += 1
        c.send({"type": "convert", "session": 1, "seq": seq, "kind": "full",
                "reading": reading(LIMIT + 1), "cursor": LIMIT + 1, "left_context": ""})
        err = c.recv()
        print(f"{LIMIT + 1} 文字: {err}")
        ok &= err.get("type") == "error" and err.get("code") == "invalid_reading" and err.get("seq") == seq
        # 接続はそのまま使える。
        seq += 1
        ok &= c.convert(1, seq, "おわり")["seq"] == seq
    finally:
        server.stop()

print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
