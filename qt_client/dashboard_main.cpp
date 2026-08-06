#include "src/pages/LoginPage.h"
#include "src/core/MainWindow.h"

#include <QApplication>
#include <QSettings>

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
    dashboard->show();
}

void showLogin()
{
    auto *login = new LoginPage;
    QObject::connect(login, &LoginPage::loginSucceeded, login, [login]() {
        showDashboard();
        login->close();
        login->deleteLater();
    });
    login->show();
}
}

int main(int argc, char *argv[])
{
    qputenv("VLC_PLUGIN_PATH", VLC_PLUGIN_PATH);

    QApplication a(argc, argv);
    // QSettings가 저장 위치를 잡으려면 조직/앱 이름이 있어야 한다(레지스트리 HKEY_CURRENT_USER 경로 등).
    QCoreApplication::setOrganizationName("daehongdan");
    QCoreApplication::setApplicationName("gas_fire_dashboard");

    // 지난번에 "자동 로그인"을 체크하고 로그인했으면 로그인 화면 자체를 건너뛴다.
    QSettings settings;
    if (settings.value("autoLogin", false).toBool())
        showDashboard();
    else
        showLogin();
    return QCoreApplication::exec();
}
