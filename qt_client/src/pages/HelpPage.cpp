#include "HelpPage.h"

#include <QVBoxLayout>
#include <QLabel>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
}

HelpPage::HelpPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *title = new QLabel("도움말", this);
    title->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(title);

    const QStringList bullets = {
        "· 종합 상태는 신호등식 3색 인디케이터(안전/경고/위험)로 표시됩니다.",
        "· 경고 단계에서 일정 시간 관리자 대응 시 자동 제어로 전환됩니다.",
        "· 수동 제어 버튼은 오작동 방지를 위해 확인 팝업이 동반됩니다.",
        "· 문의: 보안관제팀 내선 1234",
    };
    for (const QString &b : bullets) {
        auto *label = new QLabel(b, this);
        label->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
        layout->addWidget(label);
    }
    layout->addStretch();
}
