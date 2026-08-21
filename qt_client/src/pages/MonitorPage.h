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
    // 사람 감지(명세서 3번 계약) 수신 시 MainWindow가 호출. channel은 1-based(1~4).
    void updatePersonBoxes(int channel, int srcW, int srcH, int count, const QVector<DetectionBox> &boxes);

    // 서버 actuator_status를 좌측 StatusPanel의 종합상태에 반영.
    void setActuatorStatus(int fan, int valve, int siren, const QString &link,
                            const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc,
                            int targetFan, int targetValve, int targetSiren, const QString &linkReason);
    // 대피 음성 안내 송출 여부(PR #69 voice 필드). 사이렌(STM 부저)과 별개라 따로 온다.
    // 채널 고정 배정("Ch.4 사이렌&스피커")에 맞춰 4번 카메라 카드에 🔊 아이콘으로 표시한다.
    void setVoiceAnnouncementActive(bool active);
    // 수동 제어 명령의 대기중/성공/실패/타임아웃 결과를 StatusPanel에 잠깐 보여준다.
    void showControlStatus(const QString &text, const QString &color);
    // 특정 액추에이터(fan/valve/siren) 한 줄만 "처리 중.../응답 없음" 등으로 잠깐 덮어쓴다.
    void setActuatorRowStatus(const QString &target, const QString &text, const QString &color);
    // 서버 sensor 메시지의 visionOk(채널별 서버 영상 감지 생존 여부) 반영 (emergency-mode #14).
    void setCameraVisionStatus(bool ch1, bool ch2, bool ch3, bool ch4);
    // 접속 직후 push 또는 query 응답으로 받은 ROI를 해당 채널(1-based) 카메라 카드에 반영.
    void applyRoiRegionsFromServer(int channel, const QVector<RoiRegion> &regions);

signals:
    void demoStateRequested(ZoneState state);
    void controlActionRequested(const QString &target, const QString &action, const QString &title);
    void emergencyTriggerRequested(const QString &cause);
    void emergencyClearRequested(const QString &admin, const QStringList &checklist);
    // 채널 하나의 ROI 편집이 끝났을 때. MainWindow가 받아서 서버로 전송한다.
    void roiRegionsChanged(int channel, const QVector<RoiRegion> &regions);

private:
    // 채널 카드를 더블클릭하면 같은 스트림을 새로 하나 더 연결해 확대 창으로 띄운다.
    void showEnlargedView(int channel);

    StatusPanel *statusPanel;
    VideoWidget *videoWidgets[4];
    StreamReceiver *streamReceivers[4];
    QString mediaMtxHost;
};

#endif // MONITORPAGE_H
