#include "model.h"

#include "collision_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace model {

namespace {

constexpr double kRoadWidth = 0.4;
constexpr double kItemHalfWidth = 0.0;
constexpr double kDogHalfWidth = 0.3;
constexpr double kOfficeHalfWidth = 0.25;

double Clamp(double value, double first, double second) {
    const double low = std::min(first, second);
    const double high = std::max(first, second);
    return std::clamp(value, low, high);
}

double DistanceSquared(Point a, Point b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool SameLine(const Road& left, const Road& right) {
    if (left.IsVertical() != right.IsVertical()) {
        return false;
    }
    if (left.IsVertical()) {
        return left.GetStart().x == right.GetStart().x;
    }
    return left.GetStart().y == right.GetStart().y;
}

Road MergeRoads(const Road& left, const Road& right) {
    const auto a = left.GetStart();
    const auto b = left.GetEnd();
    const auto c = right.GetStart();
    const auto d = right.GetEnd();
    if (left.IsVertical()) {
        return Road{{a.x, std::min({a.y, b.y, c.y, d.y})},
                    {a.x, std::max({a.y, b.y, c.y, d.y})}, Road::Orientation::VERTICAL};
    }
    return Road{{std::min({a.x, b.x, c.x, d.x}), a.y},
                {std::max({a.x, b.x, c.x, d.x}), a.y}, Road::Orientation::HORIZONTAL};
}

}  // namespace

void Map::AddOffice(Office office) {
    if (office_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate office");
    }
    const size_t index = offices_.size();
    Office& stored = offices_.emplace_back(std::move(office));
    try {
        office_id_to_index_.emplace(stored.GetId(), index);
    } catch (...) {
        offices_.pop_back();
        throw;
    }
}

void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    const Map::Id map_id = map.GetId();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Duplicate map: " + *map.GetId());
    }
    try {
        maps_.push_back(std::move(map));
        map_loot_.push_back({loot_gen::LootGenerator{loot_config_.period, loot_config_.probability}, {}});
    } catch (...) {
        if (maps_.size() > map_loot_.size()) {
            maps_.pop_back();
        }
        map_id_to_index_.erase(map_id);
        throw;
    }
}

std::string Game::MakeToken() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        out << std::setw(16) << generator();
    }
    return out.str().substr(0, 32);
}

Point Game::SpawnAt(const Map& map, bool randomize) {
    if (map.GetRoads().empty()) {
        return {};
    }
    if (!randomize) {
        return map.GetRoads().front().GetStart();
    }
    return RandomPointOnRoads(map);
}

Point Game::RandomPointOnRoads(const Map& map) {
    if (map.GetRoads().empty()) {
        return {};
    }
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<size_t> road_distribution(0, map.GetRoads().size() - 1);
    const Road& road = map.GetRoads()[road_distribution(generator)];
    const Point start = road.GetStart();
    const Point end = road.GetEnd();
    const double length = road.IsHorizontal() ? std::abs(end.x - start.x) : std::abs(end.y - start.y);
    std::uniform_real_distribution<double> distance_distribution(0.0, length);
    const double distance = distance_distribution(generator);
    if (road.IsHorizontal()) {
        return {start.x < end.x ? start.x + distance : start.x - distance, start.y};
    }
    return {start.x, start.y < end.y ? start.y + distance : start.y - distance};
}

JoinResult Game::Join(std::string user_name, const Map::Id& map_id, bool randomize_spawn_points) {
    std::lock_guard lock(mutex_);
    const Map* map = FindMap(map_id);
    if (!map) {
        return {};
    }
    Player player;
    player.name = std::move(user_name);
    player.token = MakeToken();
    player.id = next_player_id_++;
    player.map = map;
    player.position = SpawnAt(*map, randomize_spawn_points);
    players_.push_back(std::move(player));
    return {true, players_.back().id, players_.back().token};
}

std::optional<GameState> Game::GetState(const std::string& token) const {
    std::lock_guard lock(mutex_);
    const Player* requested_player = nullptr;
    for (const Player& player : players_) {
        if (player.token == token) {
            requested_player = &player;
            break;
        }
    }
    if (!requested_player) {
        return std::nullopt;
    }

    GameState result;
    for (const Player& player : players_) {
        if (player.map == requested_player->map) {
            result.players.push_back(
                {player.id, player.name, player.position, player.speed, player.direction, player.bag, player.score});
        }
    }
    const size_t map_index = map_id_to_index_.at(requested_player->map->GetId());
    result.lost_objects = map_loot_[map_index].objects;
    return result;
}

