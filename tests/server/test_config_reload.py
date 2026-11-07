#!/usr/bin/env python3
"""不正な設定で止まらない（docs/server.md「設定の反映」）。

不正な config.json を保存すると、pazkey-ctl reload がエラーを返し、変換は前の設定のまま続く。
正しい内容に直して保存すると、コマンドなしで反映される（status の config_generation が増える）。

反映されたかは、候補の数（candidates.page_size × max_pages）で見分ける。
"""

import json
import os
import subprocess
import sys
import time

from pazkey import CTL, Client, ServerProcess, TempEnv


READING = "きかい"


def ctl(env, *args):
    return subprocess.run([str(CTL), *args], env=env.env, capture_output=True, text=True, timeout=10)


def generation(env):
    out = ctl(env, "status").stdout
    return int(next(l.split(": ")[1] for l in out.splitlines() if l.startswith("config_generation")))


def wait_generation(env, above, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        g = generation(env)
        if g > above:
            return g
        time.sleep(0.05)
    return generation(env)


def count(client, seq):
    return len(client.convert(1, seq, READING)["candidates"])


def write_direct(path, text):
    path.write_text(text)


def write_rename(path, text):
    tmp = path.with_name(".config.json.tmp")
    tmp.write_text(text)
    os.rename(tmp, path)


checks = []


def check(label, cond, detail=""):
    checks.append(cond)
    print(f"{'OK' if cond else 'NG'} {label} {detail}")


with TempEnv() as env:
    server = ServerProcess(env)
    try:
        c = Client(env.socket)
        watcher = Client(env.socket)  # config_changed が届くかを見る接続
        g0 = c.hello["config_generation"]
        check("既定の候補数は4件以上", count(c, 1) > 3)

        # 1. 正しい設定（3件）を直接書き込む → コマンドなしで反映される
        write_direct(env.config_file, env.config_json({"candidates": {"page_size": 3, "max_pages": 1}}))
        g1 = wait_generation(env, g0)
        check("正しい設定はコマンドなしで反映される", g1 == g0 + 1, f"generation {g0} → {g1}")
        check("候補が3件になる", count(c, 2) == 3)
        msg = watcher.recv()
        check("config_changed が届く", msg == {"type": "config_changed", "config_generation": g1,
                                               "zenzai": c.hello["zenzai"]}, str(msg))

        # 2. 不正な設定を保存する
        write_direct(env.config_file, '{"candidates": {"page_size": 0, "max_pages": 2}}')
        time.sleep(0.5)  # 監視による読み直し（200ms 後）が終わるのを待つ
        r = ctl(env, "reload")
        check("reload がエラーを返す", r.returncode == 1 and "/candidates/page_size" in r.stdout, repr(r.stdout))
        msg = watcher.recv()
        check("不正なら config_rejected が届く", msg == {"type": "config_rejected", "config_generation": g1}, str(msg))
        check("config_generation は変わらない", generation(env) == g1)
        check("変換は前の設定（3件）のまま続く", count(c, 3) == 3)

        # 3. 正しい内容に直して保存する（編集器と同じく rename で置き換える）→ コマンドなしで反映
        write_rename(env.config_file, env.config_json({"candidates": {"page_size": 5, "max_pages": 1}}))
        g2 = wait_generation(env, g1)
        check("直した設定はコマンドなしで反映される", g2 == g1 + 1, f"generation {g1} → {g2}")
        check("候補が5件になる", count(c, 4) == 5)

        # 4. ファイルを消すと既定値に戻る
        env.config_file.unlink()
        g3 = wait_generation(env, g2)
        check("ファイルを消すと既定値に戻る", g3 == g2 + 1 and count(c, 5) > 5, f"generation {g2} → {g3}")

        r = ctl(env, "reload")
        if env.default_model.exists():
            check("正しい設定での reload は成功する", r.returncode == 0, repr(r.stdout))
        else:
            # 既定値では Zenzai が有効で、モデルは既定の場所になる。そこにモデルがなければ、設定は反映したうえで
            # reload は /zenzai のエラーを返す（docs/server.md「Zenzai のモデルの読み込み」）。
            check("既定値での reload は Zenzai のエラーだけを返す",
                  r.returncode == 1 and "/zenzai" in r.stdout and "不正" not in r.stdout, repr(r.stdout))
        log = server.log_path.read_text()
    finally:
        server.stop()

ok = all(checks)
print("OK" if ok else "NG")
sys.exit(0 if ok else 1)
