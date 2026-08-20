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
class QStackedWidget;
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

public slots:
    // query target=floor_map 응답(available=true) 또는 접속 직후 서버가 저장된 결과를 돌려줬을 때
    // MainWindow가 호출. 업로드 다이얼로그와 무관하게 그냥 화면에 반영만 한다.
    void applyServerData(int gridSize, const QVector<QVector<int>> &bitmap,
                          const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                          const QVector<FloorMapRoute> &routes);
    // set_floor_map 업로드 응답. 다이얼로그가 이미 닫혔어도(취소 등) 안전하게 무시한다.
    void onUploadResult(bool ok, const QString &reason, int gridSize, const QVector<QVector<int>> &bitmap,
                         const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                         const QVector<FloorMapRoute> &routes);
    void onUploadTimedOut();

signals:
    // 설정 상태가 바뀔 때마다(최초 등록 등) 발생. MainWindow가 탭 배지 갱신에 사용.
    void configuredChanged(bool configured);
    // "이 이미지로 적용" 클릭 시 발생. MainWindow가 받아서 serverLink->sendSetFloorMap()으로 넘긴다.
    void floorMapUploadRequested(const QByteArray &pngBytes);

private:
    void openSetupPanel();
    void updateEmptyState();
    void setDialogBusy(bool busy, const QString &statusText);

    FloorMapGridWidget *gridWidget = nullptr;
    QLabel *emptyStateLabel = nullptr;
    QPushButton *resetButton = nullptr;
    // 지도 미등록 시 탭 진입 즉시 눈에 띄도록 페이지 상단에 표시하는 안내 배너.
    QWidget *notConfiguredBanner = nullptr;

    QDialog *setupDialog = nullptr;
    QLabel *originalPreviewLabel = nullptr;
    QLabel *convertedPreviewLabel = nullptr;
    // 변환 성공 시 convertedPreviewLabel(상태 텍스트) 대신 이걸 보여준다 — 변환 전/후를
    // 같은 다이얼로그 안에서 직접 비교할 수 있게.
    QStackedWidget *convertedStack = nullptr;
    FloorMapGridWidget *convertedPreviewGrid = nullptr;
    QPushButton *pickButton = nullptr;
    QPushButton *applyButton = nullptr;
    // 변환 성공 후에만 보이는 닫기 버튼 — 자동으로 닫지 않고 결과를 확인한 뒤 직접 닫게 한다.
    QPushButton *closeButton = nullptr;
    QImage pendingOriginalImage;

    bool hasData = false;
};

#endif // FLOORMAPPAGE_H
