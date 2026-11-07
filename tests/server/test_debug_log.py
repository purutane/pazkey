#!/usr/bin/env python3
"""デバッグログ（docs/server.md「デバッグログ」）。

`pazkey-ctl debug on` で操作と状態が書かれ、`off` で書かれなくなる。
どちらでも、読み、文脈、候補の文字列はログに出ない。
"""

import json
import subprocess
import sys

from pazkey import CTL, Client, ServerProcess, TempEnv, texts

READING = "きかいをつくる"
CONTEXT = "ひみつのぶんみゃく"


def ctl(env, *args):
    return subprocess.run([str(CTL), *args], env=env.env, capture_output=True, text=True, timeout=10)


def debug_lines(text):
    return [line for line in text.splitlines() if line.startswith("debug:")]


failures = []


def check(ok, what):
    print(("OK  " if ok else "NG  ") + what)
    if not ok:
        failures.append(what)


with TempEnv() as env:
    server = ServerProcess(env)
    try:
        c = Client(env.socket)
        c.convert(1, 1, READING, left_context=CONTEXT)
        before = server.log_path.read_text()
        check(not debug_lines(before), "既定ではデバッグログを書かない")

        r = ctl(env, "debug", "on")
        check(r.returncode == 0, f"debug on が成功する: {r.stdout.strip()} {r.stderr.strip()}")
        check("debug: on" in ctl(env, "status").stdout, "status に debug: on と出る")

        result = c.convert(1, 2, READING, left_context=CONTEXT)
        candidates = texts(result)
        c.send({"type": "commit", "session": 1, "seq": 2, "index": 0})
        c.send({"type": "commit", "session": 1, "seq": 2, "index": 0})  # 2回目は無視される
        c.send({"type": "reset", "session": 1})
        # 上書きされて捨てられる convert を作る。
        lines = [{"type": "convert", "session": 2, "seq": s, "kind": "live", "reading": READING[:s],
                  "cursor": s, "left_context": CONTEXT} for s in range(1, 6)]
        c.sock.sendall(b"".join(json.dumps(m, ensure_ascii=False).encode() + b"\n" for m in lines))
        while c.recv()["seq"] != 5:
            pass
        c.send({"type": "convert", "session": 3, "seq": 1, "kind": "full", "reading": "", "cursor": 0})
        assert c.recv()["code"] == "invalid_reading"
        c.convert(1, 3, "おわり")
        c.close()

        r = ctl(env, "debug", "off")
        check(r.returncode == 0, "debug off が成功する")
        mid = server.log_path.read_text()
        c2 = Client(env.socket)
        c2.convert(1, 1, READING, left_context=CONTEXT)
        c2.close()
        after = server.log_path.read_text()
        check("debug: off" in ctl(env, "status").stdout, "status に debug: off と出る")
    finally:
        server.stop()

log = debug_lines(mid)
print("\n".join(log))
check(any("受信 convert" in l and "len=7" in l and "ctx=9" in l for l in log), "convert の長さが書かれる")
check(any("送信 result" in l and "seq=2" in l for l in log), "result が書かれる")
check(any("学習した" in l for l in log) and any("無視した" in l for l in log), "commit の結果が書かれる")
check(any("を捨てた" in l and "上書き" in l for l in log), "捨てた convert が書かれる")
check(any("error code=invalid_reading" in l for l in log), "error が書かれる")
check(any("が閉じた" in l for l in log), "切断が書かれる")
check(len(debug_lines(after)) == len(log), "debug off の後は書かない")
# 1文字の読み（「き」）は「上書き」などの地の文と見分けられないので、2文字以上で確かめる。
secrets = [READING, CONTEXT, "おわり", *candidates] + [READING[:s] for s in range(1, 6)]
leaked = [s for s in secrets if len(s) >= 2 and s in "\n".join(debug_lines(after))]
check(not leaked, f"読み、文脈、候補の文字列がログにない {leaked}")
check(ctl(env, "debug", "maybe").returncode != 0, "debug の引数が不正なら失敗する")

print("OK" if not failures else f"NG {len(failures)} 件")
sys.exit(0 if not failures else 1)
