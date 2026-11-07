"""退避モード（docs/server.md「退避モード」）。

正常終了しなかった起動が直近 fallback.window_seconds 秒に fallback.max_crashes 回以上あれば、
Zenzai を切って起動する。reload で解除を試み、そのときも起動の記録は消さない。
モデルは tools/fetch-test-model.sh で取得した vendor/zenz-v3.2-small.gguf を使う。
"""

import json
import sys
import time

from pazkey import ROOT, Client, ServerProcess, TempEnv

MODEL = ROOT / "vendor/zenz-v3.2-small.gguf"
failed = 0


def check(name, ok, detail=""):
    global failed
    print(("OK  " if ok else "NG  ") + name + (f" {detail}" if detail and not ok else ""))
    if not ok:
        failed += 1


def write_config(env, zenzai=None, fallback=None):
    obj = {"version": 1, "zenzai": zenzai or {"model_path": str(MODEL)}}
    if fallback:
        obj["fallback"] = fallback
    env.config_file.write_text(json.dumps(obj))


def crash_file(env):
    return env.dir / "xdg_state_home/pazkey/crash.json"


def starts(env):
    f = crash_file(env)
    return json.loads(f.read_text())["starts"] if f.exists() else []


def write_starts(env, ages):
    """今から ages 秒前の起動を、正常終了しなかった起動として書く。"""
    now = int(time.time() * 1000)
    f = crash_file(env)
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(json.dumps({"starts": [now - a * 1000 for a in ages]}))


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


def start(env):
    server = ServerProcess(env, check_zenzai=False)
    return server, Client(env.socket)


if not MODEL.exists():
    print(f"モデルがない（{MODEL}）。先に tools/fetch-test-model.sh を実行すること")
    sys.exit(1)

# 1. 実際に落とす: SIGKILL を3回（既定の max_crashes）続けると、4回目の起動は退避モードになる。
with TempEnv() as env:
    write_config(env)
    states = []
    for _ in range(3):
        server, c = start(env)
        states.append(c.hello["zenzai"])
        c.close()
        server.stop(sig=9)
    check("落ちるまでの3回は enabled", states == ["enabled"] * 3, states)
    check("落ちた3回の起動が残る", len(starts(env)) == 3, starts(env))
    server, c = start(env)
    check("4回目の起動は fallback", c.hello["zenzai"] == "fallback", c.hello)
    s = status(c)
    check("status も fallback", s["zenzai"] == "fallback", s)
    check("退避モードの device は none", s["device"] == "none", s)
    check("退避モードでも変換できる", len(c.convert(1, 1, "きかいをつくる")["candidates"]) > 0)
    check("退避モードの起動も記録する", len(starts(env)) == 4, starts(env))
    log = (env.dir / "server.log").read_text()
    check("退避モードに入ったことをログに書く", "退避モードで起動する" in log)

    # 2. reload で解除を試みる。起動の記録は消さない。
    r = reload(c)
    check("reload は成功する", r["ok"] and r["errors"] == [], r)
    check("reload で enabled に戻る", status(c)["zenzai"] == "enabled")
    check("config_changed の zenzai も enabled", (r["changed"] or {}).get("zenzai") == "enabled", r["changed"])
    check("reload しても起動の記録は消さない", len(starts(env)) == 4, starts(env))
    c.close()

    # 3. 正常終了では、自分の起動の記録だけを消す。記録が残っているので、次の起動もまた退避モード。
    check("SIGTERM で正常終了する", server.stop() == 0)
    check("正常終了で自分の記録だけを消す", len(starts(env)) == 3, starts(env))
    server, c = start(env)
    check("落ちた記録が時間幅の中にあれば、次も fallback", c.hello["zenzai"] == "fallback", c.hello)
    c.close()
    server.stop()

# 4. 閾値の境目。時間幅の外の記録は数えず、捨てる。
with TempEnv() as env:
    write_config(env)
    write_starts(env, [10, 20])
    server, c = start(env)
    check("max_crashes 未満（2回）なら enabled", c.hello["zenzai"] == "enabled", c.hello)
    c.close()
    server.stop()
    check("正常終了のあとは前の2回が残る", len(starts(env)) == 2, starts(env))

with TempEnv() as env:
    write_config(env)
    write_starts(env, [400, 500, 600, 10])  # 既定の window_seconds は 300
    server, c = start(env)
    check("時間幅の外の記録は数えない", c.hello["zenzai"] == "enabled", c.hello)
    check("時間幅の外の記録は捨てる", len(starts(env)) == 2, starts(env))
    c.close()
    server.stop()

with TempEnv() as env:
    write_config(env)
    write_starts(env, [-3600, -7200, -10800])  # 時計が戻ったときの、未来の記録
    server, c = start(env)
    check("未来の記録は数えない", c.hello["zenzai"] == "enabled", c.hello)
    c.close()
    server.stop()

# 5. 設定の閾値を使う。
with TempEnv() as env:
    write_config(env, fallback={"max_crashes": 1, "window_seconds": 10})
    write_starts(env, [5])
    server, c = start(env)
    check("max_crashes=1 なら1回で fallback", c.hello["zenzai"] == "fallback", c.hello)
    c.close()
    server.stop()
    write_starts(env, [20])
    server, c = start(env)
    check("window_seconds=10 なら20秒前の記録は数えない", c.hello["zenzai"] == "enabled", c.hello)
    c.close()
    server.stop()

# 6. Zenzai を設定で切っていれば、落ちた記録があっても disabled。
with TempEnv() as env:
    write_config(env, zenzai={"enabled": False})
    write_starts(env, [10, 20, 30])
    server, c = start(env)
    check("Zenzai が無効なら disabled", c.hello["zenzai"] == "disabled", c.hello)
    c.close()
    server.stop()

# 7. 記録のファイルが壊れていても起動し、記録がないものとして扱う。
with TempEnv() as env:
    write_config(env)
    crash_file(env).parent.mkdir(parents=True, exist_ok=True)
    crash_file(env).write_text("{broken")
    server, c = start(env)
    check("壊れた記録なら enabled", c.hello["zenzai"] == "enabled", c.hello)
    check("壊れた記録は書き直す", len(starts(env)) == 1, crash_file(env).read_text())
    c.close()
    server.stop()
    check("正常終了で記録のファイルを消す", not crash_file(env).exists())

print()
print("すべて通った" if failed == 0 else f"{failed} 件が失敗した")
sys.exit(1 if failed else 0)
