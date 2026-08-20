#include "HelpPage.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QStackedWidget>
#include <QPushButton>
#include <QTimer>
#include <QPair>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";
const QString kSafeColor = "#34d399";
const QString kWarnColor = "#fbbf24";
const QString kDangerColor = "#f87171";

struct HelpTopic {
    QString title;
    QString html;
};

// 실제 화면 동작을 그대로 옮겨 적은 내용 — 코드가 바뀌면 이 문구도 같이 손봐야 한다.
QList<HelpTopic> buildTopics()
{
    return {
        { "종합 상태 색상",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>종합 상태 3색 표시</h3>"
          "<p style='margin:0 0 14px 0;'>모든 화면 상단과 구역 카드는 아래 3가지 색으로 현재 상태를 표시합니다.</p>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'><span style='color:" + kSafeColor + ";'>●</span> <b>안전</b> — 모든 센서 정상, 별도 대응 불필요</li>"
          "<li style='margin-bottom:10px;'><span style='color:" + kWarnColor + ";'>●</span> <b>경고</b> — 임계치 근접, 카운트다운 동안 관리자 확인 대기</li>"
          "<li style='margin-bottom:10px;'><span style='color:" + kDangerColor + ";'>●</span> <b>위험</b> — 임계치 초과 또는 경고 대응시간 초과, 자동 제어 및 대응 필요</li>"
          "</ul>"
          "<p style='margin:0 0 14px 0;'>경고 상태에서 카운트다운이 끝나기 전에 확인하지 않으면 자동으로 위험 상태로 전환됩니다.</p>" },

        { "모니터링 탭",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>모니터링 탭</h3>"
          "<p style='margin:0 0 14px 0;'>상단 구역 버튼으로 구역을 전환하며, 실시간 카메라 영상과 센서 수치, 제어 버튼을 확인합니다.</p>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>카메라 화면 우측 상단의 ROI 버튼으로 감지 제외구역을 설정할 수 있습니다. 화면을 드래그하면 사각형이 추가되고,"
          " 사각형 위 × 배지 클릭 또는 우클릭으로 삭제합니다.</li>"
          "<li style='margin-bottom:10px;'>연기 수치 옆에는 최근 추세(▲ 상승 / ▼ 하강 / ● 안정)가 함께 표시됩니다.</li>"
          "<li style='margin-bottom:10px;'>환기팬·밸브·사이렌은 수동 제어 버튼으로 직접 켜고 끌 수 있으며, 오작동 방지를 위해 확인 팝업이 뜹니다.</li>"
          "</ul>" },

        { "위험 모드 대응",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>위험 모드 진입 시</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>위험 상태로 전환되면 원인 선택 모달이 뜹니다. 실제 원인을 선택하면 그에 맞는 대응 체크리스트가 표시됩니다.</li>"
          "<li style='margin-bottom:10px;'>상단 배너와 화면 가장자리 글로우로 위험 구역이 있음을 항상 표시하며, 배너 클릭 시 해당 구역 모니터링으로 바로 이동합니다.</li>"
          "<li style='margin-bottom:10px;'><b>수동 전환</b> / <b>수동 해제</b> 버튼으로 관리자가 직접 위험 모드를 켜거나 끌 수 있습니다.</li>"
          "<li style='margin-bottom:10px;'>자동 제어(환기팬/밸브 가동 등)가 실패하거나 시간이 걸리는 경우 <b>재시도</b> 버튼이 깜빡이며 표시됩니다.</li>"
          "<li style='margin-bottom:10px;'>스피커로 나오는 대피 안내 음성은 하단 <b>스피커 끄기</b> 버튼으로 수동으로 멈출 수 있습니다.</li>"
          "</ul>" },

        { "센서·장치 연결 상태",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>연결 상태 배지</h3>"
          "<p style='margin:0 0 14px 0;'>구역 카드마다 센서·장치 종류별로 연결 배지가 따로 표시됩니다.</p>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>가스·화염·연기 센서 배지 — 신호 수신 여부로 정상/오류 표시</li>"
          "<li style='margin-bottom:10px;'>온습도(DHT22) 배지 — 별도 표시. 이 센서는 하드웨어 특성상 2초 간격으로만 값이 갱신되므로,"
          " 화면 수치가 잠깐 그대로여도 정상입니다.</li>"
          "<li style='margin-bottom:10px;'>액추에이터(팬/밸브/사이렌) 상태가 명령과 다르게 응답하면 <span style='color:" + kDangerColor + ";'>!</span> 표시로 구분됩니다.</li>"
          "<li style='margin-bottom:10px;'>상단 연결 배지는 서버 소켓 연결 여부와 센서 데이터 수신 흐름을 함께 보고 3단계로 표시합니다"
          " — TCP는 붙어 있어도 서버 내부가 멎으면 데이터가 끊긴 것으로 표시됩니다.</li>"
          "</ul>" },

        { "이벤트로그 탭",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>이벤트로그 탭</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>상태 전환, 수동 제어 결과, 위험 대응 결과가 발생할 때마다 자동으로 새로고침됩니다.</li>"
          "<li style='margin-bottom:10px;'>날짜를 지정하면 해당 날짜, 지정하지 않으면 최근 24시간 기록을 조회합니다.</li>"
          "<li style='margin-bottom:10px;'>제어 대상·결과 문구는 한글로 표시됩니다 (예: 알 수 없는 제어 대상 → 대상 오류 등).</li>"
          "</ul>" },

        { "그래프 탭",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>그래프 탭</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>기간을 하루/1주/1달 중에서 선택할 수 있으며, '하루'는 오늘 0시부터 현재까지입니다.</li>"
          "<li style='margin-bottom:10px;'>화면을 보고 있는 동안 1분마다 자동으로 최신 데이터가 반영됩니다.</li>"
          "<li style='margin-bottom:10px;'>경고·위험이 발생했던 시점은 그래프 위에 세로선으로 표시됩니다"
          " (<span style='color:" + kWarnColor + ";'>노랑=경고</span> / <span style='color:" + kDangerColor + ";'>빨강=위험</span>)."
          " 세로선 위에 마우스를 올리면 그 시각과 발생 원인이 표시됩니다.</li>"
          "</ul>" },

        { "평면도 탭",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>평면도 탭</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>우측 상단 <b>지도 재설정</b> 버튼으로 평면도 원본 이미지를 등록합니다 (6MB 이하 PNG/JPG).</li>"
          "<li style='margin-bottom:10px;'>등록하면 대피 경로 지도가 표시되고, 전광판(주황) 위치를 클릭하면 해당 전광판에서 각 출구까지의 대피 경로가 그려집니다.</li>"
          "<li style='margin-bottom:10px;'>평면도가 아직 등록되지 않은 동안에는 탭 이름 앞에 ❗ 표시가 붙고, 탭에 들어가면 상단에도 등록 안내 배너가 표시됩니다.</li>"
          "</ul>" },

        { "문의처",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>문의처</h3>"
          "<p style='margin:0 0 14px 0;'>화면 사용 중 문제가 있거나 문의사항이 있으면 보안관제팀 내선 1234로 연락 주세요.</p>" },
    };
}

