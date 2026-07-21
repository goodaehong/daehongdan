#include "ControlPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QDialog>

namespace {
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
}

ControlPage::ControlPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(4);

    titleLabel = new QLabel(this);
    titleLabel->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(titleLabel);

    auto *subtitle = new QLabel("오작동 방지를 위해 모든 조작은 확인 단계를 거칩니다.", this);
    subtitle->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(subtitle);
    layout->addSpacing(16);

    auto *cardRow = new QHBoxLayout;
    cardRow->setSpacing(16);
    cardRow->addWidget(createFanControlCard());
    cardRow->addWidget(createControlCard("밸브 개방 / 잠금", "가스 공급 솔레노이드 밸브",
        [this]() { return currentValveState == 1 ? QString("밸브 잠금") : QString("밸브 개방"); },
        [this]() {
            const bool willOpen = (currentValveState != 1);
            emit controlRequested("valve", willOpen ? "open" : "close", willOpen ? "밸브 개방" : "밸브 잠금");
            setValveState(willOpen ? 1 : 0); // 서버 응답을 기다리지 않고 클릭 즉시 반영
        },
        &valveStatusLabel));
    cardRow->addWidget(createControlCard("사이렌 켜기 / 끄기", "경보음 및 경고 LED 제어",
        [this]() { return currentSirenState == 1 ? QString("사이렌 끄기") : QString("사이렌 켜기"); },
        [this]() {
            const bool willTurnOn = (currentSirenState != 1);
            emit controlRequested("siren", willTurnOn ? "on" : "off", willTurnOn ? "사이렌 켜기" : "사이렌 끄기");
            setSirenState(willTurnOn ? 1 : 0); // 서버 응답을 기다리지 않고 클릭 즉시 반영
        },
        &sirenStatusLabel));
    cardRow->addStretch();
    layout->addLayout(cardRow);
    layout->addStretch();
}

void ControlPage::setZoneName(const QString &zoneName)
{
    titleLabel->setText("수동 제어 — " + zoneName);
}

void ControlPage::setFanLevel(int level)
{
    currentFanLevel = level;
    updateFanButtonStyles(level);
}

void ControlPage::setValveState(int state)
{
    currentValveState = state;
    if (!valveStatusLabel)
        return;
    const QString text = state == 1 ? "현재 상태: 개방" : state == 0 ? "현재 상태: 잠금" : "현재 상태: 확인 중";
    valveStatusLabel->setText(text);
    valveStatusLabel->setStyleSheet(QString("color:%1; font-size:11px; font-weight:bold; border:none;")
                                     .arg(state == 1 ? kAccent : kTextSecondary));
}

void ControlPage::setSirenState(int state)
{
    currentSirenState = state;
    if (!sirenStatusLabel)
        return;
    const QString text = state == 1 ? "현재 상태: ON" : state == 0 ? "현재 상태: OFF" : "현재 상태: 확인 중";
    sirenStatusLabel->setText(text);
    sirenStatusLabel->setStyleSheet(QString("color:%1; font-size:11px; font-weight:bold; border:none;")
                                     .arg(state == 1 ? "#f87171" : kTextSecondary));
}

QWidget *ControlPage::createFanControlCard()
{
    auto *card = new QFrame;
    card->setFixedSize(220, 110);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }")
                         .arg(kCardBg, kCardBorder));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    auto *titleLbl = new QLabel("환기팬 속도 제어", card);
    titleLbl->setStyleSheet(QString("color:%1; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel("IP66 환기팬 속도 제어", card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);
    layout->addStretch();

    struct FanLevel { QString action; QString label; };
    static const QVector<FanLevel> kLevels = {
        { "off", "OFF" }, { "low", "약" }, { "mid", "중" }, { "high", "강" }
    };

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    fanButtons.clear();
    for (int i = 0; i < kLevels.size(); ++i) {
        const FanLevel &lvl = kLevels[i];
        auto *btn = new QPushButton(lvl.label, card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(42, 28); // 네 버튼 크기를 동일하게 고정
        connect(btn, &QPushButton::clicked, this, [this, lvl, i]() {
            if (showConfirmDialog("환기팬 " + lvl.label)) {
                emit controlRequested("fan", lvl.action, "환기팬 " + lvl.label);
                // 서버 응답(actuator_status)을 기다리지 않고 클릭 즉시 하이라이트
                currentFanLevel = i;
                updateFanButtonStyles(currentFanLevel);
            }
        });
        fanButtons.append(btn);
        btnRow->addWidget(btn);
    }
    layout->addLayout(btnRow);

    updateFanButtonStyles(currentFanLevel); // 서버 상태를 아직 모르므로 전부 비활성 표시로 시작
    return card;
}

void ControlPage::updateFanButtonStyles(int activeLevel)
{
    for (int i = 0; i < fanButtons.size(); ++i) {
        const bool active = (i == activeLevel);
        fanButtons[i]->setStyleSheet(active
            ? QString("QPushButton { background-color:%1; color:white; border:none; border-radius:4px; font-weight:bold; }").arg(kAccent)
            : QString("QPushButton { background-color:#232333; color:%1; border:none; border-radius:4px; }").arg(kTextSecondary));
    }
}

QWidget *ControlPage::createControlCard(const QString &title, const QString &desc,
                                         const std::function<QString()> &actionTitleProvider,
                                         const std::function<void()> &onConfirm,
                                         QLabel **statusLabelOut)
{
    auto *card = new QFrame;
    card->setFixedSize(220, 110);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }"
                                 "QFrame:hover { border:1px solid %3; }").arg(kCardBg, kCardBorder, kAccent));
    auto *layout = new QVBoxLayout(card);
    auto *titleLbl = new QLabel(title, card);
    titleLbl->setStyleSheet(QString("color:%1; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel(desc, card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);

    if (statusLabelOut) {
        auto *statusLabel = new QLabel("현재 상태: 확인 중", card);
        statusLabel->setStyleSheet(QString("color:%1; font-size:11px; font-weight:bold; border:none;").arg(kTextSecondary));
        layout->addWidget(statusLabel);
        *statusLabelOut = statusLabel;
    }
    layout->addStretch();

    auto *button = new QPushButton(card);
    button->setStyleSheet("background:transparent; border:none;");
    button->setGeometry(card->rect());
    connect(button, &QPushButton::clicked, this, [this, actionTitleProvider, onConfirm]() {
        if (showConfirmDialog(actionTitleProvider()))
            onConfirm();
    });

    return card;
}

bool ControlPage::showConfirmDialog(const QString &actionName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("조작 확인");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *header = new QLabel("⚠ 조작 확인", &dialog);
    header->setStyleSheet("color:#fbbf24; font-weight:bold;");
    layout->addWidget(header);

    auto *question = new QLabel(QString("정말 '%1'를 실행하시겠습니까?").arg(actionName), &dialog);
    question->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    question->setWordWrap(true);
    layout->addWidget(question);

    auto *sub = new QLabel("이 작업은 즉시 실행됩니다. 계속하시겠습니까?", &dialog);
    sub->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(sub);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; border-radius:6px; padding:10px; }").arg(kTextPrimary));
    auto *execBtn = new QPushButton("실행", &dialog);
    execBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; border-radius:6px; padding:10px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(execBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(execBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}
