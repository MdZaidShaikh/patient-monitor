#pragma once
#include "../common/protocol.h"
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>

class Database {
public:
    explicit Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw std::runtime_error("Failed to open DB: " + std::string(sqlite3_errmsg(db_)));
        }
        
        try {
            // WAL mode: allows one writer + many readers concurrently, which is a much
            // better fit here than the default rollback journal.
            exec("PRAGMA journal_mode=WAL;");
            exec(R"(
                CREATE TABLE IF NOT EXISTS readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    patient_id INTEGER NOT NULL,
                    timestamp INTEGER NOT NULL,
                    heart_rate REAL,
                    systolic_bp REAL,
                    diastolic_bp REAL,
                    spo2 REAL
                );
            )");
            exec(R"(
                CREATE TABLE IF NOT EXISTS alerts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    patient_id INTEGER NOT NULL,
                    timestamp INTEGER NOT NULL,
                    vital TEXT,
                    message TEXT,
                    value REAL
                );
            )");
            exec("CREATE INDEX IF NOT EXISTS idx_readings_patient ON readings(patient_id);");
    
            // Prepare the insert statement once, reuse it (much faster than
            // reparsing SQL on every insert).
            const char* insert_sql =
                "INSERT INTO readings (patient_id, timestamp, heart_rate, systolic_bp, diastolic_bp, spo2) "
                "VALUES (?, ?, ?, ?, ?, ?);";
            sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr);
    
            const char* alert_sql =
                "INSERT INTO alerts (patient_id, timestamp, vital, message, value) VALUES (?, ?, ?, ?, ?);";
            sqlite3_prepare_v2(db_, alert_sql, -1, &alert_stmt_, nullptr);
        } catch (...) {
            if (insert_stmt_) sqlite3_finalize(insert_stmt_);
            if (alert_stmt_) sqlite3_finalize(alert_stmt_);
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw;
        }
    }

    ~Database() {
        if (insert_stmt_) sqlite3_finalize(insert_stmt_);
        if (alert_stmt_) sqlite3_finalize(alert_stmt_);
        if (db_) sqlite3_close(db_);
    }

    // Called by worker threads. Mutex serializes writes -- SQLite allows only
    // one writer at a time even in WAL mode, so we make that explicit rather
    // than relying on SQLITE_BUSY retry loops.
    void insert_reading(const ReadingMessage& r) {
        insert_readings(std::vector<ReadingMessage>{r});
    }

    void insert_readings(const std::vector<ReadingMessage>& readings) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (readings.empty()) return;

        exec("BEGIN IMMEDIATE TRANSACTION;");
        for (const auto& r : readings) {
            sqlite3_reset(insert_stmt_);
            sqlite3_clear_bindings(insert_stmt_);
            sqlite3_bind_int(insert_stmt_, 1, r.patient_id);
            sqlite3_bind_int(insert_stmt_, 2, r.timestamp);
            sqlite3_bind_double(insert_stmt_, 3, r.heart_rate);
            sqlite3_bind_double(insert_stmt_, 4, r.systolic_bp);
            sqlite3_bind_double(insert_stmt_, 5, r.diastolic_bp);
            sqlite3_bind_double(insert_stmt_, 6, r.spo2);
            if (sqlite3_step(insert_stmt_) != SQLITE_DONE) {
                fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(db_));
                exec("ROLLBACK;");
                return;
            }
        }
        exec("COMMIT;");
    }

    void insert_alert(uint32_t patient_id, uint32_t timestamp, const std::string& vital,
                       const std::string& message, float value) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        sqlite3_reset(alert_stmt_);
        sqlite3_clear_bindings(alert_stmt_);
        sqlite3_bind_int(alert_stmt_, 1, patient_id);
        sqlite3_bind_int(alert_stmt_, 2, timestamp);
        sqlite3_bind_text(alert_stmt_, 3, vital.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(alert_stmt_, 4, message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(alert_stmt_, 5, value);
        if (sqlite3_step(alert_stmt_) != SQLITE_DONE) {
            fprintf(stderr, "Alert insert failed: %s\n", sqlite3_errmsg(db_));
        }
    }

private:
    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("SQL error: " + msg);
        }
    }

    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_stmt_ = nullptr;
    sqlite3_stmt* alert_stmt_ = nullptr;
    std::mutex write_mutex_;
};
