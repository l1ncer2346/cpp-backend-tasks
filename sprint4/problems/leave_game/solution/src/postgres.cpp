#include "postgres.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <pqxx/pqxx>
#include <pqxx/zview.hxx>

namespace postgres {

using namespace std::literals;
using pqxx::operator"" _zv;

namespace {

constexpr auto kInsertRecord = "insert_record"_zv;
constexpr auto kSelectRecords = "select_records"_zv;

void PrepareStatements(pqxx::connection& conn) {
    conn.prepare(kInsertRecord,
                 "INSERT INTO retired_players (id, name, score, play_time_ms) VALUES ($1, $2, $3, $4);"_zv);
    conn.prepare(kSelectRecords,
                 "SELECT name, score, play_time_ms FROM retired_players "
                 "ORDER BY score DESC, play_time_ms, name LIMIT $1 OFFSET $2;"_zv);
}

void CreateTables(pqxx::connection& conn) {
    pqxx::work work{conn};
    work.exec(R"(
CREATE TABLE IF NOT EXISTS retired_players (
    id UUID CONSTRAINT retired_player_id_constraint PRIMARY KEY,
    name varchar(100) NOT NULL,
    score integer NOT NULL,
    play_time_ms integer NOT NULL
);
)"_zv);
    work.exec("CREATE INDEX IF NOT EXISTS retired_players_score_idx "
              "ON retired_players (score DESC, play_time_ms, name);"_zv);
    work.commit();
}

std::string NewId() {
    static thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

}  // namespace

ConnectionPool::ConnectionPool(size_t capacity, const std::string& db_url) {
    pool_.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        auto conn = std::make_shared<pqxx::connection>(db_url);
        if (i == 0) {
            CreateTables(*conn);
        }
        PrepareStatements(*conn);
        pool_.push_back(std::move(conn));
    }
}

ConnectionPool::ConnectionWrapper ConnectionPool::GetConnection() {
    std::unique_lock lock{mutex_};
    cond_var_.wait(lock, [this] {
        return used_connections_ < pool_.size();
    });
    return {std::move(pool_[used_connections_++]), *this};
}

void ConnectionPool::ReturnConnection(ConnectionPtr&& conn) {
    {
        std::lock_guard lock{mutex_};
        pool_[--used_connections_] = std::move(conn);
    }
    cond_var_.notify_one();
}

RecordsRepository::RecordsRepository(size_t pool_size, const std::string& db_url)
    : pool_(pool_size, db_url) {
}

void RecordsRepository::Save(const std::vector<model::RetiredPlayer>& players) {
    if (players.empty()) {
        return;
    }
    auto conn = pool_.GetConnection();
    pqxx::work work{*conn};
    for (const auto& player : players) {
        work.exec_prepared(kInsertRecord, NewId(), player.name, static_cast<std::int64_t>(player.score),
                           player.play_time_ms);
    }
    work.commit();
}

std::vector<Record> RecordsRepository::Get(size_t start, size_t max_items) {
    auto conn = pool_.GetConnection();
    pqxx::read_transaction transaction{*conn};
    std::vector<Record> records;
    const auto result = transaction.exec_prepared(kSelectRecords, static_cast<std::int64_t>(max_items),
                                                  static_cast<std::int64_t>(start));
    records.reserve(result.size());
    for (const auto& row : result) {
        records.push_back({row[0].as<std::string>(), row[1].as<std::uint64_t>(), row[2].as<std::int64_t>()});
    }
    return records;
}

}  // namespace postgres
