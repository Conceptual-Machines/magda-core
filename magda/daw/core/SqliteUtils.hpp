#pragma once

#include <sqlite3.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace magda::sqlite {

class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

inline std::string lastError(sqlite3* db) {
    const char* message = db ? sqlite3_errmsg(db) : "(no connection)";
    return message ? message : "(unknown SQLite error)";
}

inline void exec(sqlite3* db, const char* sql, const char* context = nullptr) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        std::string error = message ? message : lastError(db);
        sqlite3_free(message);
        if (context && *context)
            error = std::string(context) + ": " + error;
        throw Error(error);
    }
}

class Connection {
  public:
    Connection() = default;

    explicit Connection(const std::string& path, const std::string& context = "sqlite3_open") {
        open(path, context);
    }

    ~Connection() {
        close();
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            close();
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    void open(const std::string& path, const std::string& context = "sqlite3_open") {
        close();
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            const auto error = lastError(db_);
            close();
            throw Error(context + ": " + error);
        }
    }

    void close() noexcept {
        if (db_) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    [[nodiscard]] sqlite3* get() const noexcept {
        return db_;
    }

  private:
    sqlite3* db_ = nullptr;
};

class Statement {
  public:
    Statement(sqlite3* db, const char* sql, const char* context = nullptr) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            auto error = lastError(db_);
            if (context && *context)
                error = std::string(context) + ": " + error;
            throw Error(error);
        }
    }

    ~Statement() {
        sqlite3_finalize(stmt_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept
        : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}

    Statement& operator=(Statement&& other) noexcept {
        if (this != &other) {
            sqlite3_finalize(stmt_);
            db_ = std::exchange(other.db_, nullptr);
            stmt_ = std::exchange(other.stmt_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return stmt_;
    }

    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    void stepDone() {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
            throw Error(lastError(db_));
    }

  private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

enum class TransactionMode { Deferred, Immediate };

class Transaction {
  public:
    explicit Transaction(sqlite3* db, TransactionMode mode = TransactionMode::Deferred) : db_(db) {
        if (sqlite3_get_autocommit(db_)) {
            exec(db_, mode == TransactionMode::Immediate ? "BEGIN IMMEDIATE" : "BEGIN", "BEGIN");
            return;
        }

        static std::atomic<std::uint64_t> nextSavepoint{0};
        savepoint_ = "magda_nested_" + std::to_string(++nextSavepoint);
        exec(db_, ("SAVEPOINT " + savepoint_).c_str(), "SAVEPOINT");
    }

    ~Transaction() {
        if (finished_)
            return;
        try {
            if (savepoint_.empty()) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            } else {
                // Build both statements before running either: if the second
                // allocation throws, the savepoint must not be left half-released
                // (ROLLBACK TO already run, RELEASE skipped).
                const std::string rollbackTo = "ROLLBACK TO " + savepoint_;
                const std::string release = "RELEASE " + savepoint_;
                sqlite3_exec(db_, rollbackTo.c_str(), nullptr, nullptr, nullptr);
                sqlite3_exec(db_, release.c_str(), nullptr, nullptr, nullptr);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[Transaction] rollback failed: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "[Transaction] rollback failed: unknown exception\n");
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        if (finished_)
            return;
        if (savepoint_.empty())
            exec(db_, "COMMIT", "COMMIT");
        else
            exec(db_, ("RELEASE " + savepoint_).c_str(), "RELEASE");
        finished_ = true;
    }

  private:
    sqlite3* db_ = nullptr;
    std::string savepoint_;
    bool finished_ = false;
};

}  // namespace magda::sqlite
