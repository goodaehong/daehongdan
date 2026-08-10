#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>
#include <QVector>
#include "../core/ZoneTypes.h"
#include "../core/DetectionTypes.h"

class StatusPanel;
class VideoWidget;
class StreamReceiver;

// 모니터링 화면: 좌측 StatusPanel + 우측 2x2 카메라 그리드(VideoWidget x4).
class MonitorPage : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPage(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    // MediaMTX 재배포 서버 주소 하나로 4채널(cam1~cam4) 전부 연결.
    void connectCameras(const QString &mediaMtxHost);

    // 계약① 감지결과 수신 시 MainWindow가 호출. channel은 1-based(1~4).
    void updateDetection(int channel, int srcW, int srcH, const QVector<DetectionBox> &boxes);
    // 계약①의 alarm 필드를 해당 채널 카드에 반영("감지" 배지 + 테두리 강조).
    void setChannelAlarm(int channel, bool active);

    // 서버 actuator_status를 좌측 StatusPanel의 종합상태에 반영.
    void setActuatorStatus(int fan, int valve, int siren, const QString &link,
                            const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc);
    // 수동 제어 명령의 대기중/성공/실패/타임아웃 결과를 StatusPanel에 잠깐 보여준다.
    void showControlStatus(const QString &text, const QString &color);
    // 특정 액추에이터(fan/valve/siren) 한 줄만 "처리 중.../응답 없음" 등으로 잠깐 덮어쓴다.
    void setActuatorRowStatus(const QString &target, const QString &text, const QString &color);
    // 서버 sensor 메시지의 evacuation 필드를 StatusPanel 버튼 문구("발동"/"해제")에 반영.
    void setEvacuationActive(bool active);

signals:
    void demoStateRequested(ZoneState state);
    void controlActionRequested(const QString &target, const QString &action, const QString &title);
    void evacuationActionRequested(bool activate);

private:
    // 채널 카드를 더블클릭하면 같은 스트림을 새로 하나 더 연결해 확대 창으로 띄운다.
    void showEnlargedView(int channel);

    StatusPanel *statusPanel;
    VideoWidget *videoWidgets[4];
    StreamReceiver *streamReceivers[4];
    QString mediaMtxHost;
};

#endif // MONITORPAGE_H