// 미리보기 패널 공통 카드 틀 — 실제 화면 요소를 코드로 재현해서 UI가 바뀌어도 같이 갱신되게 한다.
QWidget *makePreviewCard(QWidget *parent, const QString &caption)
{
    auto *card = new QWidget(parent);
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    auto *box = new QWidget(card);
    box->setStyleSheet(QString("background-color:#0d0d16; border:1px solid %1; border-radius:8px;").arg(kCardBorder));
    box->setMinimumHeight(84);
    outer->addWidget(box);

    auto *label = new QLabel(caption, card);
    label->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    outer->addWidget(label);

    return card;
}

QWidget *previewInner(QWidget *card)
{
    return card->layout()->itemAt(0)->widget();
}

QWidget *buildColorPreview(QWidget *parent)
{
    auto *card = makePreviewCard(parent, "미리보기 — 종합 상태 배지(실제 색상 그대로)");
    auto *row = new QHBoxLayout(previewInner(card));
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(12);
    const QList<QPair<QString, QString>> pills = {
        { "● 안전", kSafeColor }, { "● 경고", kWarnColor }, { "● 위험", kDangerColor }
    };
    for (const auto &p : pills) {
        auto *pill = new QLabel(p.first, previewInner(card));
        pill->setStyleSheet(QString(
            "color:%1; border:1px solid %1; border-radius:12px; padding:6px 14px; font-size:14px; font-weight:bold;")
            .arg(p.second));
        row->addWidget(pill);
    }
    row->addStretch();
    return card;
}

