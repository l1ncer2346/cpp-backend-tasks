#include "json_loader.h"

#include <boost/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace json_loader {

namespace json = boost::json;
using namespace std::literals;

namespace {

model::Road ParseRoad(const json::object& road_obj) {
    const auto x0 = static_cast<model::Coord>(road_obj.at("x0").as_int64());
    const auto y0 = static_cast<model::Coord>(road_obj.at("y0").as_int64());
    const model::Point start{x0, y0};

    if (auto it = road_obj.find("x1"); it != road_obj.end()) {
        return {model::Road::HORIZONTAL, start, static_cast<model::Coord>(it->value().as_int64())};
    }
    return {model::Road::VERTICAL, start, static_cast<model::Coord>(road_obj.at("y1").as_int64())};
}

model::Building ParseBuilding(const json::object& building_obj) {
    const model::Rectangle bounds{
        {static_cast<model::Coord>(building_obj.at("x").as_int64()),
         static_cast<model::Coord>(building_obj.at("y").as_int64())},
        {static_cast<model::Dimension>(building_obj.at("w").as_int64()),
         static_cast<model::Dimension>(building_obj.at("h").as_int64())}};
    return model::Building{bounds};
}

model::Office ParseOffice(const json::object& office_obj) {
    model::Office::Id id{std::string(office_obj.at("id").as_string())};
    const model::Point position{static_cast<model::Coord>(office_obj.at("x").as_int64()),
                                 static_cast<model::Coord>(office_obj.at("y").as_int64())};
    const model::Offset offset{static_cast<model::Dimension>(office_obj.at("offsetX").as_int64()),
                                static_cast<model::Dimension>(office_obj.at("offsetY").as_int64())};
    return model::Office{std::move(id), position, offset};
}

model::Map ParseMap(const json::object& map_obj) {
    model::Map::Id id{std::string(map_obj.at("id").as_string())};
    std::string name{map_obj.at("name").as_string()};
    model::Map map{std::move(id), std::move(name)};

    for (const auto& road : map_obj.at("roads").as_array()) {
        map.AddRoad(ParseRoad(road.as_object()));
    }
    for (const auto& building : map_obj.at("buildings").as_array()) {
        map.AddBuilding(ParseBuilding(building.as_object()));
    }
    for (const auto& office : map_obj.at("offices").as_array()) {
        map.AddOffice(ParseOffice(office.as_object()));
    }

    return map;
}

json::value ParseConfig(const std::filesystem::path& json_path) {
    std::ifstream input{json_path};
    if (!input) {
        throw std::runtime_error("Failed to open config file: "s + json_path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return json::parse(buffer.str());
}

}  // namespace

model::Game LoadGame(const std::filesystem::path& json_path) {
    const json::value config = ParseConfig(json_path);

    model::Game game;
    for (const auto& map : config.as_object().at("maps").as_array()) {
        game.AddMap(ParseMap(map.as_object()));
    }

    return game;
}

}  // namespace json_loader
