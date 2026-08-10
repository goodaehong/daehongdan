#ifndef DANGERGLOWOVERLAY_H
#define DANGERGLOWOVERLAY_H

#include <QWidget>

// 위험(Danger) 상태 동안 메인 윈도우 가장자리에만 은은하게 번지는 빨간 글로우.
// video 위젯(native HWND)과 같은 부모 트리에 두면 Windows 컴포지팅이 깨지므로(DetectionOverlay 참고),
// 독립된 최상위 창으로 만들고 대상 창 위치/크기를 추적한다.
// 대신 Qt의 WA_TransparentForMouseEvents는 top-level 창에서는 보장되지 않아서,
// 네이티브 창이 생성된 뒤 Win32 WS_EX_TRANSPARENT를 직접 걸어 클릭이 100% 통과되게 한다.
class DangerGlowOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DangerGlowOverlay(QWidget *followTarget);

    void setActive(bool active);
    void syncGeometry();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QWidget *target;
    bool active = false;
    double intensity = 0.35; // 0~1, 숨쉬듯 오르내림
    double direction = 1.0;
};

#endif // DANGERGLOWOVERLAY_H
