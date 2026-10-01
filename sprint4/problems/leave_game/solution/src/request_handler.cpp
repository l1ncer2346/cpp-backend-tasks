#include "request_handler.h"

#include "api_endpoints.h"

#ifdef SERVER_LOGGING
#include "server_logging.h"
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <charconv>
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
constexpr std::string_view kAllowGetHead = "GET, HEAD";
constexpr std::string_view kAllowPost = "POST";

struct Field {
    static constexpr json::string_view kX0 = "x0";
    static constexpr json::string_view kY0 = "y0";
    static constexpr json::string_view kX1 = "x1";
    static constexpr json::string_view kY1 = "y1";
    static constexpr json::string_view kX = "x";
    static constexpr json::string_view kY = "y";
    static constexpr json::string_view kWidth = "w";
    static constexpr json::string_view kHeight = "h";
    static constexpr json::string_view kId = "id";
    static constexpr json::string_view kOffsetX = "offsetX";
    static constexpr json::string_view kOffsetY = "offsetY";
    static constexpr json::string_view kName = "name";
    static constexpr json::string_view kRoads = "roads";
    static constexpr json::string_view kBuildings = "buildings";
    static constexpr json::string_view kOffices = "offices";
    static constexpr json::string_view kLootTypes = "lootTypes";
    static constexpr json::string_view kPos = "pos";
    static constexpr json::string_view kSpeed = "speed";
    static constexpr json::string_view kDir = "dir";
    static constexpr json::string_view kBag = "bag";
    static constexpr json::string_view kType = "type";
    static constexpr json::string_view kScore = "score";
    static constexpr json::string_view kPlayers = "players";
    static constexpr json::string_view kLostObjects = "lostObjects";
    static constexpr json::string_view kUserName = "userName";
    static constexpr json::string_view kMapId = "mapId";
    static constexpr json::string_view kAuthToken = "authToken";
    static constexpr json::string_view kPlayerId = "playerId";
    static constexpr json::string_view kMove = "move";
    static constexpr json::string_view kTimeDelta = "timeDelta";
    static constexpr json::string_view kCode = "code";
    static constexpr json::string_view kMessage = "message";
    static constexpr json::string_view kPlayTime = "playTime";
};

struct ErrorCode {
    static constexpr std::string_view kInvalidMethod = "invalidMethod";
    static constexpr std::string_view kMapNotFound = "mapNotFound";
    static constexpr std::string_view kInvalidArgument = "invalidArgument";
    static constexpr std::string_view kInvalidToken = "invalidToken";
    static constexpr std::string_view kUnknownToken = "unknownToken";
    static constexpr std::string_view kBadRequest = "badRequest";
    static constexpr std::string_view kServerError = "serverError";
};

constexpr size_t kMaxRecordsItems = 100;
constexpr std::string_view kStartParam = "start";
constexpr std::string_view kMaxItemsParam = "maxItems";

size_t ParseSize(std::string_view value) {
    size_t result = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        throw std::invalid_argument("Invalid number");
    }
    return result;
}

