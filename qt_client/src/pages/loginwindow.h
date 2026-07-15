#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>

class QLineEdit;
class QLabel;

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

signals:
    void loginSucceeded();

private slots:
    void onLoginClicked();

private:
    QLineEdit *idEdit;
    QLineEdit *pwEdit;
    QLabel *errorLabel;
};

#endif // LOGINWINDOW_H
