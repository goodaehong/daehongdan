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
    : QFrame(parent)
{
    setObjectName("warningAlertBanner");
    setStyleSheet(QString(
        "QFrame#warningAlertBanner { background-color:%1; border:1px solid #fbbf24; "
        "border-left:5px solid #fbbf24; }"
        "QLabel { border:none; background:transparent; }").arg(kBg));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(14);

    auto *header = new QLabel("⚠ 경고", this);
    header->setStyleSheet("color:#fbbf24; font-size:17px; font-weight:bold;");
    layout->addWidget(header);

    const QString detail = cause.isEmpty()
        ? QString("%1에서 경고 상태가 감지되었습니다.").arg(zoneName)
        : QString("%1 · %2!").arg(zoneName, cause);
    auto *message = new QLabel(detail, this);
    message->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(message, 1);

    auto *countdownRow = new QHBoxLayout;
    countdownRow->setSpacing(6);
    countdownNumberLabel = new QLabel(this);
    countdownNumberLabel->setStyleSheet("color:#f87171; font-size:22px; font-weight:bold;");
    countdownRow->addWidget(countdownNumberLabel);
    countdownLabel = new QLabel(this);
    countdownLabel->setStyleSheet(QString("color:%1; font-size:12px;").arg(kTextSecondary));
    countdownRow->addWidget(countdownLabel);
    layout->addLayout(countdownRow);
    setRemainingSeconds(initialRemainSeconds);

    auto *ackBtn = new QPushButton("확인", this);
    ackBtn->setCursor(Qt::PointingHandCursor);
    ackBtn->setStyleSheet(
        "QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; "
        "font-size:13px; border:none; border-radius:6px; padding:8px 18px; }"
        "QPushButton:hover { background-color:#fde68a; }");
    layout->addWidget(ackBtn);

    connect(ackBtn, &QPushButton::clicked, this, [this]() {
        emit acknowledged();
        dismiss();
    });
}

void WarningAlertDialog::dismiss()
{
    hide();
    emit finished();
}

void WarningAlertDialog::setRemainingSeconds(int seconds)
{
    if (seconds > 0) {
        countdownNumberLabel->setVisible(true);
        countdownLabel->setVisible(true);
        countdownNumberLabel->setText(QString::number(seconds));
        countdownLabel->setText("초 후 자동 위험 전환");
    } else {
        countdownNumberLabel->setVisible(false);
        countdownLabel->setVisible(false);
    }
}
