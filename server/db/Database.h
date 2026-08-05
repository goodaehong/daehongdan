#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

// event_log 한 행. 조회 결과를 담아 Qt로 넘김 (컬럼과 1:1, detail 제외)  
struct EventRow {
    long id = 0, ts = 0;
    std::string zone, category, severity, cause, sensorCombo, source, response, admin;
    double gasPpm = 0.0, smokePpm = 0.0;
    std::string status;
    long durationMs = 0;
    std::string snapshotPath;
    long incidentId = 0;
};      

// sensor_log 조회 결과 한 점. bucketSec 구간 하나를 대표한다    
// 평균만 보내면 짧은 급상승이 희석돼 사라지므로 최댓값을 함께 보낸다
struct SensorPoint {
    long   t = 0;                    // 구간 시작 시각
    double gasAvg = 0, gasMax = 0;
    double smokeAvg = 0, smokeMax = 0;
};                                                             

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

    // ── 조회 ──                                                         
    // 기간 내 이벤트를 최신순으로. 필터(구역·심각도 등)는 Qt가 처리한다
    std::vector<EventRow> queryEvents(long from, long to, int limit);   
    
    // 기간 내 센서값을 bucketSec 단위로 묶어 평균·최댓값 반환     
    // bucketSec=1이면 원본 그대로 (avg == max)
    std::vector<SensorPoint> querySensors(long from, long to,
                                          const std::string& zone, int bucketSec);  


    ~Database() { close(); }

private:
    bool exec(const std::string& sql);   // 단순 SQL 실행 헬퍼 (CREATE 등)

    sqlite3* db_ = nullptr;   // SQLite 연결 핸들
    std::mutex mtx_;          // 여러 스레드가 insert해도 안전하게 직렬화
};