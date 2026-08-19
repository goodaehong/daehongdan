#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>
#include <QPixmap>

class QLineEdit;
class QLabel;
class QCheckBox;
class QPushButton;
class QTimer;

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

signals:
    void loginSucceeded();

protected:
    // 배경 사진 위 다크 톤 베이스만 채운다. 사진 자체(블러 포함)는 bgLabel이 그린다.
    void paintEvent(QPaintEvent *event) override;
    // 고정 크기 창이라 최초 1회만 사실상 의미 있지만, bgLabel 크기/블러 사진을 맞춘다.
    void resizeEvent(QResizeEvent *event) override;
    // 로그아웃 후 재표시 등 두 번째 show()부터는 창 관리자가 생성자에서 잡아둔
    // move()/setFixedSize() 지시를 무시하고 이전 창 크기로 되돌리는 경우가 있어,
    // 보여질 때마다 화면 전체 크기로 다시 강제한다.
    void showEvent(QShowEvent *event) override;

private slots:
    void onLoginClicked();
    void onLockoutTick();

private:
    void applyFullScreenSize();
    void completeLogin();
    void onLoginFailed();
    void startLockout();
    void updateLockoutMessage();
    void setFormEnabled(bool enabled);

    QLineEdit *idEdit;
    QLineEdit *pwEdit;
    QLabel *errorLabel;
    QPushButton *loginButton;
    QLabel *bgLabel;
    // 체크된 상태로 로그인하면 QSettings에 저장되고, 다음 실행부터는 이 화면 자체를 건너뛴다
    // (dashboard_main.cpp의 main()이 시작 시 확인함).
    QCheckBox *autoLoginCheck;
    QPixmap bgImage;

    // 브루트포스 방지용 로그인 시도 제한 (팀 결정 #7). 재시작하면 초기화되는 세션 한정 카운터.
    int failedAttempts = 0;
    QTimer *lockoutTimer = nullptr;
    int lockoutSecondsRemaining = 0;
};

#endif // LOGINPAGE_H
