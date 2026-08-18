#include "FloorMapPage.h"
#include "../widgets/FloorMapGridWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QFrame>
#include <QStringList>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

// 서버가 base64 전송 줄 길이를 8MB로 제한할 예정(대홍 회신) — base64는 원본의 약 1.33배이므로
// 원본 기준 6MB를 상한으로 둔다.
constexpr qint64 kMaxUploadBytes = 6LL * 1024 * 1024;

// 실제 우리 평면도(map.png)를 태호님의 evac_map_tools(EvacPlanner)로 변환해서 나온 진짜 결과.
// evac_bitmap.txt / evac_routes.txt 출력을 그대로 옮겨왔다 — 서버 연동 전이라 하드코딩만 됐을 뿐,
// 격자·전광판·출구·경로 값 자체는 손으로 지어낸 예시가 아니다.
constexpr int kGridSize = 62;

// evac_bitmap.txt 62줄을 그대로 옮겨온 것 — 한 줄 = 공백으로 구분된 0/1 62개.
QVector<QVector<int>> realFloorMapBitmap()
{
    static const char *kRows[kGridSize] = {
        "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 0 0 1 1 1 1 0 1 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 1 1 1 1 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 1 1 1 1 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1 0 0 1 1 1 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 0 0 1 1 1 0 0 1 1 0 0 1 1 1 0 0 1 1 0 0 0 1 1 0 0 1 1 0 0 1 1 1 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 1 0 0 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 1",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 1 1 1 1 0 1 1 1 1 0 0 1 1 1 1 0 0 0 0 1 1 1 1 0 0 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 1 1 1 1 0 1 1 1 1 0 0 1 1 1 1 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 1 1 1 0 0 0 1 1 1 0 0 1 1 1 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 0 0 0 1 1 1 1 1 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 0 0 1 1 1 1 1 1 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
        "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 1 1 1 1 1",
    };

    QVector<QVector<int>> bmp(kGridSize, QVector<int>(kGridSize, 0));
    for (int y = 0; y < kGridSize; ++y) {
        const QStringList tokens = QString::fromLatin1(kRows[y]).split(' ', Qt::SkipEmptyParts);
        for (int x = 0; x < kGridSize && x < tokens.size(); ++x)
            bmp[y][x] = tokens[x].toInt();
    }
    return bmp;
}
}

FloorMapPage::FloorMapPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel("평면도", this);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    headerRow->addWidget(title);
    headerRow->addStretch();

    // 자주 안 쓰는 관리 작업(등록/재설정)이라 눈에 띄되 주 동선을 가리지 않는 자리에 둔다.
    resetButton = new QPushButton("지도 재설정", this);
    resetButton->setCursor(Qt::PointingHandCursor);
    resetButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:8px 16px; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kCardBg, kTextSecondary, kCardBorder, kAccent));
    connect(resetButton, &QPushButton::clicked, this, &FloorMapPage::openSetupPanel);
    headerRow->addWidget(resetButton);
    layout->addLayout(headerRow);

    // 지도 미등록 상태를 탭 배지("❗ 평면도")뿐 아니라 탭에 들어오는 즉시도 알 수 있도록
    // 페이지 최상단에 배너로 한 번 더 표시한다.
    notConfiguredBanner = new QWidget(this);
    notConfiguredBanner->setStyleSheet(
        "background-color:#3a2e12; border:1px solid #6b5416; border-radius:8px;");
    auto *bannerLayout = new QHBoxLayout(notConfiguredBanner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    auto *bannerLabel = new QLabel(
        "❗ 아직 평면도가 등록되지 않았습니다. \"지도 재설정\"에서 원본 이미지를 등록해주세요.",
        notConfiguredBanner);
    bannerLabel->setStyleSheet("color:#fbbf24; font-size:14px; border:none;");
    bannerLayout->addWidget(bannerLabel);
    bannerLayout->addStretch();
    auto *bannerButton = new QPushButton("지금 등록", notConfiguredBanner);
    bannerButton->setCursor(Qt::PointingHandCursor);
    bannerButton->setStyleSheet(
        "QPushButton { background-color:#fbbf24; color:#1a1400; border:none; border-radius:6px; padding:6px 14px; font-weight:bold; }"
        "QPushButton:hover { background-color:#fcd34d; }");
    connect(bannerButton, &QPushButton::clicked, this, &FloorMapPage::openSetupPanel);
    bannerLayout->addWidget(bannerButton);
    layout->addWidget(notConfiguredBanner);

    auto *card = new QFrame(this);
    card->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:10px;").arg(kCardBg, kCardBorder));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);

    gridWidget = new FloorMapGridWidget(card);
    gridWidget->setVisible(false);
    cardLayout->addWidget(gridWidget);

    emptyStateLabel = new QLabel(
        "아직 등록된 평면도가 없습니다.\n\n"
        "우측 상단 \"지도 재설정\"에서 원본 평면도 이미지를 등록하면\n"
        "여기에 대피도가 표시됩니다.", card);
    emptyStateLabel->setAlignment(Qt::AlignCenter);
    emptyStateLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    cardLayout->addWidget(emptyStateLabel);

    layout->addWidget(card, 1);

    auto *hint = new QLabel("전광판(주황) 위치를 클릭하면 해당 전광판의 대피 경로가 표시됩니다.", this);
    hint->setStyleSheet(QString("color:%1; font-size:12px;").arg(kTextSecondary));
    layout->addWidget(hint);

    updateEmptyState();
}

