#include "LoginPage.h"
#include "../core/ServerConfig.h"

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
#include <QResizeEvent>
#include <QShowEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsBlurEffect>
#include <QSslSocket>
#include <QSslError>
#include <QAbstractSocket>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QPainterPath>

namespace {
const QString kValidId = "admin";
const QString kValidPw = "1234";

const QString kBg = "#0a0a12";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";

// 브랜드 마크: 공장 실루엣(굴뚝 + 톱니 지붕)을 브랜드 그라디언트로 직접 그린 아이콘.
// 예전엔 그라디언트 박스 위에 "🏭" 이모지를 얹었는데 OS/폰트마다 렌더링이 달라서 벡터로 바꿨고,
// 박스 배경도 빼서 아이콘 형태만 남긴다. 창문은 색을 덧칠하는 대신 path에서 빼내 실제로 뚫어서
// 뒤 배경이 무엇이든(카드 위/사진 위) 그대로 비치게 한다.
class BrandMarkWidget : public QWidget
{
public:
    explicit BrandMarkWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        // 부모 카드의 스타일시트(배경/테두리/라운드)가 자식에게도 상속되므로 명시적으로 끈다.
        setStyleSheet("background:transparent; border:none;");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        const auto px = [&r](qreal fx) { return r.left() + r.width() * fx; };
        const auto py = [&r](qreal fy) { return r.top() + r.height() * fy; };

        // 왼쪽 굴뚝에서 시작해 톱니 지붕(/|/|)을 지나 본체 오른쪽 벽으로 내려오는 한 붓 그리기.
        QPainterPath factory;
        factory.moveTo(px(0.08), py(0.92));   // 좌하단
        factory.lineTo(px(0.08), py(0.14));   // 굴뚝 왼쪽 벽
        factory.lineTo(px(0.26), py(0.14));   // 굴뚝 상단
        factory.lineTo(px(0.26), py(0.60));   // 굴뚝 오른쪽 -> 지붕 골
        factory.lineTo(px(0.53), py(0.34));   // 톱니 1 (사선 상승)
        factory.lineTo(px(0.53), py(0.60));   // 톱니 1 수직 하강
        factory.lineTo(px(0.81), py(0.34));   // 톱니 2 (사선 상승)
        factory.lineTo(px(0.81), py(0.92));   // 본체 오른쪽 벽
        factory.closeSubpath();

        // 창문 3개 + 굴뚝 띠 — 실제로 뚫는다.
        QPainterPath cutouts;
        for (int i = 0; i < 3; ++i)
            cutouts.addRect(QRectF(px(0.34 + i * 0.16), py(0.70), r.width() * 0.09, r.height() * 0.11));
        cutouts.addRect(QRectF(px(0.10), py(0.24), r.width() * 0.14, r.height() * 0.05));

        QLinearGradient grad(r.topLeft(), r.bottomRight());
        grad.setColorAt(0, QColor("#6ee7d8"));
        grad.setColorAt(1, QColor("#8b7cf6"));
        p.setPen(Qt::NoPen);
        p.fillPath(factory.subtracted(cutouts), grad);
    }
};

// 서버 주소는 ServerConfig.h(MainWindow와 공유)를 그대로 쓴다 — 여기서 따로 상수를 들고 있다가
// MainWindow 쪽만 갱신되면서 어긋난 적이 있어(로그인 화면 오탐 원인) 단일 출처로 합쳤다.
// 로그인 단계에서는 실제 데이터 프로토콜을 쓸 필요 없이 TCP 연결 성공 여부만 확인하면 된다.

