// qt_main.cpp ─ Qt 4채널 RTSP + 화재 감지 통합본
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>

// ── 재환님 감지 코드 ──
#include "DetectionTypes.h"
#include "FireDetector_1.h"
#include "DisplayUtils.h"

static const int NUM_CH = 4;

static const std::string CAM_IP   = "172.20.35.211";   // ★ IP 바뀌면 여기
static const std::string CAM_USER = "admin";
static const std::string CAM_PW   = "5hanwha!";

// 감지 주기: N프레임에 한 번만 감지 (부하 절감)
static const int DETECT_INTERVAL = 5;

std::string makeUrl(int ch) {
    return "rtsp://" + CAM_USER + ":" + CAM_PW + "@" + CAM_IP +
           ":554/" + std::to_string(ch) + "/profile2/media.smp";
}

// ── 공유 저장소 ──
std::vector<cv::Mat> g_frames(NUM_CH);
std::vector<bool>    g_fire(NUM_CH, false);
std::mutex g_mtx;
std::atomic<bool> g_stop(false);

// ── 워커 스레드: 수신 → 감지 → 박스 그린 프레임 저장 ──
void captureWorker(int idx) {
    cv::VideoCapture cap(makeUrl(idx));
    if (!cap.isOpened()) return;

    FireDetector detector;          // 채널마다 별도 감지기
    DetectionResult lastResult;
    cv::Mat frame;
    long count = 0;

    while (!g_stop.load()) {
        if (!cap.read(frame) || frame.empty()) break;

        if (count % DETECT_INTERVAL == 0) {
            lastResult = detector.detect(frame);
        }
        //std::cout << "ch" << idx << " after detect: " << frame.cols << "x" << frame.rows << std::endl;
        drawDetectionResult(frame, lastResult);

        {
            std::lock_guard<std::mutex> lock(g_mtx);
            g_frames[idx] = frame.clone();
            g_fire[idx] = lastResult.detected;
        }
        count++;
    }
    cap.release();
}

// ── 메인 윈도우 ──
class MainWindow : public QWidget {
public:
    MainWindow() {
        setWindowTitle("공장 관제 - 4채널 CCTV + 화재감지");
        resize(1300, 760);

        QGridLayout* grid = new QGridLayout(this);
        for (int i = 0; i < NUM_CH; i++) {
            labels[i] = new QLabel(this);
            labels[i]->setMinimumSize(320, 180);
            //labels[i]->setScaledContents(false);           // 추가
            //labels[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);  // 추가
            labels[i]->setStyleSheet(normalStyle);
            labels[i]->setAlignment(Qt::AlignCenter);
            labels[i]->setText(QString("CH%1 연결 대기...").arg(i));
            grid->addWidget(labels[i], i / 2, i % 2);
        }

        QTimer* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::updateFrames);
        timer->start(33);
    }

private:
    QLabel* labels[NUM_CH];
    const QString normalStyle = "background-color: black; color: white;";
    const QString fireStyle    = "background-color: black; color: white; border: 4px solid red;";

    void updateFrames() {
        std::vector<cv::Mat> local(NUM_CH);
        std::vector<bool> fire(NUM_CH, false);
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            for (int i = 0; i < NUM_CH; i++) {
                if (!g_frames[i].empty()) local[i] = g_frames[i].clone();
                fire[i] = g_fire[i];
            }
        }

        for (int i = 0; i < NUM_CH; i++) {
            if (local[i].empty()) continue;

            cv::Mat rgb;
            cv::cvtColor(local[i], rgb, cv::COLOR_BGR2RGB);
            QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);

            QPixmap pix = QPixmap::fromImage(img.copy());
            labels[i]->setPixmap(pix.scaled(labels[i]->size(),
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));

            labels[i]->setStyleSheet(fire[i] ? fireStyle : normalStyle);
        }
    }
};

int main(int argc, char* argv[]) {
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 1);

    QApplication app(argc, argv);

    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_CH; i++)
        workers.emplace_back(captureWorker, i);

    MainWindow w;
    w.show();
    int ret = app.exec();

    g_stop.store(true);
    for (auto& t : workers)
        if (t.joinable()) t.join();

    return ret;
}
