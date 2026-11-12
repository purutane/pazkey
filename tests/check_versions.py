"""版の文字列が、書いてあるすべての場所でそろっているかを確かめる。

版を上げるときは、ここに挙げた場所をすべて直す。どこかを忘れると、配ったものが前の版を名乗る。
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLACES = [
    ("PKGBUILD", r"^pkgver=(\S+)$"),
    ("packaging/fcitx5-pazkey-bin/PKGBUILD", r"^pkgver=(\S+)$"),
    ("server/Sources/PazkeyCore/Version.swift", r'static let version = "([^"]+)"'),
    ("frontend/CMakeLists.txt", r"project\(fcitx5-pazkey VERSION (\S+) "),
]

found = {}
for path, pattern in PLACES:
    m = re.findall(pattern, (ROOT / path).read_text(), re.MULTILINE)
    if len(m) != 1:
        print(f"NG  {path} に版がちょうど1か所ない（{pattern}）")
        sys.exit(1)
    found[path] = m[0]

if len(set(found.values())) == 1:
    print(f"OK  版はすべて {next(iter(found.values()))}")
    sys.exit(0)
for path, version in found.items():
    print(f"NG  {path}: {version}")
sys.exit(1)
