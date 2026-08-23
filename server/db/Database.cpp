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
        " clip_path TEXT,"
        " detail TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_event_ts ON event_log(ts);"
        "CREATE INDEX IF NOT EXISTS idx_event_zone ON event_log(zone, ts);";

    if (!exec(sensorSql) || !exec(eventSql)) return false;

    // 이미 만들어진 DB에는 CREATE TABLE IF NOT EXISTS가 새 컬럼을 안 붙인다. 
    // 두 번째 실행부터는 "이미 있음"으로 실패하는 게 정상이라
    // 에러를 찍는 exec() 대신 raw 호출로 조용히 무시한다
    sqlite3_exec(db_, "ALTER TABLE event_log ADD COLUMN clip_path TEXT;",
                 nullptr, nullptr, nullptr);                                

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
void Database::resolveIncident(long incidentId, long durationMs) {   //  durationMs 인자 추가
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_ || incidentId <= 0) return;
    const char* sql =
        "UPDATE event_log SET status='해결됨', duration_ms=? "   
        "WHERE incident_id=? AND status='진행중';";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(st, 1, durationMs);       // 1번=duration
    sqlite3_bind_int64(st, 2, incidentId);       // 2번=incidentId  
    sqlite3_step(st);
    sqlite3_finalize(st);
}          

// ── 오탐 판정 ── 그 사태의 모든 행을 "오탐 처리됨"으로 바꾼다            
// 해제(resolve)까지 포함해 통째로 바꾼다 — 사건 자체가 오탐이었다는 뜻이라
// 일부만 오탐으로 남기면 목록에서 상태가 뒤섞인다
int Database::markFalseAlarm(long incidentId) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_ || incidentId <= 0) return 0;
    const char* sql = "UPDATE event_log SET status='오탐 처리됨' WHERE incident_id=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, incidentId);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db_);   // 실제로 바뀐 행 수
}                                                             

// ── DB에 남은 마지막 사태 번호 ──                                       
// 사태 번호는 메모리 카운터라 서버를 껐다 켜면 1부터 다시 나온다.
// 그러면 옛 로그와 번호가 겹쳐 해결 처리·클립 경로가 엉뚱한 행을 건드린다
long Database::maxIncidentId() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_) return 0;
    long v = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT IFNULL(MAX(incident_id),0) FROM event_log;",
                           -1, &st, nullptr) != SQLITE_OK) return 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}                                                                       

// ── 클립 저장 완료 → 그 클립이 담은 시간대의 이벤트 행에 mp4 경로 기록 ──
// 한 사태에서도 클립이 두 번 만들어질 수 있다(경고 → 저장 완료 → 다시 경고).
// incident_id 만으로 UPDATE 하면 뒤 클립이 앞 클립 경로를 덮어쓴다.
// 클립이 실제로 담은 [ts-3초, ts+10초] 창 + 아직 비어 있는 행으로 좁힌다
void Database::updateClipPath(long incidentId, long clipTs,
                              const std::string& clipPath) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!db_ || incidentId <= 0 || clipPath.empty()) return;
    const char* sql =
        "UPDATE event_log SET clip_path=? "
        "WHERE incident_id=? AND ts BETWEEN ? AND ? "
        "  AND (clip_path IS NULL OR clip_path='');";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text (st, 1, clipPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, incidentId);
    sqlite3_bind_int64(st, 3, clipTs - 3);    // 클립 앞부분(직전 3초)
    sqlite3_bind_int64(st, 4, clipTs + 10);   // 클립 뒷부분(이후 10초)
    sqlite3_step(st);
    sqlite3_finalize(st);
}                                                              


// ── event_log 조회 ──
std::vector<EventRow> Database::queryEvents(long from, long to, int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<EventRow> rows;
    if (!db_) return rows;

    const char* sql =
        "SELECT id,ts,zone,category,severity,cause,sensor_combo,source,response,admin,"
        " gas_ppm,smoke_ppm,status,duration_ms,snapshot_path,incident_id,clip_path"  
        " FROM event_log WHERE ts BETWEEN ? AND ? ORDER BY ts DESC LIMIT ?;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "[DB] queryEvents prepare 실패: " << sqlite3_errmsg(db_) << "\n";
        return rows;
    }
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);
    sqlite3_bind_int  (st, 3, limit);

    // NULL이면 빈 문자열로 (Qt가 파싱하다 깨지지 않게)
    auto col = [&](int i) -> std::string {
        const unsigned char* p = sqlite3_column_text(st, i);
        return p ? reinterpret_cast<const char*>(p) : "";
    };

    while (sqlite3_step(st) == SQLITE_ROW) {
        EventRow r;
        r.id           = sqlite3_column_int64(st, 0);
        r.ts           = sqlite3_column_int64(st, 1);
        r.zone         = col(2);
        r.category     = col(3);
        r.severity     = col(4);
        r.cause        = col(5);
        r.sensorCombo  = col(6);
        r.source       = col(7);
        r.response     = col(8);
        r.admin        = col(9);
        r.gasPpm       = sqlite3_column_double(st, 10);
        r.smokePpm     = sqlite3_column_double(st, 11);
        r.status       = col(12);
        r.durationMs   = sqlite3_column_int64(st, 13);
        r.snapshotPath = col(14);
        r.incidentId   = sqlite3_column_int64(st, 15);
        r.clipPath     = col(16);     
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return rows;
}

// ── sensor_log 조회 (구간 집계) ──
std::vector<SensorPoint> Database::querySensors(long from, long to,
                                                const std::string& zone, int bucketSec) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<SensorPoint> pts;
    if (!db_ || bucketSec <= 0) return pts;

    // (ts/b)*b = 구간 시작 시각. GROUP BY ts/b 로 같은 구간끼리 묶음
    const char* sql =
        "SELECT (ts/?)*? AS t,"
        " AVG(gas_ppm), MAX(gas_ppm), AVG(smoke_ppm), MAX(smoke_ppm)"
        " FROM sensor_log WHERE zone=? AND ts BETWEEN ? AND ?"
        " GROUP BY ts/? ORDER BY t;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "[DB] querySensors prepare 실패: " << sqlite3_errmsg(db_) << "\n";
        return pts;
    }
    sqlite3_bind_int  (st, 1, bucketSec);
    sqlite3_bind_int  (st, 2, bucketSec);
    sqlite3_bind_text (st, 3, zone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, from);
    sqlite3_bind_int64(st, 5, to);
    sqlite3_bind_int  (st, 6, bucketSec);

    while (sqlite3_step(st) == SQLITE_ROW) {
        SensorPoint p;
        p.t        = sqlite3_column_int64 (st, 0);
        p.gasAvg   = sqlite3_column_double(st, 1);
        p.gasMax   = sqlite3_column_double(st, 2);
        p.smokeAvg = sqlite3_column_double(st, 3);
        p.smokeMax = sqlite3_column_double(st, 4);
        pts.push_back(p);
    }
    sqlite3_finalize(st);
    return pts;
}