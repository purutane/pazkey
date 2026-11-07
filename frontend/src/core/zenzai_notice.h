// Zenzai の状態を知らせるかどうか（docs/frontend.md「Zenzai の状態を知らせる」）。
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pazkey {

class ZenzaiNotice {
public:
    /// hello か config_changed の zenzai を渡す。知らせるべきなら、その状態（unavailable か fallback）を返す。
    /// 同じ状態が続くあいだ（再接続の hello を含む）は返さない。enabled か disabled を受けたら忘れる。
    /// 知らない値と空（古いサーバー）は無視する。
    std::optional<std::string> update(std::string_view state) {
        if (state == "unavailable" || state == "fallback") {
            if (state == notified_) return std::nullopt;
            notified_ = state;
            return notified_;
        }
        if (state == "enabled" || state == "disabled") notified_.clear();
        return std::nullopt;
    }

private:
    std::string notified_;
};

}  // namespace pazkey
