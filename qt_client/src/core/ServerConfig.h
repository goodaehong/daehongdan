#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>

// 서버 주소 단일 출처. LoginPage(연결 사전 확인)와 MainWindow(실제 데이터 소켓)가 각자 상수를
// 따로 들고 있다가 한쪽만 갱신되면서 어긋난 적이 있어(로그아웃 후 재로그인 시 "서버 연결 실패"
// 오탐 원인) 여기 하나로 합친다.
namespace ServerConfig {
inline const QString kServerHost = "172.20.32.41";
inline const quint16 kServerPort = 9999;
}

#endif // SERVERCONFIG_H
