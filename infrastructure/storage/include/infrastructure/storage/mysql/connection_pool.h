#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kd39::infrastructure::storage::mysql {

struct MysqlConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string user = "root";
    std::string passwd;
    std::string db;
    int pool_size = 4;
};

using QueryRow = std::unordered_map<std::string, std::string>;

class ConnectionPool {
public:
    virtual ~ConnectionPool() = default;

    static std::shared_ptr<ConnectionPool> Create(const MysqlConfig& cfg);

    virtual bool Execute(std::string_view sql) = 0;
    virtual std::vector<QueryRow> Query(std::string_view sql) = 0;
    virtual bool UpsertRow(const std::string& table,
                           const QueryRow& row,
                           const std::vector<std::string>& key_fields) = 0;
    virtual std::optional<QueryRow> GetRow(const std::string& table,
                                           const std::string& key_field,
                                           const std::string& key_value) = 0;
    virtual std::vector<QueryRow> FindRows(const std::string& table,
                                           const QueryRow& filters) = 0;
};

}  // namespace kd39::infrastructure::storage::mysql
