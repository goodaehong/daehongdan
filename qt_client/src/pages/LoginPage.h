#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

class QLineEdit;
class QLabel;
class QCheckBox;

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
    // 체크된 상태로 로그인하면 QSettings에 저장되고, 다음 실행부터는 이 화면 자체를 건너뛴다
    // (dashboard_main.cpp의 main()이 시작 시 확인함).
    QCheckBox *autoLoginCheck;
};

#endif // LOGINPAGE_H
