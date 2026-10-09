#include "database.hpp"
#include "schema.hpp"
#include <memory>
#include <stdexcept>
namespace nb {
Database::Database(const std::string &path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        if (db_)
            sqlite3_close(db_);
        throw std::runtime_error("Database open failed");
    }
    try {
        exec("PRAGMA foreign_keys=ON; PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA "
             "synchronous=FULL;");
    } catch (...) {
        sqlite3_close(db_);
        throw;
    }
}
Database::~Database() {
    sqlite3_close(db_);
}
void Database::exec(const std::string &sql) {
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Database operation failed");
}
Json Database::rows(const std::string &sql, const Json &params) {
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error("Database prepare failed");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
    int i = 1;
    for (const auto &p : params) {
        int rc = SQLITE_ERROR;
        if (p.is_null())
            rc = sqlite3_bind_null(raw, i);
        else if (p.is_number_integer())
            rc = sqlite3_bind_int64(raw, i, p.get<Id>());
        else if (p.is_number())
            rc = sqlite3_bind_double(raw, i, p.get<double>());
        else if (p.is_boolean())
            rc = sqlite3_bind_int(raw, i, p.get<bool>());
        else if (p.is_string()) {
            auto s = p.get<std::string>();
            rc = sqlite3_bind_text(raw, i, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK)
            throw std::runtime_error("Database bind failed");
        ++i;
    }
    Json result = Json::array();
    int rc;
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
        Json row = Json::object();
        for (int c = 0; c < sqlite3_column_count(raw); ++c) {
            auto name = sqlite3_column_name(raw, c);
            switch (sqlite3_column_type(raw, c)) {
            case SQLITE_INTEGER:
                row[name] = static_cast<Id>(sqlite3_column_int64(raw, c));
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(raw, c);
                break;
            case SQLITE_TEXT:
                row[name] = reinterpret_cast<const char *>(sqlite3_column_text(raw, c));
                break;
            default:
                row[name] = nullptr;
            }
        }
        result.push_back(row);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Database step failed");
    return result;
}
Id Database::write(const std::string &sql, const Json &params) {
    rows(sql, params);
    return sqlite3_last_insert_rowid(db_);
}
int Database::changes() const {
    return sqlite3_changes(db_);
}
void Database::migrate() {
    Transaction tx(*this);
    auto exists = rows("SELECT name FROM sqlite_master WHERE name='schema_migrations'");
    if (exists.empty())
        exec(schema_sql);
    else if (rows("SELECT MAX(version) AS v FROM schema_migrations")[0]["v"] != 1)
        throw std::runtime_error("Unsupported database schema");
    tx.commit();
}
void Database::backup(const std::string &path) {
    Database target(path);
    sqlite3_backup *b = sqlite3_backup_init(target.db_, "main", db_, "main");
    if (!b)
        throw std::runtime_error("Backup init failed");
    int rc = sqlite3_backup_step(b, -1);
    int end = sqlite3_backup_finish(b);
    if (rc != SQLITE_DONE || end != SQLITE_OK)
        throw std::runtime_error("Backup failed");
}
} // namespace nb
