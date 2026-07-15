#include "loginwindow.h"

#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
const QString kValidId = "admin";
const QString kValidPw = "1234";

const QString kBg = "#0a0a12";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
}

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("공장 가스·화재 조기감지 및 자동대응 시스템 - 로그인");
    resize(480, 640);
    setStyleSheet(QString("background-color:%1;").arg(kBg));

    auto *outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);

    auto *box = new QWidget(this);
    box->setFixedWidth(360);
    box->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:10px;").arg(kCardBg, kCardBorder));
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(32, 32, 32, 32);
    boxLayout->setSpacing(10);

    auto *icon = new QLabel(box);
    icon->setFixedSize(52, 52);
    icon->setStyleSheet(
        "background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6ee7d8, stop:1 #8b7cf6);"
        "border-radius:14px;");
    boxLayout->addWidget(icon);
    boxLayout->addSpacing(6);

    auto *title = new QLabel("통합 관제 플랫폼", box);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold;").arg(kTextPrimary));
    boxLayout->addWidget(title);

    auto *subtitle = new QLabel("계정으로 로그인하세요", box);
    subtitle->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    boxLayout->addWidget(subtitle);
    boxLayout->addSpacing(14);

    const QString fieldStyle = QString(
        "QLineEdit { background-color:#1a1a26; color:%1; border:1px solid %2; border-radius:6px; padding:8px; }")
        .arg(kTextPrimary, kCardBorder);

    auto *idLabel = new QLabel("아이디", box);
    idLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    boxLayout->addWidget(idLabel);
    idEdit = new QLineEdit(box);
    idEdit->setPlaceholderText("admin");
    idEdit->setStyleSheet(fieldStyle);
    idEdit->setMinimumHeight(36);
    boxLayout->addWidget(idEdit);

    auto *pwLabel = new QLabel("비밀번호", box);
    pwLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    boxLayout->addWidget(pwLabel);
    pwEdit = new QLineEdit(box);
    pwEdit->setPlaceholderText("••••••••");
    pwEdit->setEchoMode(QLineEdit::Password);
    pwEdit->setStyleSheet(fieldStyle);
    pwEdit->setMinimumHeight(36);
    boxLayout->addWidget(pwEdit);

    auto *optionsRow = new QHBoxLayout;
    auto *autoLogin = new QCheckBox("자동 로그인", box);
    autoLogin->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    optionsRow->addWidget(autoLogin);
    optionsRow->addStretch();
    auto *findPw = new QLabel("<a href='#' style='color:#8b7cf6;'>비밀번호 찾기</a>", box);
    optionsRow->addWidget(findPw);
    boxLayout->addLayout(optionsRow);
    boxLayout->addSpacing(8);

    auto *zoneLabel = new QLabel("담당 구역", box);
    zoneLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    boxLayout->addWidget(zoneLabel);
    auto *zoneCombo = new QComboBox(box);
    zoneCombo->addItem("A구역 — 1공장 생산동");
    zoneCombo->addItem("B구역 — 1공장 저장동");
    zoneCombo->setStyleSheet(QString(
        "QComboBox { background-color:#1a1a26; color:%1; border:1px solid %2; border-radius:6px; padding:8px; }")
        .arg(kTextPrimary, kCardBorder));
    zoneCombo->setMinimumHeight(36);
    boxLayout->addWidget(zoneCombo);
    boxLayout->addSpacing(10);

    errorLabel = new QLabel(box);
    errorLabel->setStyleSheet("color:#f87171;");
    errorLabel->setAlignment(Qt::AlignCenter);
    boxLayout->addWidget(errorLabel);

    auto *loginButton = new QPushButton("로그인", box);
    loginButton->setMinimumHeight(40);
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setStyleSheet(QString(
        "QPushButton { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 #a78bfa);"
        "color:white; font-weight:bold; border-radius:8px; }"
        "QPushButton:hover { background-color:%1; }").arg(kAccent));
    boxLayout->addWidget(loginButton);

    outer->addWidget(box);

    connect(loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(pwEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
}

void LoginWindow::onLoginClicked()
{
    if (idEdit->text() == kValidId && pwEdit->text() == kValidPw) {
        errorLabel->clear();
        emit loginSucceeded();
        return;
    }
    errorLabel->setText("아이디 또는 비밀번호가 올바르지 않습니다.");
}
