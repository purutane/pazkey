#!/usr/bin/env python3
"""古い要求は捨てられる（docs/server.md「convert のまとめ」）。

同じセッションに convert を50件続けて送る。最後の seq の result が必ず返り、
返る result の数は50件より少ない。
"""

import sys

from pazkey import Client, ServerProcess, TempEnv

N = 50
READING = "きょうはいいてんきなのでさんぽにいきましょう"

with TempEnv() as env:
    server = ServerProcess(env)
    try:
        c = Client(env.socket)
        # 50件を1回の書き込みで送る。
        lines = [{"type": "convert", "session": 1, "seq": seq, "kind": "live",
                  "reading": READING[: 1 + seq % len(READING)], "cursor": 1 + seq % len(READING),
                  "left_context": ""} for seq in range(1, N + 1)]
        import json
        c.sock.sendall(b"".join(json.dumps(m, ensure_ascii=False).encode() + b"\n" for m in lines))
        seqs = []
        while True:
            reply = c.recv()
            assert reply["type"] == "result", reply
            seqs.append(reply["seq"])
            if reply["seq"] == N:
                break
        # 後から遅れて届くものがないことを、次の要求の応答が次に来ることで確かめる。
        c.convert(1, N + 1, "おわり")
    finally:
        server.stop()

print(f"返った result: {len(seqs)} 件 {seqs}")
ok = seqs[-1] == N and len(seqs) < N and seqs == sorted(set(seqs))
print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
