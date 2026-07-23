#include "WarningAlertDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace {
const QString kBg = "#14141f";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
}

WarningAlertDialog::WarningAlertDialog(const QString &zoneName, const QString &cause,
                                        int countdownSeconds, QWidget *parent)
    : QDialog(parent)
    , remaining(countdownSeconds)
{
    setWindowTitle("경고 알림");
    setWindowModality(Qt::ApplicationModal);
    setStyleSheet(QString("background-color:%1;").arg(kBg));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *header = new QLabel("⚠ 경고 발생", this);
    header->setStyleSheet("color:#fbbf24; font-size:16px; font-weight:bold;");
    layout->addWidget(header);

    auto *message = new QLabel(QString("%1 구역에서 경고 상태가 감지되었습니다.").arg(zoneName), this);
    message->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    message->setWordWrap(true);
    layout->addWidget(message);

    if (!cause.isEmpty()) {
        auto *causeLabel = new QLabel("원인: " + cause, this);
        causeLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
        layout->addWidget(causeLabel);
    }

    countdownLabel = new QLabel(this);
    countdownLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(countdownLabel);

    layout->addSpacing(10);
    auto *ackBtn = new QPushButton("확인", this);
    ackBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; border-radius:6px; padding:10px; }");
    layout->addWidget(ackBtn);

    connect(ackBtn, &QPushButton::clicked, this, [this]() {
        timer->stop();
        emit acknowledged();
        accept();
    });

    timer = new QTimer(this);
    auto updateCountdownText = [this]() {
        countdownLabel->setText(QString("%1초 후 무응답으로 자동 처리됩니다.").arg(remaining));
    };
    updateCountdownText();
    connect(timer, &QTimer::timeout, this, [this, updateCountdownText]() {
        --remaining;
        updateCountdownText();
        if (remaining <= 0) {
            timer->stop();
            emit timedOut();
            reject();
        }
    });
    timer->start(1000);
}