void Game::ApplyMove(Player& player, char move) {
    const double speed = player.map->GetDogSpeed();
    switch (move) {
        case 'L':
            player.speed = {-speed, 0.0};
            player.direction = Direction::WEST;
            break;
        case 'R':
            player.speed = {speed, 0.0};
            player.direction = Direction::EAST;
            break;
        case 'U':
            player.speed = {0.0, -speed};
            player.direction = Direction::NORTH;
            break;
        case 'D':
            player.speed = {0.0, speed};
            player.direction = Direction::SOUTH;
            break;
        case '\0':
            player.speed = {0.0, 0.0};
            break;
        default:
            throw std::invalid_argument("Invalid move");
    }
}

bool Game::Move(const std::string& token, char move) {
    std::lock_guard lock(mutex_);
    for (Player& player : players_) {
        if (player.token == token) {
            ApplyMove(player, move);
            return true;
        }
    }
    return false;
}

bool Game::IsOnRoad(const Road& road, Point point) {
    const Point start = road.GetStart();
    const Point end = road.GetEnd();
    return point.x >= std::min(start.x, end.x) - kRoadWidth &&
           point.x <= std::max(start.x, end.x) + kRoadWidth &&
           point.y >= std::min(start.y, end.y) - kRoadWidth &&
           point.y <= std::max(start.y, end.y) + kRoadWidth;
}

Point Game::BoundToRoad(const Road& road, Point point) {
    const Point start = road.GetStart();
    const Point end = road.GetEnd();
    return {Clamp(point.x, std::min(start.x, end.x) - kRoadWidth, std::max(start.x, end.x) + kRoadWidth),
            Clamp(point.y, std::min(start.y, end.y) - kRoadWidth, std::max(start.y, end.y) + kRoadWidth)};
}

