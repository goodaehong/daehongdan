#ifndef FLOORMAPPAGE_H
#define FLOORMAPPAGE_H

#include <QWidget>
#include <QVector>
#include <QImage>
#include "../core/FloorMapTypes.h"

class QLabel;
class QPushButton;
class QDialog;
class QTimer;
class FloorMapGridWidget;

// 메뉴 "평면도" 탭: 기본 화면은 변환된 대피도 + 전광판 클릭 시 경로 표시(운영용).
// 원본 업로드/전-후 비교는 자주 안 쓰는 관리 작업이라 "지도 재설정" 버튼 뒤에 숨겨둔다.
class FloorMapPage : public QWidget
{
    Q_OBJECT

public:
    explicit FloorMapPage(QWidget *parent = nullptr);

    // 평면도가 아직 한 번도 설정 안 됐으면 false — MainWindow가 메뉴 탭 배지 표시에 쓴다.
    bool isConfigured() const { return hasData; }

signals:
    // 설정 상태가 바뀔 때마다(최초 등록 등) 발생. MainWindow가 탭 배지 갱신에 사용.
    void configuredChanged(bool configured);

private:
    void openSetupPanel();
    // evac_map_tools가 실제 우리 평면도(map.png)를 변환해 낸 결과(비트맵+전광판+출구+경로)를
    // 그대로 미리 넣어둔 것 — 손으로 지어낸 예시가 아니라 진짜 계산 결과다. 다만 서버 query/push
    // 프로토콜이 아직 없어서 "지금 고른 이미지"에 반응하는 게 아니라 이 고정된 결과만 보여준다.
    // 서버 연동되면 이 함수 몸통이 서버 응답 파싱으로 통째로 교체된다.
    void applyPrecomputedConversion(const QImage &originalImage);
    void updateEmptyState();

    FloorMapGridWidget *gridWidget = nullptr;
    QLabel *emptyStateLabel = nullptr;
    QPushButton *resetButton = nullptr;
    // 지도 미등록 시 탭 진입 즉시 눈에 띄도록 페이지 상단에 표시하는 안내 배너.
    QWidget *notConfiguredBanner = nullptr;

    QDialog *setupDialog = nullptr;
    QLabel *originalPreviewLabel = nullptr;
    QLabel *convertedPreviewLabel = nullptr;
    QImage pendingOriginalImage;

    bool hasData = false;
};

#endif // FLOORMAPPAGE_H
