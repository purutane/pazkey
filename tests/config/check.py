#!/usr/bin/env python3
"""設定の検証のテスト（docs/config.md「読み込みと検証」）。

tests/config/valid/ と invalid/ のすべての例について、次の4つが一致することを確かめる。

1. 置き場所（valid / invalid）
2. config.schema.json による判定（正）
3. サーバーの判定（pazkey-server --check-config）
4. フロントエンドの判定（frontend/build/pazkey-check-config。ビルドしていなければ飛ばす）

ファイルの読み方は RFC 8259 の JSON を UTF-8 で書いたもの（BOM なし、NaN などの拡張なし）とする。

使い方: python -I tests/config/check.py [pazkey-server のパス] [pazkey-check-config のパス]
"""

import json
import pathlib
import subprocess
import sys

import jsonschema

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / "tests" / "config"
SERVER = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "server/.build/release/pazkey-server"
FRONTEND = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "frontend/build/pazkey-check-config"


def reject_constant(name):
    raise ValueError(f"JSON にない値: {name}")


def schema_verdict(validator, path):
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8")  # 'utf-8-sig' ではないので BOM は U+FEFF として残り、json が拒む
        value = json.loads(text, parse_constant=reject_constant)
        # 対になっていないサロゲート（"\ud800"）は文字にならないので不正とする。
        json.dumps(value, ensure_ascii=False).encode("utf-8")
    except (UnicodeError, ValueError):
        return False
    return validator.is_valid(value)


def run_verdict(command, path, who):
    p = subprocess.run(command + [str(path)], capture_output=True, text=True)
    if p.returncode not in (0, 1):
        raise RuntimeError(f"{path}: {who}が異常終了した（{p.returncode}）\n{p.stderr}")
    return p.returncode == 0, p.stdout.strip()


def main():
    if not SERVER.exists():
        sys.exit(f"{SERVER} がない。先に `swift build -c release` を実行する")
    schema = json.loads((ROOT / "config.schema.json").read_text())
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)
    use_frontend = FRONTEND.exists()
    if not use_frontend:
        print(f"注意: {FRONTEND} がないので、フロントエンドの判定は確かめない")

    failures = 0
    count = 0
    for expected_dir, expected in (("valid", True), ("invalid", False)):
        for path in sorted((HERE / expected_dir).glob("*.json")):
            count += 1
            by_schema = schema_verdict(validator, path)
            by_server, output = run_verdict([str(SERVER), "--check-config"], path, "サーバー")
            by_frontend, front_output = (
                run_verdict([str(FRONTEND)], path, "フロントエンド") if use_frontend else (by_schema, "")
            )
            ok = expected == by_schema == by_server == by_frontend
            if not ok:
                failures += 1
                print(f"NG {expected_dir}/{path.name}: 置き場所={expected} スキーマ={by_schema} "
                      f"サーバー={by_server} フロントエンド={by_frontend}")
                print("   " + output.replace("\n", "\n   "))
                if front_output:
                    print("   " + front_output.replace("\n", "\n   "))
    print(f"{count} 件中 {count - failures} 件が一致")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
