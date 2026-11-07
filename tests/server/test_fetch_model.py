"""pazkey-ctl fetch-model（docs/server.md「Zenzai のモデルの取得」）。

取得元は環境変数で file:// の URL に差し替え、ネットワークには出ない。
サーバーに読み込ませるところは、tools/fetch-test-model.sh で取得した vendor/zenz-v3.2-small.gguf があれば確かめる。
"""

import fcntl
import hashlib
import json
import subprocess
import sys

from pazkey import CTL, ROOT, Client, ServerProcess, TempEnv

MODEL = ROOT / "vendor/zenz-v3.2-small.gguf"
failed = 0


def check(name, ok, detail=""):
    global failed
    print(("OK  " if ok else "NG  ") + name + (f" {detail}" if detail and not ok else ""))
    if not ok:
        failed += 1


def fetch(env, source, sha256):
    e = dict(env.env, PAZKEY_TEST_MODEL_URL=source.as_uri(), PAZKEY_TEST_MODEL_SHA256=sha256)
    return subprocess.run([str(CTL), "fetch-model"], env=e, capture_output=True, text=True, timeout=120)


def leftovers(env):
    return sorted(p.name for p in env.default_model.parent.iterdir() if p.name.endswith(".part"))


# 1. サーバーなしで取得する。ハッシュが合えば置く。つながらなくても失敗にしない。
with TempEnv() as env:
    fake = env.dir / "fake.gguf"
    fake.write_bytes(b"not a real model\n" * 100)
    good = hashlib.sha256(fake.read_bytes()).hexdigest()

    r = fetch(env, fake, good)
    check("取得できれば終了コード 0", r.returncode == 0, r.stdout + r.stderr)
    check("既定の場所に置く", env.default_model.exists() and env.default_model.read_bytes() == fake.read_bytes())
    check("サーバーにつながらなくても失敗にしない", "つながらない" in r.stdout, r.stdout)
    check("一時ファイルを残さない", leftovers(env) == [], leftovers(env))

    r = fetch(env, fake, good)
    check("ハッシュの合うファイルがあれば取得しない", r.returncode == 0 and "すでにある" in r.stdout, r.stdout + r.stderr)

    # 2. ハッシュが合わなければ置かない（すでにあるファイルも上書きしない）
    other = env.dir / "other.gguf"
    other.write_bytes(b"something else\n")
    r = fetch(env, other, "1" * 64)  # 置いてあるファイルとも、取得したファイルとも合わない
    check("ハッシュが合わなければ終了コード 1", r.returncode == 1 and "ハッシュが合わない" in r.stderr, r.stderr)
    check("ハッシュが合わなければ前のファイルのまま", env.default_model.read_bytes() == fake.read_bytes())
    check("ハッシュが合わなくても一時ファイルを残さない", leftovers(env) == [], leftovers(env))

    # 3. 取得に失敗する（取得元がない）
    r = fetch(env, env.dir / "missing.gguf", "0" * 64)
    check("取得に失敗したら終了コード 1", r.returncode == 1 and "取得に失敗した" in r.stderr, r.stderr)
    check("取得に失敗しても一時ファイルを残さない", leftovers(env) == [], leftovers(env))

    # 4. ほかで取得しているあいだは取得しない
    with open(str(env.default_model) + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        r = fetch(env, fake, good)
    check("ほかで取得していれば終了コード 1", r.returncode == 1 and "ほかで取得している" in r.stderr, r.stderr)

    # 5. --notify は通知を送る。値の -1 をオプションと読ませないよう、値の前に -- を置く（項目4で見つけた不具合）。
    bindir = env.dir / "bin"
    bindir.mkdir()
    record = env.dir / "busctl.args"
    fake_busctl = bindir / "busctl"
    fake_busctl.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> {record}\necho END >> {record}\n")
    fake_busctl.chmod(0o755)
    e = dict(env.env, PATH=f"{bindir}:{env.env['PATH']}",
             PAZKEY_TEST_MODEL_URL=other.as_uri(), PAZKEY_TEST_MODEL_SHA256=hashlib.sha256(other.read_bytes()).hexdigest())
    r = subprocess.run([str(CTL), "fetch-model", "--notify"], env=e, capture_output=True, text=True, timeout=120)
    calls = [c.strip("\n").split("\n") for c in record.read_text().split("END\n") if c.strip()] if record.exists() else []
    check("--notify は始めと終わりに通知する", r.returncode == 0 and len(calls) == 2, f"{r.stderr} {calls}")
    check("通知の値の前に -- を置く",
          all("--" in c and c.index("--") < c.index("-1") and c[-1] == "-1" for c in calls), calls)

    # 6. 引数の誤り
    r = subprocess.run([str(CTL), "fetch-model", "--bogus"], env=env.env, capture_output=True, text=True)
    check("引数が不正なら失敗する", r.returncode == 1)

# 7. サーバーに読み込ませる。既定の場所にモデルがない状態で起動し、取得したら Zenzai が使えるようになる。
if MODEL.exists():
    with TempEnv() as env:
        env.config_file.write_text(json.dumps({"version": 1, "zenzai": {"enabled": True, "model_path": ""}}))
        server = ServerProcess(env, check_zenzai=False)
        try:
            c = Client(env.socket)
            check("既定の場所にモデルがなければ unavailable", c.hello["zenzai"] == "unavailable", c.hello)
            sha = hashlib.sha256(MODEL.read_bytes()).hexdigest()
            r = fetch(env, MODEL, sha)
            check("取得してサーバーに読み込ませる", r.returncode == 0 and "読み込ませた" in r.stdout, r.stdout + r.stderr)
            s = c.request({"type": "status"})
            while s["type"] != "status_result":
                s = c.recv()
            check("取得したあとは enabled", s["zenzai"] == "enabled", s)
        finally:
            check("正常に終わる", server.stop() == 0)

    # 8. 設定が別のモデルを指していて読み込めなければ、置いたうえで失敗を知らせる
    with TempEnv() as env:
        env.config_file.write_text(json.dumps({"version": 1, "zenzai": {"model_path": str(env.dir / "missing.gguf")}}))
        server = ServerProcess(env, check_zenzai=False)
        try:
            fake = env.dir / "fake.gguf"
            fake.write_bytes(b"x")
            r = fetch(env, fake, hashlib.sha256(b"x").hexdigest())
            check("置いたがサーバーが読み込めなければ終了コード 1",
                  r.returncode == 1 and env.default_model.exists() and "読み込めなかった" in r.stderr, r.stderr)
        finally:
            server.stop()
else:
    print(f"飛ばす: {MODEL} がない（tools/fetch-test-model.sh で取得する）")

sys.exit(1 if failed else 0)
