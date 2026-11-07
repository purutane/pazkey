#!/usr/bin/env python3
"""学習データの書き込みの途中で落ちても壊れないか（docs/server.md「学習の保存」）。

azooKey の保存（LongTermLearningMemory.merge）は次の順で進む。
  1. 新しい内容を `.2` の付いたファイルに書く
  2. `.pause` を作る
  3. `.2` を元のファイルへコピーする（`.louds` が最後）
  4. `.pause` を消す
1 の途中と 3 の途中で落ちた状態をファイルを壊して作り、サーバーを起動し直して学習が残るかを見る。
"""

import sys

from pazkey import Client, ServerProcess, TempEnv, texts

TARGET = "清算"
checks = []


def check(label, cond, detail=""):
    checks.append(cond)
    print(f"{'OK' if cond else 'NG'} {label} {detail}")


def top(env):
    server = ServerProcess(env)
    try:
        return texts(Client(env.socket).convert(1, 1, "せいさん"))[0]
    finally:
        server.stop()


with TempEnv() as env:
    # 学習して保存する
    server = ServerProcess(env)
    c = Client(env.socket)
    first = texts(c.convert(1, 1, "せいさん"))
    c.send({"type": "commit", "session": 1, "seq": 1, "index": first.index(TARGET)})
    c.convert(1, 2, "て")
    server.stop()
    mem = env.memory_dir
    check("保存した学習が効く", top(env) == TARGET)

    # 1 の途中で落ちた: `.2` が書きかけ（中身を壊す）で、`.pause` はない
    for p in mem.glob("*.2"):
        p.write_bytes(p.read_bytes()[: len(p.read_bytes()) // 3])
    check("`.2` の書きかけ: 元のファイルが使われる", top(env) == TARGET)

    # 学習し直して保存し、`.2` を正しい内容に戻す
    server = ServerProcess(env)
    c = Client(env.socket)
    c.send({"type": "commit", "session": 1, "seq": 1, "index": 0})  # まだ convert していないセッションの commit は無視される
    r = texts(c.convert(1, 1, "せいさん"))
    c.send({"type": "commit", "session": 1, "seq": 1, "index": r.index(TARGET)})
    c.convert(1, 2, "て")
    server.stop()
    check("正常に保存すると `.pause` は残らない", not (mem / ".pause").exists())

    # 3 の途中で落ちた: `.pause` があり、元のファイルの一部が消えたり書きかけだったりする
    (mem / ".pause").write_bytes(b"")
    (mem / "memory.louds").unlink()
    meta = mem / "memory.memorymetadata"
    meta.write_bytes(meta.read_bytes()[:7])
    result = top(env)
    check("コピーの途中: 起動し直すと `.2` から戻り、学習が残る", result == TARGET, result)
    check("戻したあと `.pause` は消える", not (mem / ".pause").exists())
    check("戻したあと元のファイルがそろう", (mem / "memory.louds").exists()
          and meta.read_bytes() == (mem / "memory.memorymetadata.2").read_bytes())

ok = all(checks)
print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
