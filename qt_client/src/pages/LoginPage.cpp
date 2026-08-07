#include "LoginPage.h"

#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QPainter>
#include <QPaintEvent>
#include <QGraphicsDropShadowEffect>

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

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("공장 가스·화재 조기감지 및 자동대응 시스템 - 로그인");
    resize(1000, 700);
    bgImage = QPixmap(":/login_bg.jpg");

    auto *outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);

    auto *box = new QWidget(this);
    box->setFixedWidth(420);
    // 카드 배경을 완전 불투명이 아니라 살짝 반투명(rgba)으로 둬서 뒤의 배경 사진 느낌이
    // 은은하게 비치도록 하되, 글씨는 충분히 읽히도록 알파를 높게 유지한다.
    // 상단에 accent 색 강조선을 둬서 "관제센터" 모니터 느낌을 살린다.
    box->setStyleSheet(QString(
        "background-color:rgba(20,20,31,235); border:1px solid %1; border-top:3px solid %2; border-radius:14px;")
        .arg(kCardBorder, kAccent));

    // 배경 사진 위에서 카드가 떠 보이도록 은은한 그림자를 추가한다.
    auto *shadow = new QGraphicsDropShadowEffect(box);
    shadow->setBlurRadius(48);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 160));
    box->setGraphicsEffect(shadow);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(32, 32, 32, 32);
    boxLayout->setSpacing(10);

    auto *icon = new QLabel("🏭", box);
    icon->setFixedSize(52, 52);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(
        "background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6ee7d8, stop:1 #8b7cf6);"
        "border-radius:14px; font-size:26px; border:none;");
    boxLayout->addWidget(icon);
    boxLayout->addSpacing(6);

    auto *title = new QLabel("파이어가드 관제센터", box);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; border:none;").arg(kTextPrimary));
    boxLayout->addWidget(title);

    auto *subtitle = new QLabel("가스·화재를 실시간으로 감시합니다", box);
    subtitle->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    boxLayout->addWidget(subtitle);
    boxLayout->addSpacing(14);

    const QString fieldStyle = QString(
        "QLineEdit { background-color:#1a1a26; color:%1; border:1px solid %2; border-radius:6px; padding:8px; }")
        .arg(kTextPrimary, kCardBorder);

    auto *idLabel = new QLabel("아이디", box);
    idLabel->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    boxLayout->addWidget(idLabel);
    idEdit = new QLineEdit(box);
    idEdit->setPlaceholderText("admin");
    idEdit->setStyleSheet(fieldStyle);
    idEdit->setMinimumHeight(36);
    boxLayout->addWidget(idEdit);

    auto *pwLabel = new QLabel("비밀번호", box);
    pwLabel->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    boxLayout->addWidget(pwLabel);
    pwEdit = new QLineEdit(box);
    pwEdit->setPlaceholderText("••••••••");
    pwEdit->setEchoMode(QLineEdit::Password);
    pwEdit->setStyleSheet(fieldStyle);
    pwEdit->setMinimumHeight(36);
    boxLayout->addWidget(pwEdit);

    auto *optionsRow = new QHBoxLayout;
    autoLoginCheck = new QCheckBox("자동 로그인", box);
    autoLoginCheck->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    optionsRow->addWidget(autoLoginCheck);
    optionsRow->addStretch();
    auto *findPw = new QLabel("<a href='#' style='color:#8b7cf6;'>비밀번호 찾기</a>", box);
    findPw->setStyleSheet("border:none;");
    optionsRow->addWidget(findPw);
    boxLayout->addLayout(optionsRow);
    boxLayout->addSpacing(8);

    auto *zoneLabel = new QLabel("담당 공장", box);
    zoneLabel->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    boxLayout->addWidget(zoneLabel);
    auto *zoneCombo = new QComboBox(box);
    zoneCombo->addItem("A공장");
    zoneCombo->addItem("B공장");
    zoneCombo->addItem("C공장");
    zoneCombo->addItem("D공장");
    zoneCombo->setStyleSheet(QString(
        "QComboBox { background-color:#1a1a26; color:%1; border:1px solid %2; border-radius:6px; padding:8px; }"
        // 드롭다운 펼쳐졌을 때 목록은 위에서 지정 안 하면 시스템 기본 팔레트(밝은 배경+검은 글씨)를
        // 써서 어두운 화면에서 글씨가 거의 안 보였음 -> 명시적으로 지정.
        "QComboBox QAbstractItemView { background-color:#1a1a26; color:%1; border:1px solid %2; "
        "selection-background-color:%3; selection-color:white; }")
        .arg(kTextPrimary, kCardBorder, kAccent));
    zoneCombo->setMinimumHeight(36);
    boxLayout->addWidget(zoneCombo);
    boxLayout->addSpacing(10);

    errorLabel = new QLabel(box);
    errorLabel->setStyleSheet("color:#f87171; border:none;");
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

    connect(loginButton, &QPushButton::clicked, this, &LoginPage::onLoginClicked);
    connect(pwEdit, &QLineEdit::returnPressed, this, &LoginPage::onLoginClicked);

    // "자동 로그인"을 체크하고 로그인했었으면 다음 실행부터 dashboard_main.cpp가 이 화면 자체를
    // 건너뛴다. 그래도 (예: 수동 로그아웃 후) 이 화면이 다시 뜨는 경우엔 아이디/체크 상태를 그대로
    // 복원해서 다시 입력하는 수고를 덜어준다. 비밀번호는 저장하지 않는다(평문 저장 지양).
    QSettings settings;
    if (settings.value("autoLogin", false).toBool()) {
        idEdit->setText(settings.value("lastId").toString());
        autoLoginCheck->setChecked(true);
    }
}

void LoginPage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(kBg));

    if (!bgImage.isNull()) {
        // KeepAspectRatioByExpanding: 위젯을 꽉 채우도록 확대하면서 잘라내는(cover-fit) 방식.
        QPixmap scaled = bgImage.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        painter.drawPixmap(topLeft, scaled);

        // 사진 위에 어두운 반투명 오버레이를 얹어 카드 밖 배경도 다크 테마 톤에 맞추고,
        // 사진이 너무 밝을 때 로그인 카드 대비가 떨어지지 않도록 한다.
        painter.fillRect(rect(), QColor(10, 10, 18, 150));
    }
}

void LoginPage::onLoginClicked()
{
    if (idEdit->text() == kValidId && pwEdit->text() == kValidPw) {
        errorLabel->clear();
        QSettings settings;
        settings.setValue("autoLogin", autoLoginCheck->isChecked());
        settings.setValue("lastId", autoLoginCheck->isChecked() ? idEdit->text() : QString());
        emit loginSucceeded();
        return;
    }
    errorLabel->setText("아이디 또는 비밀번호가 올바르지 않습니다.");
}
