#include "WarningAlertDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace {
const QString kBg = "#14141f";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
}

WarningAlertDialog::WarningAlertDialog(const QString &zoneName, const QString &cause,
                                        int initialRemainSeconds, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("경고 알림");
    setWindowModality(Qt::ApplicationModal);
    setStyleSheet(QString("background-color:%1;").arg(kBg));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);

    auto *header = new QLabel("⚠ 경고 발생", this);
    header->setStyleSheet("color:#fbbf24; font-size:19px; font-weight:bold;");
    layout->addWidget(header);

    auto *message = new QLabel(QString("%1 구역에서 경고 상태가 감지되었습니다.").arg(zoneName), this);
    message->setStyleSheet(QString("color:%1; font-size:17px; font-weight:bold;").arg(kTextPrimary));
    message->setWordWrap(true);
    layout->addWidget(message);

    if (!cause.isEmpty()) {
        auto *causeLabel = new QLabel(cause + "!", this);
        causeLabel->setStyleSheet("color:#fbbf24; font-size:26px; font-weight:bold;");
        causeLabel->setWordWrap(true);
        layout->addWidget(causeLabel);
    }

    auto *countdownRow = new QHBoxLayout;
    countdownRow->setSpacing(12);
    countdownNumberLabel = new QLabel(this);
    countdownNumberLabel->setStyleSheet("color:#f87171; font-size:42px; font-weight:bold;");
    countdownRow->addWidget(countdownNumberLabel);
    countdownLabel = new QLabel(this);
    countdownLabel->setStyleSheet(QString("color:%1; font-size:14px;").arg(kTextSecondary));
    countdownRow->addWidget(countdownLabel, 1);
    layout->addLayout(countdownRow);
    setRemainingSeconds(initialRemainSeconds);

    layout->addSpacing(10);
    auto *ackBtn = new QPushButton("확인", this);
    ackBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; font-size:16px; border-radius:8px; padding:14px; }");
    layout->addWidget(ackBtn);

    connect(ackBtn, &QPushButton::clicked, this, [this]() {
        emit acknowledged();
        accept();
    });
}

void WarningAlertDialog::setRemainingSeconds(int seconds)
{
    if (seconds > 0) {
        countdownNumberLabel->setVisible(true);
        countdownLabel->setVisible(true);
        countdownNumberLabel->setText(QString::number(seconds));
        countdownLabel->setText("초 후 서버가 자동으로\n위험 모드로 전환합니다.");
    } else {
        countdownNumberLabel->setVisible(false);
        countdownLabel->setVisible(false);
    }
}