QWidget *buildTrendPreview(QWidget *parent)
{
    auto *card = makePreviewCard(parent, "미리보기 — 연기 추세 표시(카메라 화면·ROI 편집은 실제 캡처 예정)");
    auto *row = new QHBoxLayout(previewInner(card));
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(20);
    const QList<QPair<QString, QString>> trends = {
        { "▲ 상승", kWarnColor }, { "▼ 하강", kSafeColor }, { "● 안정", kSafeColor }
    };
    for (const auto &t : trends) {
        auto *lbl = new QLabel(t.first, previewInner(card));
        lbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; border:none;").arg(t.second));
        row->addWidget(lbl);
    }
    row->addStretch();
    return card;
}

QWidget *buildBadgePreview(QWidget *parent)
{
    auto *card = makePreviewCard(parent, "미리보기 — 연결 배지 3단계");
    auto *row = new QHBoxLayout(previewInner(card));
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(20);
    const QList<QPair<QString, QString>> badges = {
        { "🟢 연결됨", kSafeColor }, { "🟡 온습도 불안정", kWarnColor }, { "🔴 센서 오류", kDangerColor }
    };
    for (const auto &b : badges) {
        auto *lbl = new QLabel(b.first, previewInner(card));
        lbl->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(b.second));
        row->addWidget(lbl);
    }
    auto *mismatch = new QLabel("!  미반영", previewInner(card));
    mismatch->setStyleSheet(QString("color:%1; font-size:14px; font-weight:bold; border:none;").arg(kDangerColor));
    row->addWidget(mismatch);
    row->addStretch();
    return card;
}

QWidget *buildPlaceholderPreview(QWidget *parent, const QString &note)
{
    auto *card = makePreviewCard(parent, note);
    auto *inner = previewInner(card);
    inner->setStyleSheet(inner->styleSheet() + " border-style:dashed;");
    auto *centerLayout = new QVBoxLayout(inner);
    auto *icon = new QLabel("🖼  실제 화면 캡처 예정", inner);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    centerLayout->addWidget(icon);
    return card;
}

QWidget *buildContactPreview(QWidget *parent)
{
    auto *card = makePreviewCard(parent, "문의처");
    auto *row = new QHBoxLayout(previewInner(card));
    row->setContentsMargins(16, 16, 16, 16);
    auto *lbl = new QLabel("📞  보안관제팀 내선 1234", previewInner(card));
    lbl->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; border:none;").arg(kTextPrimary));
    row->addWidget(lbl);
    row->addStretch();
    return card;
}
}

