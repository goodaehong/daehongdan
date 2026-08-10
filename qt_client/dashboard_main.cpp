#include "src/pages/LoginPage.h"
#include "src/core/MainWindow.h"

#include <QApplication>

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
    showLogin();
    return QCoreApplication::exec();
}
