#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tagged.h"

namespace model {

using Coord = int;
using Dimension = int;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Size {
    Dimension width = 0;
    Dimension height = 0;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx = 0;
    Dimension dy = 0;
};

class Road {
public:
    enum class Orientation { HORIZONTAL, VERTICAL };

    Road(Point start, Point end, Orientation orientation) noexcept
        : start_(start)
        , end_(end)
        , orientation_(orientation) {
    }

    bool IsHorizontal() const noexcept {
        return orientation_ == Orientation::HORIZONTAL;
    }

    bool IsVertical() const noexcept {
        return orientation_ == Orientation::VERTICAL;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
    Orientation orientation_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_(bounds) {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_(std::move(id))
        , position_(position)
        , offset_(offset) {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name, double dog_speed) noexcept
        : id_(std::move(id))
        , name_(std::move(name))
        , dog_speed_(dog_speed) {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    double GetDogSpeed() const noexcept {
        return dog_speed_;
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    void AddRoad(const Road& road) {
        roads_.push_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.push_back(building);
    }

    void AddOffice(Office office);

private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    double dog_speed_ = 1.0;
    Roads roads_;
    Buildings buildings_;
    OfficeIdToIndex office_id_to_index_;
    Offices offices_;
};

enum class Direction { NORTH, EAST, SOUTH, WEST };

struct PlayerState {
    std::uint64_t id = 0;
    std::string name;
    Point position;
    Point speed;
    Direction direction = Direction::NORTH;
};

struct JoinResult {
    bool map_found = false;
    std::uint64_t player_id = 0;
    std::string token;
};

class Game {
public:
    using Maps = std::vector<Map>;

    explicit Game(double default_dog_speed = 1.0)
        : default_dog_speed_(default_dog_speed) {
    }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&& other) noexcept
        : default_dog_speed_(other.default_dog_speed_)
        , maps_(std::move(other.maps_))
        , map_id_to_index_(std::move(other.map_id_to_index_))
        , players_(std::move(other.players_))
        , next_player_id_(other.next_player_id_) {
    }

    Game& operator=(Game&&) = delete;

    void AddMap(Map map);

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
            return &maps_.at(it->second);
        }
        return nullptr;
    }

    JoinResult Join(std::string user_name, const Map::Id& map_id, bool randomize_spawn_points);
    std::optional<std::vector<PlayerState>> GetState(const std::string& token) const;
    bool Move(const std::string& token, char move);
    void Tick(std::int64_t delta_ms);

private:
    struct Player {
        std::string name;
        std::string token;
        std::uint64_t id = 0;
        const Map* map = nullptr;
        Point position;
        Point speed;
        Direction direction = Direction::NORTH;
    };

    static std::string MakeToken();
    static Point SpawnAt(const Map& map, bool randomize);
    static void ApplyMove(Player& player, char move);
    static bool IsOnRoad(const Road& road, Point point);
    static Point BoundToRoad(const Road& road, Point point);
    static std::vector<Road> MakeMovementRoads(const Map& map);
    static std::optional<Point> MoveOnRoads(const std::vector<Road>& roads, Point start, Point stop);

    double SpeedFor(const Map& map) const noexcept {
        return map.GetDogSpeed() > 0.0 ? map.GetDogSpeed() : default_dog_speed_;
    }

    double default_dog_speed_ = 1.0;
    Maps maps_;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, util::TaggedHasher<Map::Id>>;
    MapIdToIndex map_id_to_index_;
    std::vector<Player> players_;
    std::uint64_t next_player_id_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace model
