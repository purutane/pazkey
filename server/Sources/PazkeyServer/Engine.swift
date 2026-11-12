import Dispatch
import Foundation
import PazkeyCore
import KanaKanjiConverterModuleWithDefaultDictionary

/// 接続とクライアントのセッション ID の組。セッション ID の範囲は接続ごとに独立している。
struct SessionKey: Hashable, Sendable {
    var connection: Int
    var session: Int
}

struct ConvertRequest: Sendable {
    enum Kind: String, Sendable {
        case live
        case full
    }
    var key: SessionKey
    var seq: Int
    var kind: Kind
    var reading: String
    var cursor: Int
    var leftContext: String
}

struct ConvertResponse: Sendable {
    struct Item: Sendable {
        var text: String
        var consumed: Int
    }
    var candidates: [Item]
    var elapsedMs: Int
}

/// Zenzai の状態。`hello` と `status` の `zenzai`（docs/protocol.md）。
enum ZenzaiState: String, Sendable {
    case enabled
    case disabled
    /// 退避モード（docs/server.md「退避モード」）。設定では有効だが、Zenzai を切って動いている。
    case fallback
    case unavailable
    /// 設定では有効だが、llama.cpp が使う命令に CPU が対応していないので、モデルを読み込まない。
    case unsupported
}

/// azooKey へのアクセスをすべて受け持つ。
///
/// 不変条件4: `KanaKanjiConverter` はスレッドセーフではないので、
/// このクラスのメソッドは `queue`（直列の変換キュー）の上でだけ呼ぶ。
final class Engine: @unchecked Sendable {
    let queue: DispatchQueue

    private let converter: KanaKanjiConverter
    private var config: Config
    private var baseOptions: ConvertRequestOptions
    private var sessions: [SessionKey: SessionState] = [:]
    private let saver: LearningSaver
    /// 読み込めて使っている Zenzai のモデル。nil なら Zenzai なしで変換する。
    private var zenzaiModel: URL?
    private(set) var zenzaiState: ZenzaiState = .disabled
    /// 退避モードか。起動時に決め、設定の反映（reload）で解除を試みる。
    private var fallback: Bool
    /// llama.cpp が使うのに、この CPU にない命令。空でなければ Zenzai を使わない。
    private static let missingCpuFeatures = CpuSupport.missingFeatures()

    private struct SessionState {
        let id: KanaKanjiConverter.ConversionSessionID
        /// 最後に返した result の seq。確定に使ったら nil にする（同じ result で二度学習しない）。
        var lastSeq: Int?
        var lastCandidates: [Candidate] = []
        var composingText = ComposingText()
    }

