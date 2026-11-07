import Foundation
import llama

/// llama.cpp のログのうち、警告とエラーだけを標準エラー出力に通す（docs/server.md「ログ」）。
/// 既定では、モデルを読み込むたびに情報のログを 150 行ほど書き、journald が埋まる。
/// 変換キューの上で読み込むので、コールバックが同時に呼ばれることはない。
nonisolated(unsafe) private var lastLlamaLevelPassed = false

func quietLlamaLog() {
    llama_log_set({ level, text, _ in
        let pass: Bool
        switch level {
        case GGML_LOG_LEVEL_WARN, GGML_LOG_LEVEL_ERROR: pass = true
        case GGML_LOG_LEVEL_CONT: pass = lastLlamaLevelPassed  // 前の行の続き
        default: pass = false
        }
        lastLlamaLevelPassed = pass
        guard pass, let text else { return }
        _ = write(2, text, strlen(text))
    }, nil)
}
