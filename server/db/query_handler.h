#pragma once
#include <string>
#include "Database.h"

// {"type":"query"} 한 줄을 받아 {"type":"query_result"} 한 줄을 돌려줌
// JSON 파싱·조립을 db/ 안에 두어, 조회 종류가 늘어도 qt_link·server_main은 안 바뀜
std::string handleQuery(Database& db, const std::string& line);