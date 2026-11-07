#include "protocol.h"

#include "json.h"

namespace pazkey {

std::string helloMessage(std::string_view version) {
    return "{\"type\":\"hello\",\"protocol\":" + std::to_string(kProtocolVersion) +
           ",\"client\":\"fcitx5-pazkey\",\"version\":" + jsonQuote(version) + "}\n";
}

std::string convertMessage(int64_t session, int64_t seq, ConvertKind kind, std::string_view reading, size_t cursor,
                           std::string_view leftContext) {
    return "{\"type\":\"convert\",\"session\":" + std::to_string(session) + ",\"seq\":" + std::to_string(seq) +
           ",\"kind\":\"" + (kind == ConvertKind::Live ? "live" : "full") + "\",\"reading\":" + jsonQuote(reading) +
           ",\"cursor\":" + std::to_string(cursor) + ",\"left_context\":" + jsonQuote(leftContext) + "}\n";
}

std::string commitMessage(int64_t session, int64_t seq, int64_t index) {
    return "{\"type\":\"commit\",\"session\":" + std::to_string(session) + ",\"seq\":" + std::to_string(seq) +
           ",\"index\":" + std::to_string(index) + "}\n";
}

std::string resetMessage(int64_t session) {
    return "{\"type\":\"reset\",\"session\":" + std::to_string(session) + "}\n";
}

std::string closeMessage(int64_t session) {
    return "{\"type\":\"close\",\"session\":" + std::to_string(session) + "}\n";
}

std::optional<Incoming> parseIncoming(std::string_view line) {
    auto root = Json::parse(line);
    if (!root || !root->asObject()) return std::nullopt;
    const Json *type = root->get("type");
    if (!type || !type->asString()) return std::nullopt;
    Incoming in;
    in.type = *type->asString();
    if (const Json *v = root->get("session")) in.session = v->asInt();
    if (const Json *v = root->get("seq")) in.seq = v->asInt();
    if (const Json *v = root->get("protocol")) in.protocol = v->asInt();
    if (const Json *v = root->get("code"); v && v->asString()) in.code = *v->asString();
    if (const Json *v = root->get("message"); v && v->asString()) in.message = *v->asString();
    if (const Json *v = root->get("zenzai"); v && v->asString()) in.zenzai = *v->asString();
    if (in.type == "result") {
        const Json *cands = root->get("candidates");
        if (!cands || !cands->asArray()) return std::nullopt;
        for (const auto &c : *cands->asArray()) {
            const Json *text = c.get("text");
            const Json *consumed = c.get("consumed");
            if (!text || !text->asString() || !consumed || !consumed->asInt() || *consumed->asInt() < 0) {
                return std::nullopt;
            }
            in.candidates.push_back({*text->asString(), static_cast<size_t>(*consumed->asInt())});
        }
    }
    return in;
}

}  // namespace pazkey