HelpPage::HelpPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel("도움말", this);
    title->setStyleSheet(QString("color:%1; font-size:24px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    layout->addWidget(title);

    auto *body = new QHBoxLayout;
    body->setSpacing(16);

    topicList = new QListWidget(this);
    topicList->setFixedWidth(200);
    topicList->setStyleSheet(QString(
        "QListWidget { background-color:%1; border:1px solid %2; border-radius:10px; color:%3; padding:6px; outline:none; font-size:16px; }"
        "QListWidget::item { padding:12px 10px; border-radius:6px; }"
        "QListWidget::item:selected { background-color:%4; color:white; }"
        "QListWidget::item:hover:!selected { background-color:#1c1c2a; }")
        .arg(kCardBg, kCardBorder, kTextSecondary, kAccent));
    for (const HelpTopic &topic : buildTopics())
        topicList->addItem(topic.title);
    body->addWidget(topicList);

    auto *rightCol = new QVBoxLayout;
    rightCol->setSpacing(12);

    contentView = new QTextBrowser(this);
    contentView->setOpenExternalLinks(false);
    contentView->setStyleSheet(QString(
        "QTextBrowser { background-color:%1; border:1px solid %2; border-radius:10px; padding:16px; color:%3; font-size:16px; }")
        .arg(kCardBg, kCardBorder, kTextPrimary));
    rightCol->addWidget(contentView, 1);

    // 텍스트 아래 미리보기 — 실제 배지/버튼 스타일을 코드로 재현해서 항상 실제 화면과 같은 색·문구를 보여준다.
    previewStack = new QStackedWidget(this);
    previewStack->addWidget(buildColorPreview(previewStack));                                    // 0 종합 상태 색상
    previewStack->addWidget(buildTrendPreview(previewStack));                                     // 1 모니터링 탭
    previewStack->addWidget(buildEmergencyButtonPreview(previewStack));                           // 2 위험 모드 대응
    previewStack->addWidget(buildBadgePreview(previewStack));                                     // 3 센서·장치 연결 상태
    previewStack->addWidget(buildPlaceholderPreview(previewStack, "미리보기 — 이벤트로그 테이블")); // 4
    previewStack->addWidget(buildPlaceholderPreview(previewStack, "미리보기 — 그래프 화면"));       // 5
    previewStack->addWidget(buildPlaceholderPreview(previewStack, "미리보기 — 평면도·대피경로 화면")); // 6
    previewStack->addWidget(buildContactPreview(previewStack));                                   // 7 문의처
    rightCol->addWidget(previewStack);

    body->addLayout(rightCol, 1);

    layout->addLayout(body, 1);

    connect(topicList, &QListWidget::currentRowChanged, this, &HelpPage::showTopic);
    topicList->setCurrentRow(0);
}

QWidget *HelpPage::buildEmergencyButtonPreview(QWidget *parent)
{
    auto *card = makePreviewCard(parent, "미리보기 — 위험 모드 버튼(재시도 버튼은 실제와 동일하게 깜빡임)");
    auto *row = new QHBoxLayout(previewInner(card));
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(10);

    auto *triggerBtn = new QLabel("위험 모드 전환", previewInner(card));
    triggerBtn->setAlignment(Qt::AlignCenter);
    triggerBtn->setStyleSheet(
        "background-color:#ef4444; color:white; border-radius:6px; padding:8px 14px; font-size:13px; font-weight:bold;");
    row->addWidget(triggerBtn);

    auto *clearBtn = new QLabel("수동 해제", previewInner(card));
    clearBtn->setAlignment(Qt::AlignCenter);
    clearBtn->setStyleSheet(
        "background-color:#3a3550; color:#8d87a0; border-radius:6px; padding:8px 14px; font-size:13px; font-weight:bold;");
    row->addWidget(clearBtn);

    previewRetryButton = new QLabel("⟳ 대응 재실행", previewInner(card));
    previewRetryButton->setAlignment(Qt::AlignCenter);
    previewRetryButton->setStyleSheet(
        "background-color:#f59e0b; color:#241c00; border-radius:6px; padding:8px 14px; font-size:13px; font-weight:bold;");
    row->addWidget(previewRetryButton);
    row->addStretch();

    retryBlinkTimer = new QTimer(this);
    connect(retryBlinkTimer, &QTimer::timeout, this, [this]() {
        if (!previewRetryButton)
            return;
        retryBlinkOn = !retryBlinkOn;
        const QString bg = retryBlinkOn ? "#f59e0b" : "#7c4a08";
        previewRetryButton->setStyleSheet(QString(
            "background-color:%1; color:#241c00; border-radius:6px; padding:8px 14px; font-size:13px; font-weight:bold;").arg(bg));
    });

    return card;
}

void HelpPage::showTopic(int index)
{
    const QList<HelpTopic> topics = buildTopics();
    if (index < 0 || index >= topics.size())
        return;
    contentView->setHtml(QString("<div style='color:%1; font-size:16px; line-height:2.0;'>%2</div>").arg(kTextSecondary, topics[index].html));

    if (previewStack && index < previewStack->count())
        previewStack->setCurrentIndex(index);

    // 재시도 버튼 깜빡임은 해당 주제를 보고 있을 때만 돈다.
    if (retryBlinkTimer)
        (index == 2) ? retryBlinkTimer->start(600) : retryBlinkTimer->stop();
}