void FloorMapPage::updateEmptyState()
{
    gridWidget->setVisible(hasData);
    emptyStateLabel->setVisible(!hasData);
    notConfiguredBanner->setVisible(!hasData);
}

void FloorMapPage::openSetupPanel()
{
    setupDialog = new QDialog(this);
    setupDialog->setWindowTitle("평면도 재설정");
    setupDialog->setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    setupDialog->setMinimumWidth(640);
    auto *dialogLayout = new QVBoxLayout(setupDialog);
    dialogLayout->setContentsMargins(24, 24, 24, 24);
    dialogLayout->setSpacing(14);

    auto *desc = new QLabel(
        "원본 평면도 이미지(PNG)를 선택하면 변환 결과를 미리 확인할 수 있습니다.\n"
        "지금 표시되는 대피경로는 실제 우리 평면도를 evac_map_tools로 변환한 진짜 결과입니다 —\n"
        "다만 서버 연동이 아직 안 돼 있어 고른 이미지와 무관하게 이 고정된 결과가 적용됩니다.\n"
        "서버 연동되면 고른 이미지 기준으로 그때그때 새로 계산된 결과가 표시됩니다.",
        setupDialog);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    dialogLayout->addWidget(desc);

    auto *pickBtn = new QPushButton("원본 이미지 선택...", setupDialog);
    pickBtn->setCursor(Qt::PointingHandCursor);
    pickBtn->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; border:none; border-radius:6px; padding:8px 16px; }"
        "QPushButton:hover { background-color:#7c6ce8; }").arg(kAccent));
    dialogLayout->addWidget(pickBtn);

    // 변환 전/후를 나란히 두고 비교 — "변환이 잘 됐는지" 확인이 이 패널의 목적.
    auto *previewRow = new QHBoxLayout;
    auto makePreviewCol = [&](const QString &label) {
        auto *col = new QVBoxLayout;
        auto *lbl = new QLabel(label, setupDialog);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(kTextSecondary));
        col->addWidget(lbl);
        auto *preview = new QLabel(setupDialog);
        preview->setFixedSize(280, 280);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet(QString("background-color:#0d0d16; border:1px solid %1; border-radius:6px; color:%2;").arg(kCardBorder, kTextSecondary));
        preview->setText("(이미지 없음)");
        col->addWidget(preview);
        previewRow->addLayout(col);
        return preview;
    };
    originalPreviewLabel = makePreviewCol("변환 전 (원본)");
    convertedPreviewLabel = makePreviewCol("변환 후 (대피도 미리보기)");
    dialogLayout->addLayout(previewRow);

    auto *applyBtn = new QPushButton("이 변환 결과 적용", setupDialog);
    applyBtn->setEnabled(false);
    applyBtn->setCursor(Qt::PointingHandCursor);
    applyBtn->setStyleSheet(QString(
        "QPushButton { background-color:#232333; color:%1; border:1px solid %2; border-radius:6px; padding:8px 16px; }"
        "QPushButton:enabled:hover { border:1px solid %3; color:%3; }"
        "QPushButton:disabled { color:%4; }").arg(kTextPrimary, kCardBorder, kAccent, kTextSecondary));
    dialogLayout->addWidget(applyBtn);

    connect(pickBtn, &QPushButton::clicked, this, [this, applyBtn]() {
        const QString path = QFileDialog::getOpenFileName(this, "원본 평면도 이미지 선택", QString(), "이미지 파일 (*.png *.jpg *.jpeg)");
        if (path.isEmpty())
            return;

        const qint64 fileSize = QFileInfo(path).size();
        if (fileSize > kMaxUploadBytes) {
            QMessageBox::warning(this, "이미지 용량 초과",
                QString("선택한 이미지가 %1MB로 상한(6MB)을 초과합니다.\n"
                        "서버 전송 시 base64로 변환되면 더 커지므로 6MB 이하 이미지를 선택해주세요.")
                    .arg(fileSize / 1024.0 / 1024.0, 0, 'f', 1));
            return;
        }

        pendingOriginalImage.load(path);
        if (pendingOriginalImage.isNull()) {
            originalPreviewLabel->setText("이미지를 열 수 없습니다");
            applyBtn->setEnabled(false);
            return;
        }
        originalPreviewLabel->setPixmap(QPixmap::fromImage(pendingOriginalImage)
            .scaled(originalPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        // 고른 이미지를 실제로 변환하는 게 아니라, 우리 평면도의 기존 계산 결과를 그대로 보여준다 —
        // 서버 연동 전까지는 정직하게 "지금 고른 이 이미지 기준 결과가 아니다"를 화면에도 남겨둔다.
        convertedPreviewLabel->setText("(고른 이미지가 아니라\n우리 평면도의 기존 변환 결과가\n적용됩니다 — 서버 연동 전)");
        convertedPreviewLabel->setPixmap(QPixmap());
        applyBtn->setEnabled(true);
    });

    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        applyPrecomputedConversion(pendingOriginalImage);
        setupDialog->accept();
    });

    setupDialog->exec();
    setupDialog->deleteLater();
    setupDialog = nullptr;
}

