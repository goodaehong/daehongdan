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
    login->showMaximized();
}
}

int main(int argc, char *argv[])
{
    qputenv("VLC_PLUGIN_PATH", VLC_PLUGIN_PATH);

    QApplication a(argc, argv);
    // QSettings가 저장 위치를 잡으려면 조직/앱 이름이 있어야 한다(레지스트리 HKEY_CURRENT_USER 경로 등).
    QCoreApplication::setOrganizationName("daehongdan");
    QCoreApplication::setApplicationName("gas_fire_dashboard");

    // 한화고딕 폰트를 앱 전체 기본 폰트로 적용. 로드 실패해도(예: 리소스 누락) 시스템 기본
    // 폰트로 자연스럽게 대체되도록 family를 못 얻으면 setFont()를 아예 건너뛴다.
    const int fontId = QFontDatabase::addApplicationFont(":/fonts/HanwhaGothicL.ttf");
    if (fontId != -1) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
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
