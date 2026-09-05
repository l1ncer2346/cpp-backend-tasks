#include "json_loader.h"

#include <boost/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace json_loader {

namespace json = boost::json;

namespace {

double Number(const json::value& value) {
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }
    return value.as_double();
}

model::Point PointFrom(const json::object& object, json::string_view x_name = "x0", json::string_view y_name = "y0") {
    return {Number(object.at(x_name)), Number(object.at(y_name))};
}

model::Road ParseRoad(const json::object& object) {
    const model::Point start = PointFrom(object);
    if (auto it = object.find("x1"); it != object.end()) {
        return {start, {Number(it->value()), start.y}, model::Road::Orientation::HORIZONTAL};
    }
    return {start, {start.x, Number(object.at("y1"))}, model::Road::Orientation::VERTICAL};
}

model::Building ParseBuilding(const json::object& object) {
    const model::Rectangle bounds{
        {Number(object.at("x")), Number(object.at("y"))},
        {static_cast<model::Dimension>(Number(object.at("w"))),
         static_cast<model::Dimension>(Number(object.at("h")))}};
    return model::Building{bounds};
}

model::Office ParseOffice(const json::object& object) {
    model::Office::Id id{std::string(object.at("id").as_string())};
    const model::Point position{Number(object.at("x")), Number(object.at("y"))};
    const model::Offset offset{static_cast<model::Dimension>(Number(object.at("offsetX"))),
                               static_cast<model::Dimension>(Number(object.at("offsetY")))};
    return model::Office{std::move(id), position, offset};
}

model::Map ParseMap(const json::object& object, double default_speed) {
    model::Map::Id id{std::string(object.at("id").as_string())};
    std::string name{object.at("name").as_string()};
    double speed = default_speed;
    if (auto it = object.find("dogSpeed"); it != object.end()) {
        speed = Number(it->value());
    }
    model::Map map{std::move(id), std::move(name), speed};
    for (const auto& road : object.at("roads").as_array()) {
        map.AddRoad(ParseRoad(road.as_object()));
    }
    for (const auto& building : object.at("buildings").as_array()) {
        map.AddBuilding(ParseBuilding(building.as_object()));
    }
    for (const auto& office : object.at("offices").as_array()) {
        map.AddOffice(ParseOffice(office.as_object()));
    }
    return map;
}

json::value ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return json::parse(buffer.str());
}

}  // namespace

model::Game LoadGame(const std::filesystem::path& path) {
    const json::value config_value = ReadJson(path);
    const json::object& config = config_value.as_object();
    double default_speed = 1.0;
    if (auto it = config.find("defaultDogSpeed"); it != config.end()) {
        default_speed = Number(it->value());
    }
    model::Game game{default_speed};
    for (const auto& map : config.at("maps").as_array()) {
        game.AddMap(ParseMap(map.as_object(), default_speed));
    }
    return game;
}

}  // namespace json_loader
