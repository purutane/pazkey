// swift-tools-version: 6.1
// pazkey-server と pazkey-ctl。仕様は docs/server.md。

import Foundation
import PackageDescription

// llama.cpp の libllama.so の置き場所（docs/server.md「依存」）。
// azooKey は Linux ではシステムの libllama.so をリンクするので、-L で場所を教え、
// rpath を埋め込んで systemd から起動しても見つかるようにする。
// libllama.so の依存（libggml*.so）は、libllama.so 自身の RUNPATH（$ORIGIN）で見つかる。
// 既定の場所は tools/build-llama.sh がビルドして入れる vendor/llama/lib。
// パッケージのビルドでは、ビルドした場所と入れる場所が違うので、
// 埋め込む場所を PAZKEY_LLAMA_RPATH で別に指す（既定は PAZKEY_LLAMA_DIR と同じ）。
let llamaDir = ProcessInfo.processInfo.environment["PAZKEY_LLAMA_DIR"]
    ?? URL(fileURLWithPath: Context.packageDirectory)
        .deletingLastPathComponent()
        .appendingPathComponent("vendor/llama/lib").path
let llamaRpath = ProcessInfo.processInfo.environment["PAZKEY_LLAMA_RPATH"] ?? llamaDir
let llamaLinkerSettings: [LinkerSetting] = [
    .unsafeFlags([
        "-L", llamaDir,
        "-Xlinker", "-rpath", "-Xlinker", llamaRpath,
    ]),
]

// azooKey は、本家 main の 25a6346 にパッチを当てたブランチ pazkey を使う（docs/server.md「依存」）。
// ブランチは GitHub のフォークに置いてある。
// push する前のパッチを試すときは、PAZKEY_AZOOKEY_URL で手元のリポジトリ（file:// の URL）を指し、
// 下の revision をそのコミットにする。このとき Package.resolved が手元のパスに書き換わるので、
// コミットする前に、フォークへ push して上書きを外し、Package.resolved を作り直す。
let azooKeyURL = ProcessInfo.processInfo.environment["PAZKEY_AZOOKEY_URL"]
    ?? "https://github.com/purutane/AzooKeyKanaKanjiConverter.git"
let azooKeyPackage = URL(string: azooKeyURL)!.deletingPathExtension().lastPathComponent

let package = Package(
    name: "pazkey",
    products: [
        .executable(name: "pazkey-server", targets: ["PazkeyServer"]),
        .executable(name: "pazkey-ctl", targets: ["PazkeyCtl"]),
    ],
    dependencies: [
        // ブランチ pazkey のコミットに固定する。
        .package(
            url: azooKeyURL,
            revision: "b009e3ff99811840ae6fae7775ef4af7672e5de1",
            traits: ["Zenzai"]
        ),
    ],
    targets: [
        // 設定、プロトコル、ソケットの行の読み書き。azooKey に依存しない。
        .target(name: "PazkeyCore"),
        .executableTarget(
            name: "PazkeyServer",
            dependencies: [
                "PazkeyCore",
                .product(
                    name: "KanaKanjiConverterModuleWithDefaultDictionary",
                    package: azooKeyPackage
                ),
            ],
            // azooKey は Zenzai のとき C++ と相互運用する（依存の marisa-trie が C++）。
            // 使う側も同じ設定でないと、そのモジュールを読み込めない。
            swiftSettings: [.interoperabilityMode(.Cxx)],
            linkerSettings: llamaLinkerSettings
        ),
        .executableTarget(name: "PazkeyCtl", dependencies: ["PazkeyCore"]),
    ]
)
