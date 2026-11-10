#include "Database.h"

#include <sqlite3.h>

namespace diskdive {

struct Database::Impl {
    sqlite3* db = nullptr;
};

struct Statement::Impl {
    sqlite3* db = nullptr;  // only for error messages
    sqlite3_stmt* stmt = nullptr;
};

static std::string errorOf(sqlite3* db, const std::string& what) {
    return what + ": " + sqlite3_errmsg(db);
}

// ---- Statement ----

Statement::Statement(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Statement::Statement(Statement&&) noexcept = default;
Statement& Statement::operator=(Statement&&) noexcept = default;
Statement::~Statement() {
    if (impl_ && impl_->stmt) sqlite3_finalize(impl_->stmt);
}

Statement& Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(impl_->stmt, index, value) != SQLITE_OK) throw DbError(errorOf(impl_->db, "bind"));
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    // SQLITE_TRANSIENT: SQLite copies the text, so `value` may die right after this call.
    if (sqlite3_bind_text(impl_->stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw DbError(errorOf(impl_->db, "bind"));
    return *this;
}

Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(impl_->stmt, index) != SQLITE_OK) throw DbError(errorOf(impl_->db, "bind"));
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(impl_->stmt);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(errorOf(impl_->db, "step"));
}

void Statement::reset() {
    sqlite3_reset(impl_->stmt);
    sqlite3_clear_bindings(impl_->stmt);
}

std::int64_t Statement::columnInt64(int index) const { return sqlite3_column_int64(impl_->stmt, index); }

std::string Statement::columnText(int index) const {
    const auto* text = sqlite3_column_text(impl_->stmt, index);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

// ---- Database ----

Database::Database(const std::filesystem::path& file) : impl_(std::make_unique<Impl>()) {
    // u8string keeps non-ASCII Windows paths intact (SQLite expects UTF-8).
    const auto u8 = file.u8string();
    const std::string path(u8.begin(), u8.end());
    if (sqlite3_open(path.c_str(), &impl_->db) != SQLITE_OK) {
        const std::string msg = errorOf(impl_->db, "cannot open database '" + path + "'");
        sqlite3_close(impl_->db);
        throw DbError(msg);
    }
    exec("PRAGMA foreign_keys = ON");  // off by default in SQLite, for historical reasons
}

Database::~Database() {
    if (impl_ && impl_->db) sqlite3_close(impl_->db);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError("exec failed: " + msg);
    }
}

Statement Database::prepare(const std::string& sql) {
    auto impl = std::make_unique<Statement::Impl>();
    impl->db = impl_->db;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &impl->stmt, nullptr) != SQLITE_OK)
        throw DbError(errorOf(impl_->db, "prepare '" + sql + "'"));
    return Statement(std::move(impl));
}

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(impl_->db); }

int Database::userVersion() {
    auto st = prepare("PRAGMA user_version");
    st.step();
    return static_cast<int>(st.columnInt64(0));
}

void Database::setUserVersion(int v) {
    exec("PRAGMA user_version = " + std::to_string(v));  // PRAGMAs don't accept bound parameters
}

// ---- Transaction ----

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN"); }

Transaction::~Transaction() {
    if (!done_) {
        try { db_.exec("ROLLBACK"); } catch (...) {}  // destructors must not throw
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

}  // namespace diskdive
