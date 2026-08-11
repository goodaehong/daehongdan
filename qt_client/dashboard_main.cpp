#include "src/pages/LoginPage.h"
#include "src/core/MainWindow.h"

#include <QApplication>
#include <QSettings>
#include <QFontDatabase>
#include <QFont>

#ifndef VLC_PLUGIN_PATH
#define VLC_PLUGIN_PATH ""
#endif

namespace {
void showLogin();

void showDashboard()
{
    auto *dashboard = new MainWindow;
    QObject::connect(dashboard, &MainWindow::loggedOut, dashboard, [dashboard]() {
        dashboard->deleteLater();
        showLogin();
    });
    dashboard->showMaximized();
}

void showLogin()
{
    auto *login = new LoginPage;
    QObject::connect(login, &LoginPage::loginSucceeded, login, [login]() {
        showDashboard();
        login->close();
        login->deleteLater();
    });
    login->show();   // 결정 #8: 창 크기 고정이라 최대화 없이 고정 크기로 띄운다
}
}

int main(int argc, char *argv[])
{
    qputenv("VLC_PLUGIN_PATH", VLC_PLUGIN_PATH);

    QApplication a(argc, argv);
    // QSettings가 저장 위치를 잡으려면 조직/앱 이름이 있어야 한다(레지스트리 HKEY_CURRENT_USER 경로 등).
    QCoreApplication::setOrganizationName("daehongdan");
    QCoreApplication::setApplicationName("gas_fire_dashboard");

    // 한화고딕 두 굵기를 등록한다. L(기본)을 앱 전체 기본 폰트로 적용하고, EL은 각 스타일시트가
    // "font-weight:bold"와 함께 font-family로 직접 지정해서 쓴다 — 이 두 TTF는 서로 다른 family로
    // 등록돼 있어(같은 패밀리의 weight variant가 아님) Qt가 bold 요청만으로 자동으로 EL을 골라주지
    // 않기 때문에, 굵게 써야 할 자리마다 명시적으로 family를 지정해야 한다.
    // 로드 실패해도(예: 리소스 누락) 시스템 기본 폰트로 자연스럽게 대체되도록 family를 못 얻으면
    // setFont()를 아예 건너뛴다.
    const int regularFontId = QFontDatabase::addApplicationFont(":/fonts/06HanwhaGothicL.ttf");
    QFontDatabase::addApplicationFont(":/fonts/07HanwhaGothicEL.ttf");
    if (regularFontId != -1) {
        const QStringList families = QFontDatabase::applicationFontFamilies(regularFontId);
        if (!families.isEmpty())
            QApplication::setFont(QFont(families.first()));
    }

    // 다크 테마인데 QToolTip은 OS 기본(밝은 배경+검정 글씨)을 써서 안 보이던 문제 — 앱 전체에
    // 한 번에 적용해서 setToolTip() 쓰는 곳마다 따로 안 고쳐도 되게 한다.
    a.setStyleSheet(
        "QToolTip {"
        "  background-color: #232333;"
        "  color: #f5f5fa;"
        "  border: 1px solid #3a3550;"
        "  padding: 6px 10px;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "}");

    // 지난번에 "자동 로그인"을 체크하고 로그인했으면 로그인 화면 자체를 건너뛴다.
    QSettings settings;
    if (settings.value("autoLogin", false).toBool())
        showDashboard();
    else
        showLogin();
    return QCoreApplication::exec();
}
