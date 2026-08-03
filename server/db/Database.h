#pragma once

#include <string>
#include <mutex>
#include <sqlite3.h>

// SQLite DB 공용 클래스. sensor_log(대홍) + event_log(재환) 둘 다 이 클래스로 접근.
// 서버 시작 시 open() 1번 → 도는 내내 insert 재사용 → 종료 시 close().
class Database {
public:
    // DB 파일 열기 + 테이블 2개 생성(없으면). 성공 true.
    bool open(const std::string& path);
    void close();

    // ── sensor_log: 센서값 시계열 (1초마다) ──
    void insertSensor(long ts, const std::string& zone,
                      double temp, double humidity,
                      double gasPpm, double smokePpm,
                      double flameVal,
                      const std::string& state);

    // ── event_log: 사건 기록 (감지·대응·수동제어 발생 시) ──
    // 안 쓰는 인자는 기본값으로 생략 가능 (지금 통합 전이라 골격만)
    void insertEvent(long ts, const std::string& zone,
                     const std::string& category,
                     const std::string& severity = "",
                     const std::string& cause = "",
                     const std::string& sensorCombo = "",
                     const std::string& source = "",
                     const std::string& response = "",
                     const std::string& admin = "",
                     double gasPpm = 0.0, double smokePpm = 0.0,
                     const std::string& status = "",
                     long durationMs = 0,
                     const std::string& snapshotPath = "",
                     long incidentId = 0,
                     const std::string& detail = "");

    void resolveIncident(long incidentId, long durationMs);  

    ~Database() { close(); }

private:
    bool exec(const std::string& sql);   // 단순 SQL 실행 헬퍼 (CREATE 등)

    sqlite3* db_ = nullptr;   // SQLite 연결 핸들
    std::mutex mtx_;          // 여러 스레드가 insert해도 안전하게 직렬화
};