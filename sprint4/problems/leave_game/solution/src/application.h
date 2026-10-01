#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "model.h"
#include "postgres.h"

namespace app {

// Получает уведомления о прошедшем игровом времени
class ApplicationListener {
public:
    virtual void OnTick(std::chrono::milliseconds delta) = 0;

protected:
    ~ApplicationListener() = default;
};

class Application {
public:
    Application(model::Game& game, postgres::RecordsRepository& records)
        : game_(game)
        , records_(records) {
    }

    const model::Game::Maps& GetMaps() const noexcept {
        return game_.GetMaps();
    }

    const model::Map* FindMap(const model::Map::Id& id) const noexcept {
        return game_.FindMap(id);
    }

    model::JoinResult Join(std::string user_name, const model::Map::Id& map_id, bool randomize_spawn_points) {
        return game_.Join(std::move(user_name), map_id, randomize_spawn_points);
    }

    std::optional<model::GameState> GetState(const std::string& token) const {
        return game_.GetState(token);
    }

    bool Move(const std::string& token, char move) {
        return game_.Move(token, move);
    }

    void Tick(std::int64_t delta_ms);

    std::vector<postgres::Record> GetRecords(size_t start, size_t max_items) {
        return records_.Get(start, max_items);
    }

    void SetListener(ApplicationListener* listener) noexcept {
        listener_ = listener;
    }

private:
    model::Game& game_;
    postgres::RecordsRepository& records_;
    ApplicationListener* listener_ = nullptr;
};

// Периодически сохраняет состояние игры в файл
class StateSaver final : public ApplicationListener {
public:
    StateSaver(const model::Game& game, std::filesystem::path state_file,
               std::optional<std::chrono::milliseconds> period)
        : game_(game)
        , state_file_(std::move(state_file))
        , period_(period) {
    }

    void OnTick(std::chrono::milliseconds delta) override;
    void Save();

private:
    const model::Game& game_;
    std::filesystem::path state_file_;
    std::optional<std::chrono::milliseconds> period_;
    std::chrono::milliseconds since_last_save_{0};
    std::mutex mutex_;
};

}  // namespace app
