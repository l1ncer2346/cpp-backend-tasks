#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "loot_generator.h"
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

    Map(Id id, std::string name, double dog_speed, size_t bag_capacity) noexcept
        : id_(std::move(id))
        , name_(std::move(name))
        , dog_speed_(dog_speed)
        , bag_capacity_(bag_capacity) {
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

    size_t GetBagCapacity() const noexcept {
        return bag_capacity_;
    }

    size_t GetLootTypesCount() const noexcept {
        return loot_values_.size();
    }

    std::uint64_t GetLootValue(size_t type) const {
        return loot_values_.at(type);
    }

    void AddLootType(std::uint64_t value) {
        loot_values_.push_back(value);
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
    size_t bag_capacity_ = 3;
    std::vector<std::uint64_t> loot_values_;
    Roads roads_;
    Buildings buildings_;
    OfficeIdToIndex office_id_to_index_;
    Offices offices_;
};

enum class Direction { NORTH, EAST, SOUTH, WEST };

struct LootItem {
    std::uint64_t id = 0;
    size_t type = 0;
};

struct LostObject {
    LootItem item;
    Point position;
};

struct PlayerState {
    std::uint64_t id = 0;
    std::string name;
    Point position;
    Point speed;
    Direction direction = Direction::NORTH;
    std::vector<LootItem> bag;
    std::uint64_t score = 0;
};

struct GameState {
    std::vector<PlayerState> players;
    std::vector<LostObject> lost_objects;
};

struct LootGeneratorConfig {
    std::chrono::milliseconds period{5000};
    double probability = 0.5;
};

struct GameSettings {
    double default_dog_speed = 1.0;
    LootGeneratorConfig loot_config;
    std::chrono::milliseconds retirement_time{60000};
};

struct JoinResult {
    bool map_found = false;
    std::uint64_t player_id = 0;
    std::string token;
};

// Игрок, ушедший на покой
struct RetiredPlayer {
    std::string name;
    std::uint64_t score = 0;
    std::int64_t play_time_ms = 0;
};

// Полное состояние игрока для сохранения
struct PlayerSnapshot {
    std::uint64_t id = 0;
    std::string name;
    std::string token;
    std::string map_id;
    Point position;
    Point speed;
    Direction direction = Direction::NORTH;
    std::vector<LootItem> bag;
    std::uint64_t score = 0;
    std::int64_t idle_time_ms = 0;
    std::int64_t play_time_ms = 0;
};

struct MapLootSnapshot {
    std::string map_id;
    std::vector<LostObject> objects;
};

struct GameSnapshot {
    std::vector<PlayerSnapshot> players;
    std::vector<MapLootSnapshot> loot;
    std::uint64_t next_player_id = 0;
    std::uint64_t next_loot_id = 0;
};

class Game {
public:
    using Maps = std::vector<Map>;

    explicit Game(GameSettings settings = {})
        : settings_(settings) {
    }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&& other) noexcept
        : settings_(other.settings_)
        , maps_(std::move(other.maps_))
        , map_id_to_index_(std::move(other.map_id_to_index_))
        , map_loot_(std::move(other.map_loot_))
        , map_roads_(std::move(other.map_roads_))
        , players_(std::move(other.players_))
        , next_player_id_(other.next_player_id_)
        , next_loot_id_(other.next_loot_id_) {
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
    std::optional<GameState> GetState(const std::string& token) const;
    bool Move(const std::string& token, char move);
    // Возвращает игроков, ушедших на покой за этот тик
    std::vector<RetiredPlayer> Tick(std::int64_t delta_ms);

    GameSnapshot MakeSnapshot() const;
    void Restore(const GameSnapshot& snapshot);

private:
    struct Player {
        std::string name;
        std::string token;
        std::uint64_t id = 0;
        const Map* map = nullptr;
        Point position;
        Point speed;
        Direction direction = Direction::NORTH;
        std::vector<LootItem> bag;
        std::uint64_t score = 0;
        std::int64_t idle_time_ms = 0;
        std::int64_t play_time_ms = 0;
    };

    struct MapLoot {
        loot_gen::LootGenerator generator;
        std::vector<LostObject> objects;
    };

    static std::string MakeToken();
    static Point RandomPointOnRoads(const Map& map);
    static Point SpawnAt(const Map& map, bool randomize);
    static void ApplyMove(Player& player, char move);
    static bool IsOnRoad(const Road& road, Point point);
    static Point BoundToRoad(const Road& road, Point point);
    static std::vector<Road> MakeMovementRoads(const Map& map);
    static std::optional<Point> MoveOnRoads(const std::vector<Road>& roads, Point start, Point stop);

    struct Movement {
        Player* player;
        Point start;
        Point end;
    };

    void MovePlayer(Player& player, std::int64_t delta_ms, std::vector<std::vector<Movement>>& movements);
    void GatherLoot(size_t map_index, const std::vector<Movement>& movements);
    void GenerateLoot(size_t map_index, std::chrono::milliseconds delta);
    std::vector<RetiredPlayer> RetireIdlePlayers();
    size_t MapIndex(const Map& map) const;

    GameSettings settings_;
    Maps maps_;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, util::TaggedHasher<Map::Id>>;
    MapIdToIndex map_id_to_index_;
    std::vector<MapLoot> map_loot_;               // индексы совпадают с maps_
    std::vector<std::vector<Road>> map_roads_;    // объединённые дороги для движения
    std::vector<Player> players_;
    std::uint64_t next_player_id_ = 0;
    std::uint64_t next_loot_id_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace model
