#include "request_handler.h"

#ifdef SERVER_LOGGING
#include "server_logging.h"
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace http_handler {

namespace {

constexpr std::string_view kJson = "application/json";
constexpr std::string_view kText = "text/plain";
constexpr std::string_view kApiPrefix = "/api/";

json::object RoadToJson(const model::Road& road) {
    const auto start = road.GetStart();
    const auto end = road.GetEnd();
    json::object object;
    object["x0"] = start.x;
    object["y0"] = start.y;
    if (road.IsHorizontal()) {
        object["x1"] = end.x;
    } else {
        object["y1"] = end.y;
    }
    return object;
}

json::object BuildingToJson(const model::Building& building) {
    const auto& bounds = building.GetBounds();
    json::object object;
    object["x"] = bounds.position.x;
    object["y"] = bounds.position.y;
    object["w"] = bounds.size.width;
    object["h"] = bounds.size.height;
    return object;
}

json::object OfficeToJson(const model::Office& office) {
    json::object object;
    object["id"] = *office.GetId();
    object["x"] = office.GetPosition().x;
    object["y"] = office.GetPosition().y;
    object["offsetX"] = office.GetOffset().dx;
    object["offsetY"] = office.GetOffset().dy;
    return object;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsHex(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

std::string MimeType(const std::filesystem::path& path) {
    const std::string extension = Lower(path.extension().string());
    if (extension == ".htm" || extension == ".html") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".txt") return "text/plain";
    if (extension == ".js") return "text/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".xml") return "application/xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpe" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".bmp") return "image/bmp";
    if (extension == ".ico") return "image/vnd.microsoft.icon";
    if (extension == ".tiff" || extension == ".tif") return "image/tiff";
    if (extension == ".svg" || extension == ".svgz") return "image/svg+xml";
    if (extension == ".mp3") return "audio/mpeg";
    return "application/octet-stream";
}

bool StartsWithPath(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto path_it = path.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

}  // namespace

RequestHandler::StringResponse RequestHandler::MakeJsonResponse(http::status status, const json::value& value,
                                                                  unsigned version, bool keep_alive) {
    StringResponse response{status, version};
    response.set(http::field::content_type, std::string(kJson));
    response.set(http::field::cache_control, "no-cache");
    response.body() = json::serialize(value);
    response.content_length(response.body().size());
    response.keep_alive(keep_alive);
    return response;
}

RequestHandler::StringResponse RequestHandler::MakeErrorResponse(http::status status, std::string_view code,
                                                                   std::string_view message, unsigned version,
                                                                   bool keep_alive) {
    json::object error;
    error["code"] = std::string(code);
    error["message"] = std::string(message);
    return MakeJsonResponse(status, error, version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::MakeMethodError(std::string_view allow, unsigned version,
                                                                 bool keep_alive) {
    StringResponse response = MakeErrorResponse(http::status::method_not_allowed, "invalidMethod", "Invalid method",
                                                 version, keep_alive);
    response.set(http::field::allow, std::string(allow));
    return response;
}

RequestHandler::StringResponse RequestHandler::MakePlainResponse(http::status status, std::string_view body,
                                                                   unsigned version, bool keep_alive) {
    StringResponse response{status, version};
    response.set(http::field::content_type, std::string(kText));
    response.body() = body;
    response.content_length(response.body().size());
    response.keep_alive(keep_alive);
    return response;
}

json::value RequestHandler::MapToJson(const model::Map& map) {
    json::array roads;
    for (const auto& road : map.GetRoads()) {
        roads.push_back(RoadToJson(road));
    }
    json::array buildings;
    for (const auto& building : map.GetBuildings()) {
        buildings.push_back(BuildingToJson(building));
    }
    json::array offices;
    for (const auto& office : map.GetOffices()) {
        offices.push_back(OfficeToJson(office));
    }
    json::object object;
    object["id"] = *map.GetId();
    object["name"] = map.GetName();
    object["roads"] = std::move(roads);
    object["buildings"] = std::move(buildings);
    object["offices"] = std::move(offices);
    return object;
}

RequestHandler::StringResponse RequestHandler::MakeMapsListResponse(unsigned version, bool keep_alive) const {
    json::array maps;
    for (const auto& map : game_.GetMaps()) {
        json::object item;
        item["id"] = *map.GetId();
        item["name"] = map.GetName();
        maps.push_back(std::move(item));
    }
    return MakeJsonResponse(http::status::ok, maps, version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::MakeMapResponse(std::string_view map_id, unsigned version,
                                                                 bool keep_alive) const {
    const model::Map* map = game_.FindMap(model::Map::Id{std::string(map_id)});
    if (!map) {
        return MakeErrorResponse(http::status::not_found, "mapNotFound", "Map not found", version, keep_alive);
    }
    return MakeJsonResponse(http::status::ok, MapToJson(*map), version, keep_alive);
}

std::string RequestHandler::DirectionToString(model::Direction direction) {
    switch (direction) {
        case model::Direction::WEST: return "L";
        case model::Direction::EAST: return "R";
        case model::Direction::NORTH: return "U";
        case model::Direction::SOUTH: return "D";
    }
    return "U";
}

json::value RequestHandler::PlayerStateToJson(const model::PlayerState& player) {
    json::object object;
    object["pos"] = json::array{player.position.x, player.position.y};
    object["speed"] = json::array{player.speed.x, player.speed.y};
    object["dir"] = DirectionToString(player.direction);
    return object;
}

bool RequestHandler::IsJsonContentType(const http::request<http::string_body>& request) {
    auto it = request.find(http::field::content_type);
    if (it == request.end()) {
        return false;
    }
    std::string content_type = Lower(std::string(it->value()));
    const size_t semicolon = content_type.find(';');
    if (semicolon != std::string::npos) {
        content_type.resize(semicolon);
    }
    while (!content_type.empty() && std::isspace(static_cast<unsigned char>(content_type.back()))) {
        content_type.pop_back();
    }
    return content_type == kJson;
}

std::optional<std::string> RequestHandler::GetToken(const http::request<http::string_body>& request) {
    auto it = request.find(http::field::authorization);
    if (it == request.end()) {
        return std::nullopt;
    }
    const std::string value = std::string(it->value());
    constexpr std::string_view prefix = "Bearer ";
    if (!value.starts_with(prefix) || value.size() != prefix.size() + 32) {
        return std::nullopt;
    }
    std::string token = value.substr(prefix.size());
    if (!std::all_of(token.begin(), token.end(), IsHex)) {
        return std::nullopt;
    }
    return token;
}

RequestHandler::StringResponse RequestHandler::MakeJoinResponse(const http::request<http::string_body>& request) const {
    if (!IsJsonContentType(request)) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto& object = value.as_object();
        const auto user_it = object.find("userName");
        const auto map_it = object.find("mapId");
        if (user_it == object.end() || map_it == object.end() || !user_it->value().is_string() ||
            !map_it->value().is_string() || user_it->value().as_string().empty()) {
            throw std::invalid_argument("missing field");
        }
        const std::string user_name = std::string(user_it->value().as_string());
        const std::string map_id = std::string(map_it->value().as_string());
        const auto joined = game_.Join(user_name, model::Map::Id{map_id}, randomize_spawn_points_);
        if (!joined.map_found) {
            return MakeErrorResponse(http::status::not_found, "mapNotFound", "Map not found", request.version(),
                                     request.keep_alive());
        }
        json::object response;
        response["authToken"] = joined.token;
        response["playerId"] = joined.player_id;
        return MakeJsonResponse(http::status::ok, response, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Invalid join request",
                                 request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::MakePlayersResponse(
    const http::request<http::string_body>& request) const {
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, "invalidToken", "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    const auto state = game_.GetState(*token);
    if (!state) {
        return MakeErrorResponse(http::status::unauthorized, "unknownToken", "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    json::object players;
    for (const auto& player : *state) {
        json::object item;
        item["name"] = player.name;
        players[std::to_string(player.id)] = std::move(item);
    }
    return MakeJsonResponse(http::status::ok, players, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::MakeStateResponse(const http::request<http::string_body>& request) const {
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, "invalidToken", "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    const auto state = game_.GetState(*token);
    if (!state) {
        return MakeErrorResponse(http::status::unauthorized, "unknownToken", "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    json::object players;
    for (const auto& player : *state) {
        players[std::to_string(player.id)] = PlayerStateToJson(player);
    }
    json::object response;
    response["players"] = std::move(players);
    return MakeJsonResponse(http::status::ok, response, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::MakeActionResponse(const http::request<http::string_body>& request) const {
    if (!IsJsonContentType(request)) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, "invalidToken", "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    if (!game_.GetState(*token)) {
        return MakeErrorResponse(http::status::unauthorized, "unknownToken", "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto it = value.as_object().find("move");
        if (it == value.as_object().end() || !it->value().is_string()) {
            throw std::invalid_argument("missing move");
        }
        const std::string move = std::string(it->value().as_string());
        if (move.size() > 1 || (move.empty() ? false : std::string_view("LRUD").find(move[0]) == std::string_view::npos)) {
            throw std::invalid_argument("invalid move");
        }
        if (!game_.Move(*token, move.empty() ? '\0' : move[0])) {
            return MakeErrorResponse(http::status::unauthorized, "unknownToken", "Player token has not been found",
                                     request.version(), request.keep_alive());
        }
        return MakeJsonResponse(http::status::ok, json::object{}, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Failed to parse action",
                                 request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::MakeTickResponse(const http::request<http::string_body>& request) const {
    if (automatic_ticks_) {
        return MakeErrorResponse(http::status::bad_request, "badRequest", "Invalid endpoint", request.version(),
                                 request.keep_alive());
    }
    if (!IsJsonContentType(request)) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto it = value.as_object().find("timeDelta");
        if (it == value.as_object().end() ||
            (!it->value().is_int64() && !it->value().is_uint64()) ||
            (it->value().is_int64() && it->value().as_int64() < 0)) {
            throw std::invalid_argument("invalid delta");
        }
        std::int64_t delta = 0;
        if (it->value().is_int64()) {
            delta = it->value().as_int64();
        } else if (it->value().as_uint64() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("delta is too large");
        } else {
            delta = static_cast<std::int64_t>(it->value().as_uint64());
        }
        game_.Tick(delta);
        return MakeJsonResponse(http::status::ok, json::object{}, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, "invalidArgument", "Invalid time delta",
                                 request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::HandleApiRequest(
    const http::request<http::string_body>& request, const std::string& target) const {
    const unsigned version = request.version();
    const bool keep_alive = request.keep_alive();
    if (target == "/api/v1/maps") {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError("GET, HEAD", version, keep_alive);
        }
        return MakeMapsListResponse(version, keep_alive);
    }
    constexpr std::string_view maps_prefix = "/api/v1/maps/";
    if (target.starts_with(maps_prefix)) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError("GET, HEAD", version, keep_alive);
        }
        return MakeMapResponse(target.substr(maps_prefix.size()), version, keep_alive);
    }
    if (target == "/api/v1/game/join") {
        if (request.method() != http::verb::post) {
            return MakeMethodError("POST", version, keep_alive);
        }
        return MakeJoinResponse(request);
    }
    if (target == "/api/v1/game/players") {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError("GET, HEAD", version, keep_alive);
        }
        return MakePlayersResponse(request);
    }
    if (target == "/api/v1/game/state") {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError("GET, HEAD", version, keep_alive);
        }
        return MakeStateResponse(request);
    }
    if (target == "/api/v1/game/player/action") {
        if (request.method() != http::verb::post) {
            return MakeMethodError("POST", version, keep_alive);
        }
        return MakeActionResponse(request);
    }
    if (target == "/api/v1/game/tick") {
        if (automatic_ticks_) {
            return MakeTickResponse(request);
        }
        if (request.method() != http::verb::post) {
            return MakeMethodError("POST", version, keep_alive);
        }
        return MakeTickResponse(request);
    }
    return MakeErrorResponse(http::status::bad_request, "badRequest", "Bad request", version, keep_alive);
}

std::string RequestHandler::DecodeUrl(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            decoded.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size() || !IsHex(value[i + 1]) || !IsHex(value[i + 2])) {
            throw std::invalid_argument("Invalid URL encoding");
        }
        decoded.push_back(static_cast<char>(HexValue(value[i + 1]) * 16 + HexValue(value[i + 2])));
        i += 2;
    }
    return decoded;
}

std::filesystem::path RequestHandler::ResolveStaticPath(std::string_view target, bool& outside_root) const {
    outside_root = false;
    const size_t query = target.find('?');
    const std::string decoded = DecodeUrl(target.substr(0, query));
    std::string relative = decoded;
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    std::filesystem::path root = std::filesystem::weakly_canonical(www_root_);
    std::filesystem::path path = std::filesystem::weakly_canonical(root / std::filesystem::path(relative));
    if (!StartsWithPath(path, root)) {
        outside_root = true;
        return {};
    }
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        path /= "index.html";
    }
    return path;
}

RequestHandler::StringResponse RequestHandler::MakeStaticResponse(const std::filesystem::path& path, unsigned version,
                                                                    bool keep_alive) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return MakePlainResponse(http::status::not_found, "File not found", version, keep_alive);
    }
    std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    StringResponse response{http::status::ok, version};
    response.set(http::field::content_type, MimeType(path));
    response.body() = std::move(body);
    response.content_length(response.body().size());
    response.keep_alive(keep_alive);
    return response;
}

RequestHandler::StringResponse RequestHandler::HandleStaticRequest(
    const http::request<http::string_body>& request, const std::string& target) const {
    if (www_root_.empty()) {
        return MakePlainResponse(http::status::not_found, "File not found", request.version(), request.keep_alive());
    }
    try {
        bool outside_root = false;
        const auto path = ResolveStaticPath(target, outside_root);
        if (outside_root) {
            return MakePlainResponse(http::status::bad_request, "Path is outside the static root", request.version(),
                                     request.keep_alive());
        }
        return MakeStaticResponse(path, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakePlainResponse(http::status::bad_request, "Invalid path", request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::HandleRequest(const http::request<http::string_body>& request,
                                                               const std::string& target) const {
    if (target.starts_with(kApiPrefix)) {
        return HandleApiRequest(request, target);
    }
    if (request.method() == http::verb::get || request.method() == http::verb::head) {
        return HandleStaticRequest(request, target);
    }
    return MakeErrorResponse(http::status::bad_request, "badRequest", "Bad request", request.version(),
                             request.keep_alive());
}

#ifdef SERVER_LOGGING
void RequestHandler::LogRequest(std::string_view ip, std::string_view target, std::string_view method) {
    server_logging::LogRequest(ip, target, method);
}

void RequestHandler::LogResponse(std::string_view ip, const StringResponse& response, long long response_time) {
    std::optional<std::string> content_type;
    if (auto it = response.find(http::field::content_type); it != response.end()) {
        content_type = std::string(it->value());
    }
    server_logging::LogResponse(ip, response.result_int(), response_time,
                                content_type ? json::value(*content_type) : json::value(nullptr));
}
#endif

}  // namespace http_handler
