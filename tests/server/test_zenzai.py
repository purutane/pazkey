"""Zenzai のモデルの読み込み（docs/server.md「Zenzai のモデルの読み込み」）。

モデルが読めないときは Zenzai なしで動き、反映で失敗したら Zenzai の部分だけ前の状態のまま動く。
モデルは tools/fetch-test-model.sh で取得した vendor/zenz-v3.2-small.gguf を使う。
"""

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


def write_config(env, zenzai, max_chars=64):
    env.config_dir.mkdir(parents=True, exist_ok=True)
    env.config_file.write_text(json.dumps({"version": 1, "context": {"max_chars": max_chars}, "zenzai": zenzai}))


def reload(client):
    """reload_result を返す。直前に届いた config_changed（反映したときだけ届く）を "changed" に入れる。"""
    client.send({"type": "reload"})
    changed = None
    while True:
        reply = client.recv()
        if reply["type"] == "config_changed":
            changed = reply
        if reply["type"] == "reload_result":
            reply["changed"] = changed
            return reply


def status(client):
    reply = client.request({"type": "status"})
    while reply["type"] != "status_result":
        reply = client.recv()
    return reply


def converts(client, session):
    r = client.convert(session, 1, "きかいをつくる")
    return len(r["candidates"]) > 0


if not MODEL.exists():
    print(f"モデルがない（{MODEL}）。先に tools/fetch-test-model.sh を実行すること")
    sys.exit(1)

# 1. 既定の場所にモデルがない → unavailable で起動し、変換はできる。
with TempEnv() as env:
    write_config(env, {"model_path": str(env.dir / "missing.gguf")})
    server = ServerProcess(env, check_zenzai=False)
    c = Client(env.socket)
    check("モデルがなければ hello は unavailable", c.hello["zenzai"] == "unavailable", c.hello)
    s = status(c)
    check("モデルがなければ device は none", s["device"] == "none", s)
    check("モデルがなくても変換できる", converts(c, 1))
    c.close()
    server.stop()

# 2. 壊れたモデル → unavailable で起動し、落ちない。
with TempEnv() as env:
    broken = env.dir / "broken.gguf"
    broken.write_bytes(b"not a gguf file\n" * 100)
    write_config(env, {"model_path": str(broken)})
    server = ServerProcess(env, check_zenzai=False)
    c = Client(env.socket)
    check("壊れたモデルなら unavailable", c.hello["zenzai"] == "unavailable", c.hello)
    check("壊れたモデルでも変換できる", converts(c, 1))
    c.close()
    check("壊れたモデルでも落ちない", server.proc.poll() is None)
    server.stop()

# 3. 正しいモデル → enabled。反映の失敗、無効化、再有効化。
with TempEnv() as env:
    write_config(env, {"model_path": str(MODEL)}, max_chars=256)
    server = ServerProcess(env, check_zenzai=False)
    c = Client(env.socket)
    check("正しいモデルなら enabled", c.hello["zenzai"] == "enabled", c.hello)
    check("正しいモデルなら device は CPU", status(c)["device"] == "CPU")
    check("Zenzai ありで変換できる", converts(c, 1))
    r = c.convert(2, 1, "きかいをつくる", left_context="工場で新しい")
    check("左文脈つきで変換できる", len(r["candidates"]) > 0)

    # プロンプト（左文脈 + 読み + 候補）が 512 トークンを超えても落ちない（azooKey のパッチ。docs/server.md「依存」）。
    # パッチの前は、左文脈 64 文字なら読み 238 文字以上、左文脈 256 文字なら読み 126 文字以上で落ちていた。
    unit = "きょうはいいてんきなのでさんぽにいきましょう"
    ctx = "今日の会議では新しい製品の計画について話し合いました。" * 20
    r = c.convert(5, 1, (unit * 20)[:256], left_context=ctx[:64])
    check("読み 256 文字と左文脈 64 文字で落ちない", len(r["candidates"]) > 0)
    r = c.convert(6, 1, (unit * 20)[:256], left_context=ctx[:256])
    check("読み 256 文字と左文脈 256 文字で落ちない", len(r["candidates"]) > 0)

    write_config(env, {"model_path": str(env.dir / "missing.gguf"), "inference_limit": 5})
    r = reload(c)
    check("反映で読めなければ ok は true で errors に /zenzai",
          r["ok"] is True and [e["path"] for e in r["errors"]] == ["/zenzai"], r)
    check("反映で読めなければ前のモデルのまま enabled", status(c)["zenzai"] == "enabled")
    check("config_changed の zenzai も enabled", (r["changed"] or {}).get("zenzai") == "enabled", r["changed"])
    p = subprocess.run([str(CTL), "reload"], env=env.env, capture_output=True, text=True)
    check("pazkey-ctl reload は終了コード 1 で /zenzai のエラーを表示する",
          p.returncode == 1 and "/zenzai" in p.stdout, repr(p.stdout))
    check("反映で読めなくても変換できる", converts(c, 3))

    write_config(env, {"enabled": False, "model_path": str(MODEL)})
    r = reload(c)
    check("無効にしたら disabled", r["ok"] and r["errors"] == [] and status(c)["zenzai"] == "disabled", r)
    check("無効にしたら device は none", status(c)["device"] == "none")
    check("config_changed の zenzai も disabled", (r["changed"] or {}).get("zenzai") == "disabled", r["changed"])

    write_config(env, {"enabled": True, "model_path": str(env.dir / "missing.gguf")})
    r = reload(c)
    check("Zenzai なしから読めなければ unavailable", status(c)["zenzai"] == "unavailable", r)
    check("config_changed の zenzai も unavailable", (r["changed"] or {}).get("zenzai") == "unavailable", r["changed"])

    write_config(env, {"model_path": str(MODEL)})
    r = reload(c)
    check("置き直して reload すれば enabled", r["errors"] == [] and status(c)["zenzai"] == "enabled", r)
    check("置き直したあとも変換できる", converts(c, 4))
    c.close()
    check("正常に終わる", server.stop() == 0)

print("OK" if failed == 0 else f"{failed} 件が失敗した")
sys.exit(1 if failed else 0)
