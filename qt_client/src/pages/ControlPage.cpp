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

struct ControlOption { QString action; QString label; };
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
    cardRow->addWidget(createValveControlCard());
    cardRow->addWidget(createSirenControlCard());
    cardRow->addWidget(createEvacuationCard());
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
    updateButtonStyles(fanButtons, level);
    updateStatusLabel(fanStatusLabel, fanButtons, level);
}

void ControlPage::setValveState(int state)
{
    currentValveState = state;
    updateButtonStyles(valveButtons, state);
    updateStatusLabel(valveStatusLabel, valveButtons, state);
}

void ControlPage::setSirenState(int state)
{
    currentSirenState = state;
    updateButtonStyles(sirenButtons, state);
    updateStatusLabel(sirenStatusLabel, sirenButtons, state);
}

void ControlPage::updateButtonStyles(QVector<QPushButton *> &buttons, int activeIndex)
{
    for (int i = 0; i < buttons.size(); ++i) {
        const bool active = (i == activeIndex);
        buttons[i]->setStyleSheet(active
            ? QString("QPushButton { background-color:%1; color:white; border:none; border-radius:6px; font-size:14px; font-weight:bold; }").arg(kAccent)
            : QString("QPushButton { background-color:#232333; color:%1; border:none; border-radius:6px; font-size:14px; }").arg(kTextSecondary));
    }
}

void ControlPage::updateStatusLabel(QLabel *label, QVector<QPushButton *> &buttons, int activeIndex)
{
    if (!label)
        return;
    if (activeIndex >= 0 && activeIndex < buttons.size()) {
        label->setText("현재 상태: " + buttons[activeIndex]->text());
        label->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; border:none;").arg(kAccent));
    } else {
        label->setText("현재 상태: 확인 중");
        label->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; border:none;").arg(kTextSecondary));
    }
}

QWidget *ControlPage::createFanControlCard()
{
    auto *card = new QFrame;
    card->setFixedSize(260, 150);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }")
                         .arg(kCardBg, kCardBorder));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);
    auto *titleLbl = new QLabel("환기팬 속도 제어", card);
    titleLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel("IP66 환기팬 속도 제어", card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);

    fanStatusLabel = new QLabel(card);
    layout->addWidget(fanStatusLabel);
    layout->addStretch();

    static const QVector<ControlOption> kOptions = {
        { "off", "OFF" }, { "low", "약" }, { "mid", "중" }, { "high", "강" }
    };

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    fanButtons.clear();
    for (int i = 0; i < kOptions.size(); ++i) {
        const ControlOption &opt = kOptions[i];
        auto *btn = new QPushButton(opt.label, card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(52, 34); // 네 버튼 크기를 동일하게 고정
        connect(btn, &QPushButton::clicked, this, [this, opt, i]() {
            if (showConfirmDialog("환기팬 " + opt.label)) {
                emit controlRequested("fan", opt.action, "환기팬 " + opt.label);
                // 서버 응답(actuator_status)을 기다리지 않고 클릭 즉시 반영
                setFanLevel(i);
            }
        });
        fanButtons.append(btn);
        btnRow->addWidget(btn);
    }
    layout->addLayout(btnRow);

    setFanLevel(currentFanLevel); // 서버 상태를 아직 모르므로 전부 비활성 표시로 시작
    return card;
}

QWidget *ControlPage::createValveControlCard()
{
    auto *card = new QFrame;
    card->setFixedSize(260, 150);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }")
                         .arg(kCardBg, kCardBorder));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);
    auto *titleLbl = new QLabel("밸브 개방 / 잠금", card);
    titleLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel("가스 공급 솔레노이드 밸브", card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);

    valveStatusLabel = new QLabel(card);
    layout->addWidget(valveStatusLabel);
    layout->addStretch();

    static const QVector<ControlOption> kOptions = {
        { "close", "잠금" }, { "open", "개방" }
    };

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    valveButtons.clear();
    for (int i = 0; i < kOptions.size(); ++i) {
        const ControlOption &opt = kOptions[i];
        auto *btn = new QPushButton(opt.label, card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(110, 34);
        connect(btn, &QPushButton::clicked, this, [this, opt, i]() {
            if (showConfirmDialog("밸브 " + opt.label)) {
                emit controlRequested("valve", opt.action, "밸브 " + opt.label);
                setValveState(i); // 서버 응답을 기다리지 않고 클릭 즉시 반영
            }
        });
        valveButtons.append(btn);
        btnRow->addWidget(btn);
    }
    layout->addLayout(btnRow);

    setValveState(currentValveState);
    return card;
}

QWidget *ControlPage::createSirenControlCard()
{
    auto *card = new QFrame;
    card->setFixedSize(260, 150);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }")
                         .arg(kCardBg, kCardBorder));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);
    auto *titleLbl = new QLabel("사이렌 ON / OFF", card);
    titleLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel("경보음 및 경고 LED 제어", card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);

    sirenStatusLabel = new QLabel(card);
    layout->addWidget(sirenStatusLabel);
    layout->addStretch();

    static const QVector<ControlOption> kOptions = {
        { "off", "OFF" }, { "on", "ON" }
    };

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    sirenButtons.clear();
    for (int i = 0; i < kOptions.size(); ++i) {
        const ControlOption &opt = kOptions[i];
        auto *btn = new QPushButton(opt.label, card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(110, 34);
        connect(btn, &QPushButton::clicked, this, [this, opt, i]() {
            if (showConfirmDialog("사이렌 " + opt.label)) {
                emit controlRequested("siren", opt.action, "사이렌 " + opt.label);
                setSirenState(i); // 서버 응답을 기다리지 않고 클릭 즉시 반영
            }
        });
        sirenButtons.append(btn);
        btnRow->addWidget(btn);
    }
    layout->addLayout(btnRow);

    setSirenState(currentSirenState);
    return card;
}

QWidget *ControlPage::createEvacuationCard()
{
    auto *card = new QFrame;
    card->setFixedSize(260, 150);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet("QFrame { background-color:#2a1414; border:1px solid #7f2f2f; border-radius:8px; }"
                         "QFrame:hover { border:1px solid #ef4444; }");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);
    auto *titleLbl = new QLabel("대피 모드 발동", card);
    titleLbl->setStyleSheet("color:#fca5a5; font-size:15px; font-weight:bold; border:none;");
    auto *descLabel = new QLabel("전 구역 대피 경보 발령 (관리자 수동)", card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLbl);
    layout->addWidget(descLabel);
    layout->addStretch();

    auto *triggerBtn = new QPushButton("대피 모드 실행", card);
    triggerBtn->setCursor(Qt::PointingHandCursor);
    triggerBtn->setFixedHeight(40);
    triggerBtn->setStyleSheet("QPushButton { background-color:#ef4444; color:white; border:none; border-radius:6px; font-size:14px; font-weight:bold; }"
                               "QPushButton:hover { background-color:#dc2626; }");
    connect(triggerBtn, &QPushButton::clicked, this, [this]() {
        if (showConfirmDialog("대피 모드 실행"))
            emit controlRequested("evacuation", "trigger", "대피 모드 실행");
    });
    layout->addWidget(triggerBtn);

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