void FloorMapPage::applyPrecomputedConversion(const QImage &originalImage)
{
    Q_UNUSED(originalImage); // 서버 연동 전까지는 골라도 무시 — 대신 우리 평면도의 기존 결과를 쓴다

    // 전광판 6개 — evac_routes.txt의 각 Start ID 그룹 첫 waypoint(=전광판 자기 위치)에서 그대로 옮김.
    QVector<FloorMapMarker> displays = {
        { 1, 60, 27 },
        { 2, 38, 60 },
        { 3, 32, 11 },
        { 4, 23, 11 },
        { 5, 20, 60 },
        { 6, 1, 25 },
    };
    // 출구 4개 — 모든 Start의 같은 Exit ID 경로가 도착하는 좌표(경계선 위)로 확인.
    QVector<FloorMapMarker> exits = {
        { 1, 61, 48 },
        { 2, 48, 0 },
        { 3, 16, 61 },
        { 4, 0, 22 },
    };
    // evac_routes.txt 24개 경로 그대로 — waypoint는 (y,x)로 적혀 있어 QPoint(x,y)로 뒤집어 옮김.
    QVector<FloorMapRoute> routes = {
        { 1, 1, { QPoint(27, 60), QPoint(27, 50), QPoint(38, 50), QPoint(38, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 1, 2, { QPoint(27, 60), QPoint(27, 48), QPoint(0, 48) } },
        { 1, 3, { QPoint(27, 60), QPoint(27, 48), QPoint(33, 48), QPoint(33, 16), QPoint(61, 16) } },
        { 1, 4, { QPoint(27, 60), QPoint(27, 48), QPoint(24, 48), QPoint(24, 46), QPoint(23, 46),
                  QPoint(23, 1), QPoint(22, 1), QPoint(22, 0) } },
        { 2, 1, { QPoint(60, 38), QPoint(60, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 2, 2, { QPoint(60, 38), QPoint(60, 48), QPoint(0, 48) } },
        { 2, 3, { QPoint(60, 38), QPoint(60, 16), QPoint(61, 16) } },
        { 2, 4, { QPoint(60, 38), QPoint(60, 13), QPoint(42, 13), QPoint(42, 10), QPoint(31, 10),
                  QPoint(31, 1), QPoint(22, 1), QPoint(22, 0) } },
        { 3, 1, { QPoint(11, 32), QPoint(11, 36), QPoint(13, 36), QPoint(13, 50), QPoint(38, 50),
                  QPoint(38, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 3, 2, { QPoint(11, 32), QPoint(11, 36), QPoint(13, 36), QPoint(13, 48), QPoint(0, 48) } },
        { 3, 3, { QPoint(11, 32), QPoint(11, 35), QPoint(13, 35), QPoint(13, 16), QPoint(61, 16) } },
        { 3, 4, { QPoint(11, 32), QPoint(11, 35), QPoint(13, 35), QPoint(13, 15), QPoint(15, 15),
                  QPoint(15, 13), QPoint(17, 13), QPoint(17, 1), QPoint(22, 1), QPoint(22, 0) } },
        { 4, 1, { QPoint(11, 23), QPoint(11, 26), QPoint(13, 26), QPoint(13, 50), QPoint(38, 50),
                  QPoint(38, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 4, 2, { QPoint(11, 23), QPoint(11, 26), QPoint(13, 26), QPoint(13, 48), QPoint(0, 48) } },
        { 4, 3, { QPoint(11, 23), QPoint(11, 25), QPoint(13, 25), QPoint(13, 16), QPoint(61, 16) } },
        { 4, 4, { QPoint(11, 23), QPoint(11, 25), QPoint(13, 25), QPoint(13, 15), QPoint(15, 15),
                  QPoint(15, 13), QPoint(17, 13), QPoint(17, 1), QPoint(22, 1), QPoint(22, 0) } },
        { 5, 1, { QPoint(60, 20), QPoint(60, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 5, 2, { QPoint(60, 20), QPoint(60, 48), QPoint(0, 48) } },
        { 5, 3, { QPoint(60, 20), QPoint(60, 16), QPoint(61, 16) } },
        { 5, 4, { QPoint(60, 20), QPoint(60, 13), QPoint(42, 13), QPoint(42, 10), QPoint(31, 10),
                  QPoint(31, 1), QPoint(22, 1), QPoint(22, 0) } },
        { 6, 1, { QPoint(25, 1), QPoint(25, 21), QPoint(33, 21), QPoint(33, 50), QPoint(38, 50),
                  QPoint(38, 60), QPoint(48, 60), QPoint(48, 61) } },
        { 6, 2, { QPoint(25, 1), QPoint(25, 21), QPoint(24, 21), QPoint(24, 43), QPoint(23, 43),
                  QPoint(23, 48), QPoint(0, 48) } },
        { 6, 3, { QPoint(25, 1), QPoint(25, 16), QPoint(61, 16) } },
        { 6, 4, { QPoint(25, 1), QPoint(22, 1), QPoint(22, 0) } },
    };

    gridWidget->setMapData(kGridSize, realFloorMapBitmap(), displays, exits, routes);

    const bool wasConfigured = hasData;
    hasData = true;
    updateEmptyState();
    if (!wasConfigured)
        emit configuredChanged(true);
}