constexpr int kMaxLoginAttempts = 5;      // 결정 #7: 로그인 제한 있음 (브루트포스 대비)
constexpr int kLockoutSeconds = 30;
constexpr int kServerCheckTimeoutMs = 3000;
}

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("SafeVision - 지능형 통합 화재 관제 시스템");
    // 결정 #8 갱신: 고정 크기 자체는 유지하되, 기준을 임의 사이즈가 아니라 화면 전체(작업표시줄
    // 제외 가용 영역)로 잡는다. 타이틀바는 그대로 두므로 완전 프레임리스 풀스크린은 아니다.
    applyFullScreenSize();
    bgImage = QPixmap(":/login_bg.jpg");

    // 배경 사진은 카드 아래로 깔리는 별도 QLabel로 분리해서 QGraphicsBlurEffect(결정 #4)를
    // 카드 위젯까지 같이 흐려지지 않게 이 라벨에만 건다. paintEvent에서 직접 그리면
    // 이 위젯 전체(카드 포함)가 한 장으로 합성돼서 부분 블러가 불가능하다.
    bgLabel = new QLabel(this);
    bgLabel->setGeometry(rect());
    bgLabel->lower();
    auto *bgBlur = new QGraphicsBlurEffect(bgLabel);
    bgBlur->setBlurRadius(8);   // 블러 약하게
    bgLabel->setGraphicsEffect(bgBlur);

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

    auto *icon = new BrandMarkWidget(box);
    icon->setFixedSize(56, 56);
    boxLayout->addWidget(icon);
    boxLayout->addSpacing(6);

    auto *title = new QLabel("SafeVision", box);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("color:%1; font-size:40px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextPrimary));
    boxLayout->addWidget(title);

    auto *subtitle = new QLabel("지능형 통합 화재 관제 시스템", box);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QString("color:%1; font-size:16px; border:none;").arg(kTextSecondary));
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

    // 결정 #3: 회원가입/비밀번호 찾기 둘 다 미구현 — 계정은 관리자가 사전 발급하므로 링크 자체를 뺀다.
    autoLoginCheck = new QCheckBox("자동 로그인", box);
    autoLoginCheck->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    boxLayout->addWidget(autoLoginCheck);
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

    loginButton = new QPushButton("로그인", box);
    loginButton->setMinimumHeight(40);
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setStyleSheet(QString(
        "QPushButton { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 #a78bfa);"
        "color:white; font-weight:bold; font-family:\"hanwhaGothic EL\"; border-radius:8px; }"
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

void LoginPage::applyFullScreenSize()
{
    const QRect screenGeo = QGuiApplication::primaryScreen()->availableGeometry();
    setFixedSize(screenGeo.size());
    move(screenGeo.topLeft());
}

void LoginPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 로그아웃 후 재표시 시 창 관리자가 이전(생성자 시점) 지시를 무시하고 기본 크기로
    // 되돌리는 경우가 있어, 실제로 화면에 뜰 때마다 한 번 더 강제 적용한다.
    applyFullScreenSize();
}

void LoginPage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // 사진(+블러)은 bgLabel이 그린다. 여기서는 사진이 없거나 라벨 밖 여백을 위한 다크 베이스만 채운다.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(kBg));
}

void LoginPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    bgLabel->setGeometry(rect());

    if (bgImage.isNull() || rect().isEmpty())
        return;

    // KeepAspectRatioByExpanding: 위젯을 꽉 채우도록 확대하면서 잘라내는(cover-fit) 방식.
    QPixmap scaled = bgImage.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap composed(size());
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    painter.drawPixmap(topLeft, scaled);
    // 사진 위에 어두운 반투명 오버레이를 얹어 카드 밖 배경도 다크 테마 톤에 맞추고,
    // 사진이 너무 밝을 때 로그인 카드 대비가 떨어지지 않도록 한다(결정 #4). 알파를 낮춰서
    // 사진이 좀 더 비치도록(불투명도 낮춤) 조정.
    painter.fillRect(composed.rect(), QColor(10, 10, 18, 100));
    painter.end();
    bgLabel->setPixmap(composed);
}

void LoginPage::setFormEnabled(bool enabled)
{
    idEdit->setEnabled(enabled);
    pwEdit->setEnabled(enabled);
    loginButton->setEnabled(enabled);
}

