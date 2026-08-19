#ifndef HELPPAGE_H
#define HELPPAGE_H

#include <QWidget>

class QListWidget;
class QTextBrowser;
class QStackedWidget;
class QLabel;
class QTimer;

// 메뉴 "도움말" 탭: 좌측 주제 목록 + 우측 설명 + 그 아래 미리보기 카드. 신규/대체 인력이 화면 보고
// 바로 확인할 수 있도록 실제 UI 동작(색상 의미, 버튼별 기능, 탭별 사용법)을 그대로 옮겨 적는다.
// 미리보기 카드는 실제 위젯과 같은 색상 상수·문구를 코드로 재현한 것이라 UI가 바뀌면 같이 갱신해야 한다.
// (카메라 ROI/평면도처럼 동작 자체를 보여줘야 하는 항목은 자리만 잡아두고 실제 캡처 이미지로 나중에 교체한다.)
class HelpPage : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPage(QWidget *parent = nullptr);

private:
    void showTopic(int index);
    QWidget *buildEmergencyButtonPreview(QWidget *parent);

    QListWidget *topicList = nullptr;
    QTextBrowser *contentView = nullptr;
    QStackedWidget *previewStack = nullptr;

    // 위험 모드 미리보기의 "재시도" 라벨 — 실제 StatusPanel과 같은 600ms 주기로 깜빡인다.
    QLabel *previewRetryButton = nullptr;
    QTimer *retryBlinkTimer = nullptr;
    bool retryBlinkOn = false;
};

#endif // HELPPAGE_H
