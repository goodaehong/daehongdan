#include "Database.h"
#include <iostream>

// ── DB 파일 열기 + 테이블 생성 ──
bool Database::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[DB] 열기 실패: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }

    // sensor_log: 센서값 시계열
    const char* sensorSql =
        "CREATE TABLE IF NOT EXISTS sensor_log ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " ts INTEGER NOT NULL,"
        " zone TEXT NOT NULL,"
        " temp REAL, humidity REAL,"
        " gas_ppm REAL, smoke_ppm REAL,"
        " flame_val REAL," 
        " state TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_sensor_ts ON sensor_log(ts);"
        "CREATE INDEX IF NOT EXISTS idx_sensor_zone ON sensor_log(zone, ts);";

    // event_log: 사건 기록
    const char* eventSql =
        "CREATE TABLE IF NOT EXISTS event_log ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " ts INTEGER NOT NULL,"
        " zone TEXT NOT NULL,"
        " category TEXT NOT NULL,"
        " severity TEXT, cause TEXT, sensor_combo TEXT,"
        " source TEXT, response TEXT, admin TEXT,"
        " gas_ppm REAL, smoke_ppm REAL,"
        " status TEXT, duration_ms INTEGER,"
        " snapshot_path TEXT, incident_id INTEGER,"
        " detail TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_event_ts ON event_log(ts);"
        "CREATE INDEX IF NOT EXISTS idx_event_zone ON event_log(zone, ts);";

    if (!exec(sensorSql) || !exec(eventSql)) return false;

    std::cout << "[DB] 열기 성공: " << path << "\n";
    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

// ── 단순 SQL 실행 (CREATE 등). 호출자가 이미 mtx_ 잠근 상태에서만 사용 ──
bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[DB] SQL 실패: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ── sensor_log INSERT ──
void Database::insertSensor(long ts, const std::string& zone,
                            double temp, double humidity,
                            double gasPpm, double smokePpm,
                            double flameVal,
                            const std::string& state) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_) return;

    const char* sql =
        "INSERT INTO sensor_log(ts,zone,temp,humidity,gas_ppm,smoke_ppm,flame_val,state)"
        " VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "[DB] sensor prepare 실패: " << sqlite3_errmsg(db_) << "\n";
        return;
    }
    sqlite3_bind_int64 (st, 1, ts);
    sqlite3_bind_text  (st, 2, zone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 3, temp);
    sqlite3_bind_double(st, 4, humidity);
    sqlite3_bind_double(st, 5, gasPpm);
    sqlite3_bind_double(st, 6, smokePpm);
    sqlite3_bind_double(st, 7, flameVal);
    sqlite3_bind_text  (st, 8, state.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE)
        std::cerr << "[DB] sensor insert 실패: " << sqlite3_errmsg(db_) << "\n";
    sqlite3_finalize(st);
}

// ── event_log INSERT ──
void Database::insertEvent(long ts, const std::string& zone,
                           const std::string& category,
                           const std::string& severity,
                           const std::string& cause,
                           const std::string& sensorCombo,
                           const std::string& source,
                           const std::string& response,
                           const std::string& admin,
                           double gasPpm, double smokePpm,
                           const std::string& status,
                           long durationMs,
                           const std::string& snapshotPath,
                           long incidentId,
                           const std::string& detail) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_) return;

    const char* sql =
        "INSERT INTO event_log(ts,zone,category,severity,cause,sensor_combo,"
        "source,response,admin,gas_ppm,smoke_ppm,status,duration_ms,"
        "snapshot_path,incident_id,detail)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "[DB] event prepare 실패: " << sqlite3_errmsg(db_) << "\n";
        return;
    }
    sqlite3_bind_int64 (st, 1, ts);
    sqlite3_bind_text  (st, 2, zone.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 3, category.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 4, severity.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 5, cause.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 6, sensorCombo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 7, source.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 8, response.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 9, admin.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 10, gasPpm);
    sqlite3_bind_double(st, 11, smokePpm);
    sqlite3_bind_text  (st, 12, status.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64 (st, 13, durationMs);
    sqlite3_bind_text  (st, 14, snapshotPath.c_str(),-1, SQLITE_TRANSIENT);
    sqlite3_bind_int64 (st, 15, incidentId);
    sqlite3_bind_text  (st, 16, detail.c_str(),     -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE)
        std::cerr << "[DB] event insert 실패: " << sqlite3_errmsg(db_) << "\n";
    sqlite3_finalize(st);
}

// ── 한 사태(incident)의 '진행중' 이벤트를 '해결됨'으로 일괄 변경 ── 
void Database::resolveIncident(long incidentId) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_ || incidentId <= 0) return;
    const char* sql =
        "UPDATE event_log SET status='해결됨' "
        "WHERE incident_id=? AND status='진행중';";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(st, 1, incidentId);
    sqlite3_step(st);
    sqlite3_finalize(st);
}                                                              