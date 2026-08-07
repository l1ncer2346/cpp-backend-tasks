#include "request_handler.h"

using namespace std::literals;

namespace http_handler {

namespace {

struct ContentType {
    ContentType() = delete;
    constexpr static std::string_view APPLICATION_JSON = "application/json"sv;
};

constexpr std::string_view kMapsApiTarget = "/api/v1/maps"sv;

}  // namespace

RequestHandler::StringResponse RequestHandler::MakeJsonResponse(http::status status, const json::value& value,
                                                                  unsigned version, bool keep_alive) {
    StringResponse response{status, version};
    response.set(http::field::content_type, ContentType::APPLICATION_JSON);
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

json::value RequestHandler::MapToJson(const model::Map& map) {
    json::array roads;
    for (const auto& road : map.GetRoads()) {
        json::object road_obj;
        road_obj["x0"] = road.GetStart().x;
        road_obj["y0"] = road.GetStart().y;
        if (road.IsHorizontal()) {
            road_obj["x1"] = road.GetEnd().x;
        } else {
            road_obj["y1"] = road.GetEnd().y;
        }
        roads.push_back(std::move(road_obj));
    }

    json::array buildings;
    for (const auto& building : map.GetBuildings()) {
        const auto& bounds = building.GetBounds();
        json::object building_obj;
        building_obj["x"] = bounds.position.x;
        building_obj["y"] = bounds.position.y;
        building_obj["w"] = bounds.size.width;
        building_obj["h"] = bounds.size.height;
        buildings.push_back(std::move(building_obj));
    }

    json::array offices;
    for (const auto& office : map.GetOffices()) {
        json::object office_obj;
        office_obj["id"] = *office.GetId();
        office_obj["x"] = office.GetPosition().x;
        office_obj["y"] = office.GetPosition().y;
        office_obj["offsetX"] = office.GetOffset().dx;
        office_obj["offsetY"] = office.GetOffset().dy;
        offices.push_back(std::move(office_obj));
    }

    json::object map_obj;
    map_obj["id"] = *map.GetId();
    map_obj["name"] = map.GetName();
    map_obj["roads"] = std::move(roads);
    map_obj["buildings"] = std::move(buildings);
    map_obj["offices"] = std::move(offices);
    return map_obj;
}

RequestHandler::StringResponse RequestHandler::MakeMapsListResponse(unsigned version, bool keep_alive) const {
    json::array maps_array;
    for (const auto& map : game_.GetMaps()) {
        json::object map_info;
        map_info["id"] = *map.GetId();
        map_info["name"] = map.GetName();
        maps_array.push_back(std::move(map_info));
    }
    return MakeJsonResponse(http::status::ok, maps_array, version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::MakeMapResponse(const std::string& map_id, unsigned version,
                                                                 bool keep_alive) const {
    const model::Map* map = game_.FindMap(model::Map::Id{map_id});
    if (!map) {
        return MakeErrorResponse(http::status::not_found, "mapNotFound"sv, "Map not found"sv, version, keep_alive);
    }
    return MakeJsonResponse(http::status::ok, MapToJson(*map), version, keep_alive);
}

RequestHandler::StringResponse RequestHandler::HandleApiRequest(const std::string& target, unsigned version,
                                                                  bool keep_alive) const {
    if (target == std::string(kMapsApiTarget)) {
        return MakeMapsListResponse(version, keep_alive);
    }

    const std::string map_id_prefix = std::string(kMapsApiTarget) + "/";
    if (target.starts_with(map_id_prefix)) {
        return MakeMapResponse(target.substr(map_id_prefix.size()), version, keep_alive);
    }

    return MakeErrorResponse(http::status::bad_request, "badRequest"sv, "Bad request"sv, version, keep_alive);
}

}  // namespace http_handler