void LoginPage::onLoginClicked()
{
    if (!loginButton->isEnabled())
        return; // 서버 확인 중이거나 잠금 상태 — 무시

    if (idEdit->text() != kValidId || pwEdit->text() != kValidPw) {
        onLoginFailed();
        return;
    }

    // 결정 #9: 아이디/비번은 맞아도 서버가 안 잡히면 대시보드가 무용지물이라 여기서 먼저 확인한다.
    // 상시 연결 상태 배지(안건 9-b)는 채택 안 함 — 로그인 시도 시에만 짧게 확인.
    errorLabel->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    errorLabel->setText("서버 연결 확인 중...");
    setFormEnabled(false);

    // 단순 TCP 연결 여부가 아니라 TLS 핸드셰이크(암호화)까지 성공하는지 확인한다 — 포트는 열려
    // 있어도 인증서가 안 맞으면 실제 데이터 소켓(ServerLink)도 못 붙기 때문에, 여기서도 같은
    // 검증(ServerConfig::verifyServerCertificate)을 거쳐야 사전 확인이 의미가 있다.
    auto *socket = new QSslSocket(this);
    auto *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);

    auto finish = [this, socket, timeoutTimer](bool connected) {
        socket->disconnect(this);
        timeoutTimer->stop();
        timeoutTimer->deleteLater();
        socket->abort();
        socket->deleteLater();
        if (connected) {
            completeLogin();
        } else {
            setFormEnabled(true);
            errorLabel->setStyleSheet("color:#f87171; border:none;");
            errorLabel->setText("서버에 연결할 수 없습니다.");
        }
    };

    // ServerLink와 마찬가지로 서버가 실제 TLS를 켜기 전까지는 평문으로 확인한다
    // (ServerConfig::kUseTls 하나로 양쪽 다 같이 전환됨).
    if (ServerConfig::kUseTls) {
        connect(socket, &QSslSocket::encrypted, this, [finish]() { finish(true); });
        connect(socket, &QSslSocket::sslErrors, this, [socket](const QList<QSslError> &errors) {
            ServerConfig::verifyServerCertificate(socket, errors);
        });
    } else {
        connect(socket, &QAbstractSocket::connected, this, [finish]() { finish(true); });
    }
    connect(socket, &QSslSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError) { finish(false); });
    connect(timeoutTimer, &QTimer::timeout, this, [finish]() { finish(false); });

    timeoutTimer->start(kServerCheckTimeoutMs);
    if (ServerConfig::kUseTls)
        socket->connectToHostEncrypted(ServerConfig::kServerHost, ServerConfig::kServerPort);
    else
        socket->connectToHost(ServerConfig::kServerHost, ServerConfig::kServerPort);
}

void LoginPage::completeLogin()
{
    errorLabel->clear();
    failedAttempts = 0;
    QSettings settings;
    settings.setValue("autoLogin", autoLoginCheck->isChecked());
    settings.setValue("lastId", autoLoginCheck->isChecked() ? idEdit->text() : QString());
    emit loginSucceeded();
}

void LoginPage::onLoginFailed()
{
    // 결정 #6: 통합 문구 — 아이디/비번 중 뭐가 틀렸는지 구분 안 함.
    ++failedAttempts;
    if (failedAttempts >= kMaxLoginAttempts) {
        startLockout();
        return;
    }
    errorLabel->setStyleSheet("color:#f87171; border:none;");
    errorLabel->setText("아이디 또는 비밀번호가 올바르지 않습니다.");
}

void LoginPage::startLockout()
{
    // 결정 #7: 로그인 시도 제한. 세션(앱 실행) 한정 카운터 — 재시작하면 풀린다.
    lockoutSecondsRemaining = kLockoutSeconds;
    setFormEnabled(false);
    updateLockoutMessage();

    if (!lockoutTimer) {
        lockoutTimer = new QTimer(this);
        connect(lockoutTimer, &QTimer::timeout, this, &LoginPage::onLockoutTick);
    }
    lockoutTimer->start(1000);
}

void LoginPage::onLockoutTick()
{
    --lockoutSecondsRemaining;
    if (lockoutSecondsRemaining <= 0) {
        lockoutTimer->stop();
        failedAttempts = 0;
        setFormEnabled(true);
        errorLabel->clear();
        return;
    }
    updateLockoutMessage();
}

void LoginPage::updateLockoutMessage()
{
    errorLabel->setStyleSheet("color:#f87171; border:none;");
    errorLabel->setText(QString("로그인 시도 횟수를 초과했습니다. %1초 후 다시 시도하세요.").arg(lockoutSecondsRemaining));
}
