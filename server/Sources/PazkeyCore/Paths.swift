import Foundation

/// XDG の場所。docs/design.md「名前」の表に合わせる。
public enum Paths {
    private static func env(_ name: String) -> String? {
        guard let v = ProcessInfo.processInfo.environment[name], !v.isEmpty, v.hasPrefix("/") else { return nil }
        return v
    }

    private static var home: String {
        ProcessInfo.processInfo.environment["HOME"] ?? NSHomeDirectory()
    }

    private static func xdg(_ name: String, _ fallback: String) -> URL {
        URL(fileURLWithPath: env(name) ?? (home + "/" + fallback), isDirectory: true)
            .appendingPathComponent("pazkey", isDirectory: true)
    }

    /// `$XDG_CONFIG_HOME/pazkey/`
    public static var configDirectory: URL { xdg("XDG_CONFIG_HOME", ".config") }
    /// `$XDG_CONFIG_HOME/pazkey/config.json`
    public static var configFile: URL { configDirectory.appendingPathComponent("config.json") }
    /// `$XDG_STATE_HOME/pazkey/`
    public static var stateDirectory: URL { xdg("XDG_STATE_HOME", ".local/state") }
    /// `$XDG_STATE_HOME/pazkey/memory/`
    public static var memoryDirectory: URL { stateDirectory.appendingPathComponent("memory", isDirectory: true) }
    /// `$XDG_STATE_HOME/pazkey/crash.json`（docs/server.md「退避モード」）
    public static var crashRecord: URL { stateDirectory.appendingPathComponent("crash.json") }
    /// `$XDG_CACHE_HOME/pazkey/shared/`
    public static var sharedDirectory: URL {
        xdg("XDG_CACHE_HOME", ".cache").appendingPathComponent("shared", isDirectory: true)
    }
    /// `$XDG_DATA_HOME/pazkey/`
    public static var dataDirectory: URL { xdg("XDG_DATA_HOME", ".local/share") }
    /// `zenzai.model_path` が空のときに使う Zenzai のモデル。`pazkey-ctl fetch-model` が取得する場所
    /// （docs/config.md、docs/server.md「Zenzai のモデルの取得」）。
    public static var defaultModel: URL { dataDirectory.appendingPathComponent("zenz.gguf") }
    /// `$XDG_RUNTIME_DIR/pazkey/server.sock`
    public static var socket: String {
        let runtime = env("XDG_RUNTIME_DIR") ?? "/run/user/\(getuid())"
        return runtime + "/pazkey/server.sock"
    }
}
