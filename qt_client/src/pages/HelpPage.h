#ifndef HELPPAGE_H
#define HELPPAGE_H

#include <QWidget>
#include <QVector>
#include "../core/FloorMapTypes.h"

class QListWidget;
class QTextBrowser;
class QStackedWidget;
class QLabel;
class QTimer;
class FloorMapGridWidget;

// 메뉴 "도움말" 탭: 좌측 주제 목록 + 우측 설명 + 그 아래 미리보기 카드. 신규/대체 인력이 화면 보고
// 바로 확인할 수 있도록 실제 UI 동작(색상 의미, 버튼별 기능, 탭별 사용법)을 그대로 옮겨 적는다.
// 미리보기 카드는 대부분 실제 위젯과 같은 색상 상수·문구를 코드로 재현한 것이라 UI가 바뀌면 같이
// 갱신해야 한다. 이벤트로그/그래프는 실제 화면 캡처 이미지(resources/help/, 2026-08-21)를 그대로
// 보여주고, 평면도는 캡처로는 "클릭하면 경로가 뜬다"를 못 보여줘서 실제 위젯(FloorMapGridWidget)을
// 예시 데이터로 띄워 직접 눌러볼 수 있게 했다.
class HelpPage : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPage(QWidget *parent = nullptr);

    // MainWindow가 서버로부터 실제 평면도 데이터를 받을 때마다(FloorMapPage와 동일한 데이터) 호출.
    // 평면도 탭 미리보기 예시 격자를 지금 실제 등록된 평면도 모양으로 갱신한다.
    void updateFloorMapExample(int gridSize, const QVector<QVector<int>> &bitmap,
                                const QVector<FloorMapMarker> &displays,
                                const QVector<FloorMapMarker> &exits,
                                const QVector<FloorMapRoute> &routes);

private:
    void showTopic(int index);
    QWidget *buildEmergencyButtonPreview(QWidget *parent);
    QWidget *buildFloorMapPreview(QWidget *parent);
    // 실제 평면도가 아직 등록 전이거나 못 받아온 상태일 때 보여줄 기본 예시 격자.
    void applyFloorMapSampleData();

    QListWidget *topicList = nullptr;
    QTextBrowser *contentView = nullptr;
    QStackedWidget *previewStack = nullptr;

    // 위험 모드 미리보기의 "재시도" 라벨 — 실제 StatusPanel과 같은 600ms 주기로 깜빡인다.
    QLabel *previewRetryButton = nullptr;
    QTimer *retryBlinkTimer = nullptr;
    bool retryBlinkOn = false;

    // 평면도 탭 미리보기 안의 실제 위젯 재사용 인스턴스 — 클릭하면 실제 화면과 동일하게 대피경로가 뜬다.
    FloorMapGridWidget *floorMapPreviewGrid = nullptr;
};

#endif // HELPPAGE_H
