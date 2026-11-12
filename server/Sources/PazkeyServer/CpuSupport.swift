import Foundation
import PazkeyCore

#if canImport(Glibc)
import Glibc
#endif

/// llama.cpp が使う命令に、この CPU が対応しているか（docs/server.md「Zenzai のモデルの読み込み」）。
///
/// llama.cpp の CPU 版（`libggml-cpu.so`）は、ビルドのときに決めた命令（`-bin` では AVX2 など）を前提にする。
/// 対応しない CPU でモデルを読み込むと、不正な命令（SIGILL）でサーバーが落ちるので、読み込む前に確かめる。
enum CpuSupport {
    /// ggml の関数（ビルドのときにその命令を使うと決めたなら 1 を返す）と、`/proc/cpuinfo` の flags の名前。
    /// 関数は定数を返すだけで、その命令を実行しない。`ggml_backend_get_features` は内部で `ggml_cpu_init` を呼び、
    /// その命令を実行しうるので使わない。
    private static let features: [(function: String, flag: String, name: String)] = [
        ("ggml_cpu_has_sse3", "pni", "SSE3"),
        ("ggml_cpu_has_ssse3", "ssse3", "SSSE3"),
        ("ggml_cpu_has_avx", "avx", "AVX"),
        ("ggml_cpu_has_avx2", "avx2", "AVX2"),
        ("ggml_cpu_has_fma", "fma", "FMA"),
        ("ggml_cpu_has_f16c", "f16c", "F16C"),
        ("ggml_cpu_has_bmi2", "bmi2", "BMI2"),
        ("ggml_cpu_has_avx_vnni", "avx_vnni", "AVX-VNNI"),
        ("ggml_cpu_has_avx512", "avx512f", "AVX512F"),
        ("ggml_cpu_has_avx512_vbmi", "avx512vbmi", "AVX512-VBMI"),
        ("ggml_cpu_has_avx512_vnni", "avx512_vnni", "AVX512-VNNI"),
        ("ggml_cpu_has_avx512_bf16", "avx512_bf16", "AVX512-BF16"),
        ("ggml_cpu_has_amx_int8", "amx_int8", "AMX-INT8"),
    ]

    /// llama.cpp が使うのに、この CPU にない命令の名前。確かめられなければ空を返し、理由をログに残す。
    /// テストのときだけ、環境変数 `PAZKEY_TEST_CPU_FLAGS`（空白区切り）で CPU の flags を差し替えられる。
    static func missingFeatures() -> [String] {
        #if arch(x86_64)
        guard let flags = cpuFlags() else {
            log("CPU の対応する命令を確かめられないので、確かめずに Zenzai を使う")
            return []
        }
        typealias HasFeature = @convention(c) () -> Int32
        return features.compactMap { feature in
            // RTLD_DEFAULT（nil）で、読み込み済みの libggml-cpu.so から探す。
            guard let symbol = dlsym(nil, feature.function) else { return nil }
            let built = unsafeBitCast(symbol, to: HasFeature.self)() != 0
            return built && !flags.contains(feature.flag) ? feature.name : nil
        }
        #else
        return []
        #endif
    }

    private static func cpuFlags() -> Set<String>? {
        if let override = ProcessInfo.processInfo.environment["PAZKEY_TEST_CPU_FLAGS"] {
            return Set(override.split(separator: " ").map(String.init))
        }
        guard let text = try? String(contentsOfFile: "/proc/cpuinfo", encoding: .utf8) else { return nil }
        for line in text.split(separator: "\n") where line.hasPrefix("flags") {
            guard let colon = line.firstIndex(of: ":") else { continue }
            return Set(line[line.index(after: colon)...].split(separator: " ").map(String.init))
        }
        return nil
    }
}
