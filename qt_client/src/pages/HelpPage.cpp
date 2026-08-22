#include "HelpPage.h"
#include "../widgets/FloorMapGridWidget.h"
#include "../core/FloorMapTypes.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QStackedWidget>
#include <QPushButton>
#include <QTimer>
#include <QPair>
#include <QPixmap>

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
          "<p style='margin:0 0 14px 0;'>경고 상태에서 카운트다운이 끝나기 전에 확인하지 않으면 자동으로 위험 상태로 전환됩니다.</p>"
          "<h3 style='color:#f5f5fa; font-size:16px; font-weight:700; margin:18px 0 10px 0;'>가스·화재·연기 감지 종류별 경고/위험 기준</h3>"
          "<p style='margin:0 0 10px 0; color:" + kTextSecondary + ";'>카메라 영상 감지와 센서 감지는 따로 판정되고, 조합에 따라 경고/위험이 갈립니다"
          " (server/judgement.cpp 판단 매트릭스 기준).</p>"
          "<p style='margin:0 0 6px 0;'><span style='color:" + kWarnColor + ";'>● 경고</span> — 영상 또는 센서 <b>하나만</b> 단독으로 감지됐을 때</p>"
          "<ul style='margin:0 0 14px 0; padding-left:22px;'>"
          "<li style='margin-bottom:6px;'>불꽃 센서만 감지 (화재 영상은 안 잡힘)</li>"
          "<li style='margin-bottom:6px;'>화재 영상만 감지 (불꽃 센서는 기준치 미만)</li>"
          "<li style='margin-bottom:0;'>연기 영상만 감지 (연기 센서는 기준치 미만)</li>"
          "</ul>"
          "<p style='margin:0 0 6px 0;'><span style='color:" + kDangerColor + ";'>● 위험</span> — 기준치를 크게 초과했거나, 영상+센서가 <b>같이</b> 감지됐을 때</p>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:6px;'>가스 센서(MQ-9)만 기준치 초과 — 영상 감지와 무관하게 단독으로 위험</li>"
          "<li style='margin-bottom:6px;'>연기 센서(MQ-2)만 기준치 초과 — 연기 영상 없이도 단독으로 위험</li>"
          "<li style='margin-bottom:6px;'>화재 영상 + 불꽃 센서 <b>둘 다</b> 감지</li>"
          "<li style='margin-bottom:6px;'>연기 영상 + 연기 센서 <b>둘 다</b> 감지</li>"
          "<li style='margin-bottom:6px;'>가스 + (화재 영상·불꽃 센서 둘 다) — 셋 다 감지</li>"
          "<li style='margin-bottom:6px;'>가스 + (연기 영상·연기 센서 둘 다) — 셋 다 감지</li>"
          "<li style='margin-bottom:0;'>가스 + 연기 센서 — 영상 없이 센서 둘만 같이 기준치 초과</li>"
          "</ul>" },

        { "모니터링 탭",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>모니터링 탭</h3>"
          "<p style='margin:0 0 14px 0;'>상단 구역 버튼으로 구역을 전환하며, 실시간 카메라 영상과 센서 수치, 제어 버튼을 확인합니다.</p>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>카메라 화면 우측 상단의 ROI 버튼으로 감지 제외구역을 설정할 수 있습니다. 화면을 드래그하면 사각형이 추가되고,"
          " 사각형 위 × 배지 클릭 또는 우클릭으로 삭제합니다.</li>"
          "<li style='margin-bottom:10px;'>연기 수치 옆에는 최근 추세(▲ 상승 / ▼ 하강 / ● 안정)가 함께 표시됩니다.</li>"
          "<li style='margin-bottom:10px;'>환기팬·밸브·사이렌은 수동 제어 버튼으로 직접 켜고 끌 수 있으며, 오작동 방지를 위해 확인 팝업이 뜹니다.</li>"
          "</ul>"
          "<h3 style='color:#f5f5fa; font-size:16px; font-weight:700; margin:18px 0 10px 0;'>카메라 화면 상단 아이콘 (왼쪽부터)</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:8px;'><b>Ch.N</b> — 채널 번호</li>"
          "<li style='margin-bottom:8px;'><span style='color:#f59e0b;'>●감지</span> — 화재/연기 감지 알람이 켜져 있을 때만 표시</li>"
          "<li style='margin-bottom:8px;'>👤 N — 사람 감지 인원 수. 없으면 회색, 있으면 초록, 위험 구역 안에 있으면 빨강으로 깜빡입니다.</li>"
          "<li style='margin-bottom:8px;'>🔊 — 대피 안내 음성이 나가는 중일 때만 표시 (사이렌·스피커 담당 채널)</li>"
          "<li style='margin-bottom:8px;'>● LIVE — 실시간 스트리밍 표시</li>"
          "<li style='margin-bottom:8px;'><b>− / 1.0x / +</b> — 화면 확대·축소(0.1배 단위, 최대 2.5배). 확대된 상태에서 영상을 드래그하면 보이는 위치를 옮길 수 있고,"
          " 배율 숫자(1.0x)를 클릭하면 배율과 위치가 한 번에 초기화됩니다.</li>"
          "<li style='margin-bottom:8px;'><b>▭</b> — 감시 제외 영역(ROI) 편집 모드 켜기/끄기 (켜지면 빨간색으로 표시)</li>"
          "<li style='margin-bottom:8px;'><b>👁</b> — 이미 설정해둔 제외 영역 표시를 화면에서 껐다 켰다 (영상만 깨끗하게 보고 싶을 때). 편집 모드 중엔 이 설정과 무관하게 항상 보입니다.</li>"
          "<li style='margin-bottom:0;'><b>⛶</b> — 확대 보기 (카메라 화면 더블클릭과 동일하게 새 창으로 크게 봅니다)</li>"
          "</ul>" },

        { "위험 모드 대응",
          "<h3 style='color:#f5f5fa; font-size:22px; font-weight:700; margin:0 0 16px 0; padding-bottom:10px; border-bottom:1px solid #2c2c3d;'>위험 모드 진입 시</h3>"
          "<ul style='margin:0; padding-left:22px;'>"
          "<li style='margin-bottom:10px;'>위험 상태로 전환되면 원인 선택 모달이 뜹니다. 실제 원인을 선택하면 그에 맞는 대응 체크리스트가 표시됩니다.</li>"
          "<li style='margin-bottom:10px;'>상단 배너와 화면 가장자리 글로우로 위험 구역이 있음을 항상 표시하며, 배너 클릭 시 해당 구역 모니터링으로 바로 이동합니다.</li>"
          "<li style='margin-bottom:10px;'><b>위험 모드 전환</b> / <b>위험 모드 해제</b> 버튼으로 관리자가 직접 위험 모드를 켜거나 끌 수 있습니다.</li>"
          "<li style='margin-bottom:10px;'>자동 대응(환기팬/밸브 가동 등)이 실패하면 <b>위험 모드 전환</b> 버튼이 <b>⟳ 대응 재실행 (사유)</b>로 바뀌며 주황색으로 깜빡입니다. 눌러서 같은 대응을 다시 시도할 수 있습니다.</li>"
          "<li style='margin-bottom:10px;'>스피커로 나오는 대피 안내 음성은 <b>🔇 음성 정지</b> 버튼으로 수동으로 멈출 수 있습니다.</li>"
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
          "<li style='margin-bottom:10px;'>기록되는 감지 내용 종류는 다음과 같습니다:"
          "<ul style='margin:10px 0 0 0; padding-left:20px;'>"
          "<li style='margin-bottom:10px;'><span style='color:" + kWarnColor + ";'>경고 상태 감지</span> / <span style='color:" + kDangerColor + ";'>위험 상태 감지</span> — 센서·카메라가 자동으로 감지</li>"
          "<li style='margin-bottom:10px;'><b>[위험 전환]</b> — 경고 무응답 또는 관리자 수동 전환으로 위험 모드 진입</li>"
          "<li style='margin-bottom:10px;'><b>[대응 재실행]</b> — 이미 위험 상태에서 원인이 다시 감지되어 대응을 재실행</li>"
          "<li style='margin-bottom:10px;'>위험 해제 / 위험 모드 해제 — 자동 정상 복귀 또는 관리자 수동 해제(체크리스트 포함)</li>"
          "<li style='margin-bottom:10px;'>관리자 수동 제어 — 팬·밸브·사이렌·스피커를 관리자가 직접 조작</li>"
          "</ul></li>"
          "<li style='margin-bottom:10px;'>각 행의 <b>위험도</b>(안전/경고/위험/정보)와 <b>처리상태</b>(해결됨/오탐 처리됨)로도 필터링할 수 있습니다.</li>"
          "<li style='margin-bottom:10px;'>행을 클릭하면 우측에 상세 정보와 함께, 감지 시점 스냅샷(사진)이 자동으로 뜨고, 화재/연기 등 실제 감지 이벤트라면 사고 전후 클립(mp4)도 재생 버튼을 눌러 Qt 안에서 바로 볼 수 있습니다.</li>"
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
// 캡션을 박스 "위"에 둔다 — 뭘 보고 있는 건지 먼저 읽고 나서 박스를 보는 순서가 자연스럽고,
// 평면도처럼 "눌러보라"는 안내가 있는 카드는 미리 읽어야 시도해볼 생각이 든다.
// boxHeight는 항상 setFixedHeight로 고정한다 — QStackedWidget은 카드 하나가 아니라 안에 든
// 모든 페이지 중 가장 큰 sizeHint를 기준으로 자기 크기를 잡기 때문에, 높이가 안 고정된 카드가
// 하나라도 있으면 그 카드가 지금 안 보이는 동안에도 다른(작은) 카드들까지 같이 늘어나 버린다.
QWidget *makePreviewCard(QWidget *parent, const QString &caption, int boxHeight = 84)
{
    auto *card = new QWidget(parent);
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    auto *label = new QLabel(caption, card);
    label->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold; border:none;").arg(kTextSecondary));
    label->setWordWrap(true);
    outer->addWidget(label);

    auto *box = new QWidget(card);
    box->setStyleSheet(QString("background-color:#0d0d16; border:1px solid %1; border-radius:8px;").arg(kCardBorder));
    box->setFixedHeight(boxHeight);
    outer->addWidget(box);
    outer->addStretch();

    return card;
}

QWidget *previewInner(QWidget *card)
{
    return card->layout()->itemAt(1)->widget();
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

// 실제 화면 캡처 예시 이미지를 보여준다(2026-08-21, 실제 앱 스크린샷). 카드 폭에 맞춰 한 번만
// 스케일해서 붙인다 — 라벨을 리사이즈마다 다시 스케일하는 건 도움말 화면엔 과한 처리라 생략.
QWidget *buildImagePreview(QWidget *parent, const QString &resourcePath, const QString &caption)
{
    // 높이를 기준으로 스케일한다 — 실제 캡처가 1920x1080 안팎이라 폭 기준으로 맞추면 카드가
    // 너무 길어진다. showTopic()이 주제별로 previewStack 높이를 따로 맞춰주므로(다른 주제를
    // 밀어내지 않음) 이 값은 이 주제만 생각해서 넉넉하게 잡아도 된다.
    const QPixmap original(resourcePath);
    const QPixmap scaled = original.isNull() ? original : original.scaledToHeight(480, Qt::SmoothTransformation);

    auto *card = makePreviewCard(parent, caption, scaled.height() + 16);
    auto *inner = previewInner(card);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *imgLabel = new QLabel(inner);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setStyleSheet("border:none;");
    imgLabel->setPixmap(scaled);
    layout->addWidget(imgLabel);

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
    previewStack->addWidget(buildImagePreview(previewStack, ":/help/eventlog_example.png", "미리보기 — 이벤트로그 화면 (실제 캡처)")); // 4
    previewStack->addWidget(buildImagePreview(previewStack, ":/help/graph_example.png", "미리보기 — 그래프 화면 (실제 캡처)"));         // 5
    previewStack->addWidget(buildFloorMapPreview(previewStack));                                   // 6 평면도 탭
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

    auto *clearBtn = new QLabel("위험 모드 해제", previewInner(card));
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

// 평면도·대피경로는 캡처 이미지로는 "클릭하면 경로가 뜬다"는 동작 자체를 못 보여준다 — 왼쪽엔
// "지도 재설정" 눌렀을 때 뜨는 변환 창 캡처를 참고용으로 작게 두고, 오른쪽엔 실제 화면과 같은
// 위젯(FloorMapGridWidget)을 띄워서 주황 원(전광판)을 직접 눌러보면 대피경로가 그려지는 걸 체험할
// 수 있게 한다. 위아래로 쌓으면 카드가 너무 길어져서(스크롤 필요) 좌우로 나눴다.
// floorMapPreviewGrid는 실제 평면도 데이터가 도착하면(updateFloorMapExample) 지금 등록된 진짜
// 평면도 모양으로 바뀐다 — 그 전까지는 아래 예시 격자를 기본값으로 보여준다.
QWidget *HelpPage::buildFloorMapPreview(QWidget *parent)
{
    // showTopic()이 주제별로 previewStack 높이를 따로 맞춰주므로(다른 주제를 안 밀어냄) 이
    // 주제만 생각해서 격자를 넉넉하게 키울 수 있다 — 평면도 탭 글 내용이 짧은 편이라 여유가 있다.
    constexpr int kContentHeight = 420; // 캡션 한 줄 + 이미지/격자

    const QPixmap convertOriginal(":/help/floormap_convert_example.png");
    const QPixmap convertScaled = convertOriginal.isNull() ? convertOriginal
        : convertOriginal.scaledToHeight(kContentHeight, Qt::SmoothTransformation);

    auto *card = makePreviewCard(parent,
        "미리보기 — 평면도 등록 + 대피경로 (오른쪽 주황 원을 눌러보세요, 실제 화면과 동일하게 동작합니다)",
        kContentHeight + 30);
    auto *inner = previewInner(card);
    auto *row = new QHBoxLayout(inner);
    row->setContentsMargins(10, 10, 10, 10);
    row->setSpacing(14);

    if (!convertScaled.isNull()) {
        auto *leftCol = new QVBoxLayout;
        leftCol->setSpacing(6);
        auto *convertCaption = new QLabel("① 지도 재설정 → 원본 이미지 선택 시 뜨는 변환 창 (실제 캡처)", inner);
        convertCaption->setWordWrap(true);
        convertCaption->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
        leftCol->addWidget(convertCaption);

        auto *convertImage = new QLabel(inner);
        convertImage->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        convertImage->setStyleSheet("border:none;");
        convertImage->setPixmap(convertScaled);
        leftCol->addWidget(convertImage);
        leftCol->addStretch();
        row->addLayout(leftCol);
    }

    auto *rightCol = new QVBoxLayout;
    rightCol->setSpacing(6);
    auto *gridCaption = new QLabel("② 등록 후 화면 — 주황 원(전광판)을 눌러보세요", inner);
    rightCol->addWidget(gridCaption);
    gridCaption->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));

    floorMapPreviewGrid = new FloorMapGridWidget(inner);
    floorMapPreviewGrid->setFixedSize(kContentHeight, kContentHeight); // 정사각형 격자
    rightCol->addWidget(floorMapPreviewGrid);
    rightCol->addStretch();
    row->addLayout(rightCol);
    row->addStretch();

    applyFloorMapSampleData();

    return card;
}

// 아직 실제 평면도가 등록되기 전(또는 그 데이터를 못 받아온 상태)에 보여줄 기본 예시.
// 실제 등록된 평면도가 오면(updateFloorMapExample) 이 자리를 그대로 덮어쓴다.
void HelpPage::applyFloorMapSampleData()
{
    if (!floorMapPreviewGrid)
        return;
    const int gridSize = 20;
    QVector<QVector<int>> bitmap(gridSize, QVector<int>(gridSize, 0));
    for (int i = 0; i < gridSize; ++i) {
        bitmap[0][i] = bitmap[gridSize - 1][i] = 1;
        bitmap[i][0] = bitmap[i][gridSize - 1] = 1;
    }
    const QVector<FloorMapMarker> displays = { { 1, 2, 10 }, { 2, 10, 3 } };
    const QVector<FloorMapMarker> exits = { { 1, 18, 3 }, { 2, 18, 16 } };
    const QVector<FloorMapRoute> routes = {
        { 1, 2, { QPoint(10, 2), QPoint(10, 10), QPoint(16, 10), QPoint(16, 18) } },
        { 2, 1, { QPoint(3, 10), QPoint(3, 18) } },
    };
    floorMapPreviewGrid->setMapData(gridSize, bitmap, displays, exits, routes);
}

// MainWindow가 서버로부터 실제 평면도 데이터를 받을 때마다(FloorMapPage와 동일한 데이터) 같이
// 호출해준다 — 도움말의 예시 격자가 지금 실제 등록된 평면도 모양 그대로 보이게 하기 위함.
void HelpPage::updateFloorMapExample(int gridSize, const QVector<QVector<int>> &bitmap,
                                      const QVector<FloorMapMarker> &displays,
                                      const QVector<FloorMapMarker> &exits,
                                      const QVector<FloorMapRoute> &routes)
{
    if (floorMapPreviewGrid)
        floorMapPreviewGrid->setMapData(gridSize, bitmap, displays, exits, routes);
}

void HelpPage::showTopic(int index)
{
    const QList<HelpTopic> topics = buildTopics();
    if (index < 0 || index >= topics.size())
        return;
    contentView->setHtml(QString("<div style='color:%1; font-size:16px; line-height:2.0;'>%2</div>").arg(kTextSecondary, topics[index].html));

    if (previewStack && index < previewStack->count()) {
        previewStack->setCurrentIndex(index);
        // QStackedWidget은 기본적으로 "지금 보이는 페이지"가 아니라 "그 안에 든 모든 페이지 중
        // 가장 큰 sizeHint"를 기준으로 자기 크기를 잡는다 — 그대로 두면 주제 하나(예: 평면도)가
        // 크다는 이유로 다른 작은 주제(예: 종합 상태 색상)의 설명 글까지 밀려 올라간다. 주제가
        // 바뀔 때마다 지금 페이지에 맞는 높이로 직접 고정해서, 각 주제가 자기 내용에 맞는
        // 박스 크기를 갖고 나머지 공간은 전부 설명 글(contentView)이 쓰게 한다.
        if (QWidget *page = previewStack->currentWidget())
            previewStack->setFixedHeight(page->sizeHint().height());
    }

    // 재시도 버튼 깜빡임은 해당 주제를 보고 있을 때만 돈다.
    if (retryBlinkTimer)
        (index == 2) ? retryBlinkTimer->start(600) : retryBlinkTimer->stop();
}
