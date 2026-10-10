#pragma once
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
namespace nb {
using Json = nlohmann::json;
using Id = std::int64_t;
class Database {
    sqlite3 *db_{};

  public:
    explicit Database(const std::string &path);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    void exec(const std::string &sql);
    Json rows(const std::string &sql, const Json &params = Json::array());
    Id write(const std::string &sql, const Json &params = Json::array());
    int changes() const;
    void migrate();
    void backup(const std::string &path);
};
// A transaction owns its rollback; an exception cannot leave a half-written event.
class Transaction {
    Database &db_;
    bool committed_ = false;

  public:
    explicit Transaction(Database &db) : db_(db) {
        db_.exec("BEGIN IMMEDIATE");
    }
    ~Transaction() {
        if (!committed_) {
            try {
                db_.exec("ROLLBACK");
            } catch (...) {
            }
        }
    }
    void commit() {
        db_.exec("COMMIT");
        committed_ = true;
    }
};
} // namespace nb
