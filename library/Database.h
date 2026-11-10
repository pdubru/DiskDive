#pragma once
// Thin RAII wrapper over SQLite. The sqlite3 header is PRIVATE to library/ (same rule as TagLib in core/):
// nothing here exposes sqlite types, so callers never depend on the SQLite C API.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace diskdive {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One prepared statement. Parameters are 1-based, columns are 0-based (SQLite's own convention).
// Must not outlive the Database it was prepared on.
class Statement {
public:
    ~Statement();
    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, const std::string& value);
    Statement& bindNull(int index);

    bool step();   // true = a row is available, false = finished
    void reset();  // rewind so the statement can be run again with new bindings

    std::int64_t columnInt64(int index) const;
    std::string columnText(int index) const;

private:
    friend class Database;
    struct Impl;
    explicit Statement(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

class Database {
public:
    // Opens (creating if needed) a database file. Pass ":memory:" for a throw-away in-RAM database (tests).
    explicit Database(const std::filesystem::path& file);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const std::string& sql);  // for statements without parameters or results
    Statement prepare(const std::string& sql);
    std::int64_t lastInsertRowId() const;

    // Scalar PRAGMA user_version: we use it as the schema version number.
    int userVersion();
    void setUserVersion(int v);

private:
    friend class Statement;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// BEGIN ... COMMIT around a block of work; rolls back if destroyed without commit() (e.g. on an exception).
// Also makes bulk inserts far faster: SQLite syncs to disk once per transaction, not once per statement.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    bool done_ = false;
};

}  // namespace diskdive