// значение параметра из строки запроса вида a=1&b=2
std::optional<std::string_view> FindQueryParam(std::string_view query, std::string_view name) {
    while (!query.empty()) {
        const size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        const size_t eq = pair.find('=');
        if (pair.substr(0, eq) == name) {
            return eq == std::string_view::npos ? std::string_view{} : pair.substr(eq + 1);
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query.remove_prefix(amp + 1);
    }
    return std::nullopt;
}

// пустая команда - остановка
bool IsValidMove(std::string_view move) {
    if (move.empty()) {
        return true;
    }
    constexpr std::string_view kMoves = "LRUD";
    return move.size() == 1 && kMoves.find(move.front()) != std::string_view::npos;
}

json::object RoadToJson(const model::Road& road) {
    const auto start = road.GetStart();
    const auto end = road.GetEnd();
    json::object object;
    object[Field::kX0] = start.x;
    object[Field::kY0] = start.y;
    if (road.IsHorizontal()) {
        object[Field::kX1] = end.x;
    } else {
        object[Field::kY1] = end.y;
    }
    return object;
}

json::object BuildingToJson(const model::Building& building) {
    const auto& bounds = building.GetBounds();
    json::object object;
    object[Field::kX] = bounds.position.x;
    object[Field::kY] = bounds.position.y;
    object[Field::kWidth] = bounds.size.width;
    object[Field::kHeight] = bounds.size.height;
    return object;
}

json::object OfficeToJson(const model::Office& office) {
    json::object object;
    object[Field::kId] = *office.GetId();
    object[Field::kX] = office.GetPosition().x;
    object[Field::kY] = office.GetPosition().y;
    object[Field::kOffsetX] = office.GetOffset().dx;
    object[Field::kOffsetY] = office.GetOffset().dy;
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
    error[Field::kCode] = std::string(code);
    error[Field::kMessage] = std::string(message);
    return MakeJsonResponse(status, error, version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::MakeMethodError(std::string_view allow, unsigned version,
                                                                 bool keep_alive) {
    StringResponse response = MakeErrorResponse(http::status::method_not_allowed, ErrorCode::kInvalidMethod, "Invalid method",
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

json::value RequestHandler::MapToJson(const model::Map& map) const {
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
    object[Field::kId] = *map.GetId();
    object[Field::kName] = map.GetName();
    object[Field::kRoads] = std::move(roads);
    object[Field::kBuildings] = std::move(buildings);
    object[Field::kOffices] = std::move(offices);
    if (const auto* loot_types = extra_.FindLootTypes(*map.GetId())) {
        object[Field::kLootTypes] = *loot_types;
    }
    return object;
}

RequestHandler::StringResponse RequestHandler::MakeMapsListResponse(unsigned version, bool keep_alive) const {
    json::array maps;
    for (const auto& map : app_.GetMaps()) {
        json::object item;
        item[Field::kId] = *map.GetId();
        item[Field::kName] = map.GetName();
        maps.push_back(std::move(item));
    }
    return MakeJsonResponse(http::status::ok, maps, version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::MakeMapResponse(std::string_view map_id, unsigned version,
                                                                 bool keep_alive) const {
    const model::Map* map = app_.FindMap(model::Map::Id{std::string(map_id)});
    if (!map) {
        return MakeErrorResponse(http::status::not_found, ErrorCode::kMapNotFound, "Map not found", version, keep_alive);
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
    object[Field::kPos] = json::array{player.position.x, player.position.y};
    object[Field::kSpeed] = json::array{player.speed.x, player.speed.y};
    object[Field::kDir] = DirectionToString(player.direction);
    json::array bag;
    for (const auto& item : player.bag) {
        bag.push_back(json::object{{Field::kId, item.id}, {Field::kType, item.type}});
    }
    object[Field::kBag] = std::move(bag);
    object[Field::kScore] = player.score;
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
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto& object = value.as_object();
        const auto user_it = object.find(Field::kUserName);
        const auto map_it = object.find(Field::kMapId);
        if (user_it == object.end() || map_it == object.end() || !user_it->value().is_string() ||
            !map_it->value().is_string() || user_it->value().as_string().empty()) {
            throw std::invalid_argument("missing field");
        }
        const std::string user_name = std::string(user_it->value().as_string());
        const std::string map_id = std::string(map_it->value().as_string());
        const auto joined = app_.Join(user_name, model::Map::Id{map_id}, randomize_spawn_points_);
        if (!joined.map_found) {
            return MakeErrorResponse(http::status::not_found, ErrorCode::kMapNotFound, "Map not found", request.version(),
                                     request.keep_alive());
        }
        json::object response;
        response[Field::kAuthToken] = joined.token;
        response[Field::kPlayerId] = joined.player_id;
        return MakeJsonResponse(http::status::ok, response, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid join request",
                                 request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::MakePlayersResponse(
    const http::request<http::string_body>& request) const {
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kInvalidToken, "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    const auto state = app_.GetState(*token);
    if (!state) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kUnknownToken, "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    json::object players;
    for (const auto& player : state->players) {
        json::object item;
        item[Field::kName] = player.name;
        players[std::to_string(player.id)] = std::move(item);
    }
    return MakeJsonResponse(http::status::ok, players, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::MakeStateResponse(const http::request<http::string_body>& request) const {
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kInvalidToken, "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    const auto state = app_.GetState(*token);
    if (!state) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kUnknownToken, "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    json::object players;
    for (const auto& player : state->players) {
        players[std::to_string(player.id)] = PlayerStateToJson(player);
    }
    json::object lost_objects;
    for (const auto& object : state->lost_objects) {
        lost_objects[std::to_string(object.item.id)] =
            json::object{{Field::kType, object.item.type}, {Field::kPos, json::array{object.position.x, object.position.y}}};
    }
    json::object response;
    response[Field::kPlayers] = std::move(players);
    response[Field::kLostObjects] = std::move(lost_objects);
    return MakeJsonResponse(http::status::ok, response, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::MakeActionResponse(const http::request<http::string_body>& request) const {
    if (!IsJsonContentType(request)) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    const auto token = GetToken(request);
    if (!token) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kInvalidToken, "Authorization header is required",
                                 request.version(), request.keep_alive());
    }
    if (!app_.GetState(*token)) {
        return MakeErrorResponse(http::status::unauthorized, ErrorCode::kUnknownToken, "Player token has not been found",
                                 request.version(), request.keep_alive());
    }
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto it = value.as_object().find(Field::kMove);
        if (it == value.as_object().end() || !it->value().is_string()) {
            throw std::invalid_argument("missing move");
        }
        const std::string move = std::string(it->value().as_string());
        if (!IsValidMove(move)) {
            throw std::invalid_argument("invalid move");
        }
        if (!app_.Move(*token, move.empty() ? '\0' : move[0])) {
            return MakeErrorResponse(http::status::unauthorized, ErrorCode::kUnknownToken, "Player token has not been found",
                                     request.version(), request.keep_alive());
        }
        return MakeJsonResponse(http::status::ok, json::object{}, request.version(), request.keep_alive());
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Failed to parse action",
                                 request.version(), request.keep_alive());
    }
}

RequestHandler::StringResponse RequestHandler::MakeTickResponse(const http::request<http::string_body>& request) const {
    if (automatic_ticks_) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kBadRequest, "Invalid endpoint", request.version(),
                                 request.keep_alive());
    }
    if (!IsJsonContentType(request)) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid content type",
                                 request.version(), request.keep_alive());
    }
    std::int64_t delta = 0;
    try {
        const auto value = json::parse(request.body());
        if (!value.is_object()) {
            throw std::invalid_argument("not an object");
        }
        const auto it = value.as_object().find(Field::kTimeDelta);
        if (it == value.as_object().end() ||
            (!it->value().is_int64() && !it->value().is_uint64()) ||
            (it->value().is_int64() && it->value().as_int64() < 0)) {
            throw std::invalid_argument("invalid delta");
        }
        if (it->value().is_int64()) {
            delta = it->value().as_int64();
        } else if (it->value().as_uint64() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("delta is too large");
        } else {
            delta = static_cast<std::int64_t>(it->value().as_uint64());
        }
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid time delta",
                                 request.version(), request.keep_alive());
    }
    app_.Tick(delta);
    return MakeJsonResponse(http::status::ok, json::object{}, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::MakeRecordsResponse(const http::request<http::string_body>& request,
                                                                     std::string_view query) const {
    size_t start = 0;
    size_t max_items = kMaxRecordsItems;
    try {
        if (const auto value = FindQueryParam(query, kStartParam)) {
            start = ParseSize(*value);
        }
        if (const auto value = FindQueryParam(query, kMaxItemsParam)) {
            max_items = ParseSize(*value);
        }
    } catch (const std::exception&) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "Invalid query parameters",
                                 request.version(), request.keep_alive());
    }
    if (max_items > kMaxRecordsItems) {
        return MakeErrorResponse(http::status::bad_request, ErrorCode::kInvalidArgument, "maxItems is too large",
                                 request.version(), request.keep_alive());
    }

    json::array records;
    for (const auto& record : app_.GetRecords(start, max_items)) {
        records.push_back(json::object{{Field::kName, record.name},
                                       {Field::kScore, record.score},
                                       {Field::kPlayTime, static_cast<double>(record.play_time_ms) / 1000.0}});
    }
    return MakeJsonResponse(http::status::ok, records, request.version(), request.keep_alive());
}

RequestHandler::StringResponse RequestHandler::HandleApiRequest(
    const http::request<http::string_body>& request, const std::string& full_target) const {
    const unsigned version = request.version();
    const bool keep_alive = request.keep_alive();
    const size_t query_pos = full_target.find('?');
    const std::string target = full_target.substr(0, query_pos);
    const std::string_view query =
        query_pos == std::string::npos ? std::string_view{} : std::string_view{full_target}.substr(query_pos + 1);
    if (target == Endpoint::kRecords) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError(kAllowGetHead, version, keep_alive);
        }
        return MakeRecordsResponse(request, query);
    }
    if (target == Endpoint::kMaps) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError(kAllowGetHead, version, keep_alive);
        }
        return MakeMapsListResponse(version, keep_alive);
    }
    if (target.starts_with(Endpoint::kMapPrefix)) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError(kAllowGetHead, version, keep_alive);
        }
        return MakeMapResponse(target.substr(Endpoint::kMapPrefix.size()), version, keep_alive);
    }
    if (target == Endpoint::kJoin) {
        if (request.method() != http::verb::post) {
            return MakeMethodError(kAllowPost, version, keep_alive);
        }
        return MakeJoinResponse(request);
    }
    if (target == Endpoint::kPlayers) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError(kAllowGetHead, version, keep_alive);
        }
        return MakePlayersResponse(request);
    }
    if (target == Endpoint::kState) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodError(kAllowGetHead, version, keep_alive);
        }
        return MakeStateResponse(request);
    }
    if (target == Endpoint::kAction) {
        if (request.method() != http::verb::post) {
            return MakeMethodError(kAllowPost, version, keep_alive);
        }
        return MakeActionResponse(request);
    }
    if (target == Endpoint::kTick) {
        if (automatic_ticks_) {
            return MakeTickResponse(request);
        }
        if (request.method() != http::verb::post) {
            return MakeMethodError(kAllowPost, version, keep_alive);
        }
        return MakeTickResponse(request);
    }
    return MakeErrorResponse(http::status::bad_request, ErrorCode::kBadRequest, "Bad request", version, keep_alive);
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
    if (target.starts_with(Endpoint::kApiPrefix)) {
        try {
            return HandleApiRequest(request, target);
        } catch (const std::exception&) {
            // например, недоступна база данных
            return MakeErrorResponse(http::status::internal_server_error, ErrorCode::kServerError,
                                     "Internal server error", request.version(), request.keep_alive());
        }
    }
    if (request.method() == http::verb::get || request.method() == http::verb::head) {
        return HandleStaticRequest(request, target);
    }
    return MakeErrorResponse(http::status::bad_request, ErrorCode::kBadRequest, "Bad request", request.version(),
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