std::vector<Road> Game::MakeMovementRoads(const Map& map) {
    std::vector<Road> vertical;
    std::vector<Road> horizontal;
    for (const Road& road : map.GetRoads()) {
        Point start = road.GetStart();
        Point end = road.GetEnd();
        if (road.IsVertical()) {
            if (start.y > end.y) {
                std::swap(start, end);
            }
            vertical.emplace_back(start, end, Road::Orientation::VERTICAL);
        } else {
            if (start.x > end.x) {
                std::swap(start, end);
            }
            horizontal.emplace_back(start, end, Road::Orientation::HORIZONTAL);
        }
    }

    std::vector<Road> merged;
    auto merge_adjacent = [&merged](std::vector<Road>& roads) {
        std::vector<bool> removed(roads.size(), false);
        for (size_t i = 0; i < roads.size(); ++i) {
            if (removed[i]) {
                continue;
            }
            for (size_t j = i + 1; j < roads.size(); ++j) {
                if (removed[j] || !SameLine(roads[i], roads[j])) {
                    continue;
                }
                const auto first = roads[i].GetStart();
                const auto last = roads[i].GetEnd();
                const auto other_first = roads[j].GetStart();
                const auto other_last = roads[j].GetEnd();
                const bool adjacent = roads[i].IsVertical()
                                           ? (last.y == other_first.y || first.y == other_last.y)
                                           : (last.x == other_first.x || first.x == other_last.x);
                if (adjacent) {
                    merged.push_back(MergeRoads(roads[i], roads[j]));
                    removed[i] = true;
                    removed[j] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < roads.size(); ++i) {
            if (!removed[i]) {
                merged.push_back(roads[i]);
            }
        }
    };
    merge_adjacent(vertical);
    merge_adjacent(horizontal);
    return merged;
}

std::optional<Point> Game::MoveOnRoads(const std::vector<Road>& roads, Point start, Point stop) {
    std::optional<Point> best;
    double best_distance = -1.0;
    for (const Road& road : roads) {
        if (!IsOnRoad(road, start)) {
            continue;
        }
        Point candidate = BoundToRoad(road, stop);
        const double distance = DistanceSquared(start, candidate);
        if (!best || distance > best_distance) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

namespace {

class GatherProvider : public collision_detector::ItemGathererProvider {
public:
    void AddItem(Point position, double width) {
        items_.push_back({{position.x, position.y}, width});
    }

    void AddGatherer(Point start, Point end, double width) {
        gatherers_.push_back({{start.x, start.y}, {end.x, end.y}, width});
    }

    size_t ItemsCount() const override {
        return items_.size();
    }

    collision_detector::Item GetItem(size_t idx) const override {
        return items_.at(idx);
    }

    size_t GatherersCount() const override {
        return gatherers_.size();
    }

    collision_detector::Gatherer GetGatherer(size_t idx) const override {
        return gatherers_.at(idx);
    }

private:
    std::vector<collision_detector::Item> items_;
    std::vector<collision_detector::Gatherer> gatherers_;
};

}  // namespace

void Game::GatherLoot(size_t map_index, const std::vector<Movement>& movements) {
    const Map& map = maps_[map_index];
    auto& objects = map_loot_[map_index].objects;
    if (movements.empty()) {
        return;
    }

    // loot goes first, offices follow it
    GatherProvider provider;
    for (const LostObject& object : objects) {
        provider.AddItem(object.position, kItemHalfWidth);
    }
    for (const Office& office : map.GetOffices()) {
        provider.AddItem(office.GetPosition(), kOfficeHalfWidth);
    }
    for (const Movement& movement : movements) {
        provider.AddGatherer(movement.start, movement.end, kDogHalfWidth);
    }

    std::vector<bool> collected(objects.size(), false);
    for (const auto& event : collision_detector::FindGatherEvents(provider)) {
        Player& player = *movements[event.gatherer_id].player;
        if (event.item_id >= objects.size()) {
            for (const LootItem& item : player.bag) {
                player.score += map.GetLootValue(item.type);
            }
            player.bag.clear();
            continue;
        }
        if (collected[event.item_id] || player.bag.size() >= map.GetBagCapacity()) {
            continue;
        }
        collected[event.item_id] = true;
        player.bag.push_back(objects[event.item_id].item);
    }

    size_t index = 0;
    std::erase_if(objects, [&collected, &index](const LostObject&) {
        return collected[index++];
    });
}

void Game::GenerateLoot(size_t map_index, std::chrono::milliseconds delta) {
    const Map& map = maps_[map_index];
    if (map.GetLootTypesCount() == 0 || map.GetRoads().empty()) {
        return;
    }
    const auto looters = static_cast<unsigned>(
        std::count_if(players_.begin(), players_.end(), [&map](const Player& player) {
            return player.map == &map;
        }));
    MapLoot& loot = map_loot_[map_index];
    const unsigned count =
        loot.generator.Generate(delta, static_cast<unsigned>(loot.objects.size()), looters);

    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<size_t> type_distribution(0, map.GetLootTypesCount() - 1);
    for (unsigned i = 0; i < count; ++i) {
        loot.objects.push_back({{next_loot_id_++, type_distribution(generator)}, RandomPointOnRoads(map)});
    }
}

void Game::Tick(std::int64_t delta_ms) {
    if (delta_ms < 0) {
        return;
    }
    std::lock_guard lock(mutex_);
    std::vector<std::vector<Movement>> movements(maps_.size());
    for (Player& player : players_) {
        const Point start = player.position;
        const Point target{player.position.x + player.speed.x * (static_cast<double>(delta_ms) / 1000.0),
                           player.position.y + player.speed.y * (static_cast<double>(delta_ms) / 1000.0)};
        const auto roads = MakeMovementRoads(*player.map);
        const auto bounded = MoveOnRoads(roads, player.position, target);
        if (!bounded) {
            continue;
        }
        player.position = *bounded;
        if (std::abs(bounded->x - target.x) > std::numeric_limits<double>::epsilon() ||
            std::abs(bounded->y - target.y) > std::numeric_limits<double>::epsilon()) {
            player.speed = {0.0, 0.0};
        }
        movements[map_id_to_index_.at(player.map->GetId())].push_back({&player, start, player.position});
    }

    for (size_t i = 0; i < maps_.size(); ++i) {
        GatherLoot(i, movements[i]);
        GenerateLoot(i, std::chrono::milliseconds{delta_ms});
    }
}

}  // namespace model
