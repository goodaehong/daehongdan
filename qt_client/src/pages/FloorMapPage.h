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
    // 서버 연동 전까지 UI 확인용 예시 데이터를 만든다 — 실제 변환은 아님(버튼 문구로도 명시).
    void applyPlaceholderConversion(const QImage &originalImage);
    void updateEmptyState();

    FloorMapGridWidget *gridWidget = nullptr;
    QLabel *emptyStateLabel = nullptr;
    QPushButton *resetButton = nullptr;

    QDialog *setupDialog = nullptr;
    QLabel *originalPreviewLabel = nullptr;
    QLabel *convertedPreviewLabel = nullptr;
    QImage pendingOriginalImage;

    bool hasData = false;
};

#endif // FLOORMAPPAGE_H
