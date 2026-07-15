#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

class QLineEdit;
class QLabel;

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

signals:
    void loginSucceeded();

private slots:
    void onLoginClicked();

private:
    QLineEdit *idEdit;
    QLineEdit *pwEdit;
    QLabel *errorLabel;
};

#endif // LOGINPAGE_H
