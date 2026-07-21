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
    cardRow->addWidget(createControlCard("환기팬 가동", "IP66 환기팬 속도 제어", [this]() {
        emit controlRequested("fan", "on", "환기팬 가동");
    }));
    cardRow->addWidget(createControlCard("밸브 개방 / 잠금", "가스 공급 솔레노이드 밸브", [this]() {
        emit controlRequested("valve", "open", "밸브 개방 / 잠금");
    }));
    cardRow->addWidget(createControlCard("사이렌 끄기", "경보음 및 경고 LED 제어", [this]() {
        emit controlRequested("siren", "off", "사이렌 끄기");
    }));
    cardRow->addStretch();
    layout->addLayout(cardRow);
    layout->addStretch();
}

void ControlPage::setZoneName(const QString &zoneName)
{
    titleLabel->setText("수동 제어 — " + zoneName);
}

QWidget *ControlPage::createControlCard(const QString &title, const QString &desc, const std::function<void()> &onConfirm)
{
    auto *card = new QFrame;
    card->setFixedSize(220, 90);
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
    layout->addStretch();

    auto *button = new QPushButton(card);
    button->setStyleSheet("background:transparent; border:none;");
    button->setGeometry(card->rect());
    connect(button, &QPushButton::clicked, this, [this, title, onConfirm]() {
        if (showConfirmDialog(title))
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