    /// 変換キュー（`queue`）の上で呼ぶこと。`fallback` なら退避モードで起動し、Zenzai のモデルを読み込まない。
    init(queue: DispatchQueue, config: Config, fallback: Bool) {
        dispatchPrecondition(condition: .onQueue(queue))
        self.queue = queue
        self.fallback = fallback
        for dir in [Paths.memoryDirectory, Paths.sharedDirectory] {
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        self.converter = KanaKanjiConverter.withDefaultDictionary(preloadDictionary: false)
        self.config = config
        self.baseOptions = Self.makeOptions(config)
        self.saver = LearningSaver()
        self.saver.configure(config.learning, engine: self)
        if let error = updateZenzai(config) { log("Zenzai なしで動く: \(error)") }
    }

    /// 推論に使っているデバイスの名前。Zenzai を使っていなければ "none"。
    /// CPU 版の llama.cpp だけを使う。今は GPU に対応しないので、デバイスは指定しない（docs/server.md「依存」）。
    var deviceName: String {
        dispatchPrecondition(condition: .onQueue(queue))
        return zenzaiState == .enabled ? "CPU" : "none"
    }

    var sessionCount: Int {
        dispatchPrecondition(condition: .onQueue(queue))
        return sessions.count
    }

    private static func makeOptions(_ config: Config) -> ConvertRequestOptions {
        ConvertRequestOptions(
            N_best: config.maxCandidates,
            requireJapanesePrediction: .disabled,
            requireEnglishPrediction: .disabled,
            keyboardLanguage: .ja_JP,
            learningType: config.learning.enabled ? .inputAndOutput : .nothing,
            memoryDirectoryURL: Paths.memoryDirectory,
            sharedContainerURL: Paths.sharedDirectory,
            textReplacer: .withDefaultEmojiDictionary(),
            specialCandidateProviders: nil,
            // Zenzai は convert ごとに左文脈を入れて設定する（zenzaiOptions）。
            zenzaiMode: .off,
            metadata: .init(versionString: "pazkey \(Pazkey.version)")
        )
    }

    /// Zenzai を有効にした変換のオプション（docs/server.md「convert の処理」）。
    private static func zenzaiMode(_ config: Config, model: URL, leftContext: String) -> ConvertRequestOptions.ZenzaiMode {
        .on(
            weight: model,
            inferenceLimit: config.zenzai.inferenceLimit,
            personalizationMode: nil,
            versionDependentMode: .v3(.init(
                leftSideContext: leftContext.isEmpty ? nil : leftContext,
                maxLeftSideContextLength: config.context.maxChars
            ))
        )
    }

    /// 設定が求めるモデル。Zenzai が無効なら nil。
    private static func wantedModel(_ config: Config) -> URL? {
        guard config.zenzai.enabled else { return nil }
        return config.zenzai.modelPath.isEmpty ? Paths.defaultModel : URL(fileURLWithPath: config.zenzai.modelPath)
    }

    /// 設定の Zenzai の部分を反映する（docs/server.md「Zenzai のモデルの読み込み」）。
    /// モデルを読み込めなければ、前の状態のまま動き続け、エラーの文言を返す。
    private func updateZenzai(_ newConfig: Config) -> String? {
        guard let wanted = Self.wantedModel(newConfig) else {
            zenzaiModel = nil
            zenzaiState = .disabled
            return nil
        }
        // 読み込むと落ちるので、退避モードより先に確かめる。起動中に CPU は変わらないので、1回だけ調べる。
        if !Self.missingCpuFeatures.isEmpty {
            zenzaiModel = nil
            zenzaiState = .unsupported
            let names = Self.missingCpuFeatures.joined(separator: "、")
            return "この CPU は llama.cpp が使う命令（\(names)）に対応していないので、Zenzai を使えない"
        }
        if fallback {
            zenzaiModel = nil
            zenzaiState = .fallback
            return nil
        }
        if wanted == zenzaiModel { return nil }
        if let error = loadModel(wanted, config: newConfig) {
            if zenzaiModel == nil { zenzaiState = .unavailable }
            return error
        }
        zenzaiModel = wanted
        zenzaiState = .enabled
        log("Zenzai のモデルを読み込んだ: \(wanted.path)")
        return nil
    }

    /// 使い捨てのセッションで1回変換して、モデルを読み込ませる。
    /// azooKey は読み込みに失敗しても変換をエラーにせず、`zenzStatus` にだけ残すので、それで確かめる。
    private func loadModel(_ model: URL, config: Config) -> String? {
        guard FileManager.default.isReadableFile(atPath: model.path) else {
            return "モデルのファイルを読めない: \(model.path)"
        }
        var options = baseOptions
        options.zenzaiMode = Self.zenzaiMode(config, model: model, leftContext: "")
        var ct = ComposingText()
        ct.insertAtCursorPosition("かな", inputStyle: .direct)
        let id = converter.createSession()
        defer { converter.removeSession(id) }
        _ = try? converter.withSession(id) {
            converter.requestCandidates(ct, options: options)
        }
        let loaded = "load \(model.absoluteString)"
        let status = converter.zenzStatus
        guard status != loaded else { return nil }
        let reason = status.hasPrefix(loaded)
            ? status.dropFirst(loaded.count).trimmingCharacters(in: .whitespaces)
            : status
        return "モデルを読み込めない: \(model.path)（\(reason)）"
    }

    /// 新しい設定を使う（軽い項目は次の変換から効く）。
    /// Zenzai のモデルを読み込めなかったときは、そのエラーの文言を返す（ほかの項目は反映する）。
    func apply(config newConfig: Config) -> String? {
        dispatchPrecondition(condition: .onQueue(queue))
        if fallback {
            // 起動の記録は消さない。原因が直っていなければ、次に落ちたときにすぐ退避モードへ戻る。
            fallback = false
            log("設定の反映で、退避モードの解除を試みる")
        }
        config = newConfig
        baseOptions = Self.makeOptions(newConfig)
        saver.configure(newConfig.learning, engine: self)
        return updateZenzai(newConfig)
    }

    private func session(_ key: SessionKey) -> SessionState {
        if let s = sessions[key] { return s }
        let s = SessionState(id: converter.createSession())
        sessions[key] = s
        return s
    }

    func convert(_ request: ConvertRequest) throws -> ConvertResponse {
        dispatchPrecondition(condition: .onQueue(queue))
        let start = DispatchTime.now()
        var state = session(request.key)
        let length = request.reading.unicodeScalars.count

        var ct = ComposingText()
        ct.insertAtCursorPosition(request.reading, inputStyle: .direct)
        _ = ct.moveCursorFromCursorPosition(count: request.cursor - length)

        var options = baseOptions
        if let model = zenzaiModel {
            options.zenzaiMode = Self.zenzaiMode(config, model: model, leftContext: request.leftContext)
        }
        let result = try converter.withSession(state.id) {
            converter.requestCandidates(ct.prefixToCursorPosition(), options: options)
        }
        let limit = request.kind == .live ? 1 : config.maxCandidates
        let candidates = Array(result.mainResults.prefix(limit))
        let items = candidates.map { candidate in
            var rest = ct
            rest.prefixComplete(composingCount: candidate.composingCount)
            return ConvertResponse.Item(text: candidate.text, consumed: length - rest.convertTarget.count)
        }
        state.lastSeq = request.seq
        state.lastCandidates = candidates
        state.composingText = ct
        sessions[request.key] = state
        let elapsed = Int((DispatchTime.now().uptimeNanoseconds - start.uptimeNanoseconds) / 1_000_000)
        return ConvertResponse(candidates: items, elapsedMs: elapsed)
    }

    /// AncoSession.execute(.selectCandidate) と同じ順序で呼ぶ（不変条件3）。
    /// seq が最後の result と違う、または index が範囲外なら何もしない。学習したら true。
    @discardableResult
    func commit(key: SessionKey, seq: Int, index: Int) throws -> Bool {
        dispatchPrecondition(condition: .onQueue(queue))
        guard var state = sessions[key], state.lastSeq == seq, state.lastCandidates.indices.contains(index) else {
            return false
        }
        let candidate = state.lastCandidates[index]
        try converter.withSession(state.id) {
            converter.setCompletedData(candidate)
            converter.updateLearningData(candidate)
            state.composingText.prefixComplete(composingCount: candidate.composingCount)
            if state.composingText.isEmpty {
                state.composingText.stopComposition()
                converter.stopComposition()
            }
        }
        state.lastSeq = nil
        state.lastCandidates = []
        sessions[key] = state
        saver.noteCommit()
        return true
    }

    func reset(key: SessionKey) throws {
        dispatchPrecondition(condition: .onQueue(queue))
        guard var state = sessions[key] else { return }
        try converter.withSession(state.id) {
            converter.stopComposition()
        }
        state.lastSeq = nil
        state.lastCandidates = []
        state.composingText = ComposingText()
        sessions[key] = state
    }

    func close(key: SessionKey) {
        dispatchPrecondition(condition: .onQueue(queue))
        guard let state = sessions.removeValue(forKey: key) else { return }
        converter.removeSession(state.id)
    }

    /// 学習を保存する。保存にかかった時間（ミリ秒）を返す。
    @discardableResult
    func saveLearning() -> Int {
        dispatchPrecondition(condition: .onQueue(queue))
        let start = DispatchTime.now()
        converter.commitUpdateLearningData()
        saver.didSave()
        return Int((DispatchTime.now().uptimeNanoseconds - start.uptimeNanoseconds) / 1_000_000)
    }

    var hasUnsavedLearning: Bool {
        dispatchPrecondition(condition: .onQueue(queue))
        return saver.unsavedCommits > 0
    }
}

/// 学習の保存の時機を決める（docs/server.md「学習の保存」）。変換キューの上でだけ使う。
final class LearningSaver {
    private(set) var unsavedCommits = 0
    private var everyCommits = 20
    private var idleSeconds = 60
    private var timer: (any DispatchSourceTimer)?
    private weak var engine: Engine?

    func configure(_ learning: Config.Learning, engine: Engine) {
        self.everyCommits = learning.saveEveryCommits
        self.idleSeconds = learning.saveIdleSeconds
        self.engine = engine
        if unsavedCommits > 0 { armIdleTimer() }
    }

    func noteCommit() {
        unsavedCommits += 1
        if unsavedCommits >= everyCommits, let engine {
            let ms = engine.saveLearning()
            log("学習を保存した（確定の回数による。\(ms)ms）")
        } else {
            armIdleTimer()
        }
    }

    func didSave() {
        unsavedCommits = 0
        timer?.cancel()
        timer = nil
    }

    private func armIdleTimer() {
        guard let engine else { return }
        timer?.cancel()
        let t = DispatchSource.makeTimerSource(queue: engine.queue)
        t.schedule(deadline: .now() + .seconds(idleSeconds))
        t.setEventHandler { [weak engine] in
            guard let engine, engine.hasUnsavedLearning else { return }
            let ms = engine.saveLearning()
            log("学習を保存した（確定がない時間による。\(ms)ms）")
        }
        t.resume()
        timer = t
    }
}
