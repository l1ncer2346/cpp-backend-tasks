#pragma once

#include <string_view>

namespace http_handler {

struct Endpoint {
    static constexpr std::string_view kApiPrefix = "/api/";
    static constexpr std::string_view kMaps = "/api/v1/maps";
    static constexpr std::string_view kMapPrefix = "/api/v1/maps/";
    static constexpr std::string_view kJoin = "/api/v1/game/join";
    static constexpr std::string_view kPlayers = "/api/v1/game/players";
    static constexpr std::string_view kState = "/api/v1/game/state";
    static constexpr std::string_view kAction = "/api/v1/game/player/action";
    static constexpr std::string_view kTick = "/api/v1/game/tick";
};

}  // namespace http_handler
