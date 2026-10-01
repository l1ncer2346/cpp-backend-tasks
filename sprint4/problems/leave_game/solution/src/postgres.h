#pragma once

#include <pqxx/connection>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "model.h"

namespace postgres {

class ConnectionPool {
    using PoolType = ConnectionPool;
    using ConnectionPtr = std::shared_ptr<pqxx::connection>;

public:
    class ConnectionWrapper {
    public:
        ConnectionWrapper(ConnectionPtr&& conn, PoolType& pool) noexcept
            : conn_(std::move(conn))
            , pool_(&pool) {
        }

        ConnectionWrapper(const ConnectionWrapper&) = delete;
        ConnectionWrapper& operator=(const ConnectionWrapper&) = delete;
        ConnectionWrapper(ConnectionWrapper&&) = default;
        ConnectionWrapper& operator=(ConnectionWrapper&&) = default;

        pqxx::connection& operator*() const& noexcept {
            return *conn_;
        }
        pqxx::connection& operator*() const&& = delete;

        pqxx::connection* operator->() const& noexcept {
            return conn_.get();
        }

        ~ConnectionWrapper() {
            if (conn_) {
                pool_->ReturnConnection(std::move(conn_));
            }
        }

    private:
        ConnectionPtr conn_;
        PoolType* pool_;
    };

    ConnectionPool(size_t capacity, const std::string& db_url);

    ConnectionWrapper GetConnection();

private:
    void ReturnConnection(ConnectionPtr&& conn);

    std::mutex mutex_;
    std::condition_variable cond_var_;
    std::vector<ConnectionPtr> pool_;
    size_t used_connections_ = 0;
};

struct Record {
    std::string name;
    std::uint64_t score = 0;
    std::int64_t play_time_ms = 0;
};

// Таблица рекордов ушедших на покой игроков
class RecordsRepository {
public:
    RecordsRepository(size_t pool_size, const std::string& db_url);

    void Save(const std::vector<model::RetiredPlayer>& players);
    std::vector<Record> Get(size_t start, size_t max_items);

private:
    ConnectionPool pool_;
};

}  // namespace postgres
