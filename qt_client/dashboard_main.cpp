#include "loginwindow.h"
#include "mainwindow.h"

#include <QApplication>

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
    auto *login = new LoginWindow;
    QObject::connect(login, &LoginWindow::loginSucceeded, login, [login]() {
        showDashboard();
        login->close();
        login->deleteLater();
    });
    login->show();
}
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    showLogin();
    return QCoreApplication::exec();
}
