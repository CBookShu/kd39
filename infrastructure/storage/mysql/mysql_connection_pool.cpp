#include "infrastructure/storage/mysql/connection_pool.h"

#include <mutex>
#include <unordered_map>

#include "common/log/logger.h"

namespace kd39::infrastructure::storage::mysql {
namespace {
struct TableState {
    std::unordered_map<std::string, QueryRow> rows;
};

struct BackendState {
    std::mutex mu;
    std::unordered_map<std::string, TableState> tables;
};

std::shared_ptr<BackendState> SharedStateFor(const MysqlConfig& cfg) {
    static std::mutex global_mu;
    static std::unordered_map<std::string, std::weak_ptr<BackendState>> states;
    const auto key = cfg.host + ":" + std::to_string(cfg.port) + "/" + cfg.db + "/" + cfg.user;
    std::scoped_lock lock(global_mu);
    if (auto existing = states[key].lock()) {
        return existing;
    }
    auto created = std::make_shared<BackendState>();
    states[key] = created;
    return created;
}

std::string BuildCompositeKey(const QueryRow& row, const std::vector<std::string>& key_fields) {
    std::string result;
    for (const auto& field : key_fields) {
        if (!result.empty()) result += '|';
        if (auto it = row.find(field); it != row.end()) {
            result += it->second;
        }
    }
    return result;
}

bool Matches(const QueryRow& row, const QueryRow& filters) {
    for (const auto& [key, value] : filters) {
        auto it = row.find(key);
        if (it == row.end() || it->second != value) {
            return false;
        }
    }
    return true;
}
}  // namespace

class ConnectionPoolImpl final : public ConnectionPool {
public:
    explicit ConnectionPoolImpl(MysqlConfig cfg)
        : cfg_(std::move(cfg)), state_(SharedStateFor(cfg_)) {
        KD39_LOG_INFO("MySQL pool ready: {}:{}/{} pool_size={}",
                      cfg_.host, cfg_.port, cfg_.db, cfg_.pool_size);
    }

    bool Execute(std::string_view sql) override {
        KD39_LOG_DEBUG("MySQL Execute: {}", sql);
        return true;
    }

    std::vector<QueryRow> Query(std::string_view sql) override {
        KD39_LOG_DEBUG("MySQL Query: {}", sql);
        return {};
    }

    bool UpsertRow(const std::string& table,
                   const QueryRow& row,
                   const std::vector<std::string>& key_fields) override {
        std::scoped_lock lock(state_->mu);
        state_->tables[table].rows[BuildCompositeKey(row, key_fields)] = row;
        return true;
    }

    std::optional<QueryRow> GetRow(const std::string& table,
                                   const std::string& key_field,
                                   const std::string& key_value) override {
        std::scoped_lock lock(state_->mu);
        auto table_it = state_->tables.find(table);
        if (table_it == state_->tables.end()) {
            return std::nullopt;
        }
        for (const auto& [_, row] : table_it->second.rows) {
            auto it = row.find(key_field);
            if (it != row.end() && it->second == key_value) {
                return row;
            }
        }
        return std::nullopt;
    }

    std::vector<QueryRow> FindRows(const std::string& table,
                                   const QueryRow& filters) override {
        std::vector<QueryRow> rows;
        std::scoped_lock lock(state_->mu);
        auto table_it = state_->tables.find(table);
        if (table_it == state_->tables.end()) {
            return rows;
        }
        for (const auto& [_, row] : table_it->second.rows) {
            if (Matches(row, filters)) {
                rows.push_back(row);
            }
        }
        return rows;
    }

private:
    MysqlConfig cfg_;
    std::shared_ptr<BackendState> state_;
};

std::shared_ptr<ConnectionPool> ConnectionPool::Create(const MysqlConfig& cfg) {
    return std::make_shared<ConnectionPoolImpl>(cfg);
}

}  // namespace kd39::infrastructure::storage::mysql
