#include "StartupWindow.h"

#include "HistoryManager.h"
#include "ThemeUtils.h"
#include "TranslationManager.h"

#include <QAbstractItemView>
#include <QColor>
#include <QApplication>
#include <QDate>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#ifndef APP_NAME
#define APP_NAME "SnapLingo"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#ifndef APP_COPYRIGHT
#define APP_COPYRIGHT "Copyright Â© 2026"
#endif

#ifndef APP_REPO_URL
#define APP_REPO_URL ""
#endif

static QString prettyHotkey(QString s)
{
    s = s.trimmed();
    if (s.isEmpty())
        return s;

    const QStringList parts = s.split('+', Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (QString p : parts)
    {
        p = p.trimmed();
        const QString lower = p.toLower();
        if (lower == "ctrl" || lower == "control")
            out << "Ctrl";
        else if (lower == "alt")
            out << "Alt";
        else if (lower == "shift")
            out << "Shift";
        else if (lower == "win" || lower == "meta" || lower == "super")
            out << "Win";
        else if (p.size() == 1)
            out << p.toUpper();
        else
            out << p;
    }
    return out.join('+');
}

static QString loadPrivacyPolicyMarkdown()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    
    // Determine language suffix
    QString lang = TranslationManager::instance().getLanguage();
    QString langSuffix = (lang == "en") ? "_en" : "";
    
    const QStringList candidates = {
        QDir::cleanPath(baseDir + "/assets/privacy_policy" + langSuffix + ".md"),
        QDir::cleanPath(baseDir + "/../assets/privacy_policy" + langSuffix + ".md"),
        QDir::cleanPath(baseDir + "/../../assets/privacy_policy" + langSuffix + ".md"),
    };

    for (const QString &path : candidates)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        return QString::fromUtf8(f.readAll());
    }

    return QString();
}

StartupWindow::StartupWindow(const AppConfig &cfg, HistoryManager *historyManager,
                             const QString &profileName, QWidget *parent)
    : QDialog(parent), m_cfg(cfg), m_historyManager(historyManager), m_profileName(profileName)
{
    loadUiConfig();
    setupWindowChrome();
    setupDialogUi();
    applyDashboardStyle();

    QCoreApplication::setApplicationVersion(m_currentVersion.isEmpty() ? QString::fromUtf8(APP_VERSION) : m_currentVersion);

    QTimer::singleShot(0, this, [this]()
                       { startUpdateCheck(false); });
}

void StartupWindow::setupWindowChrome()
{
    const QString windowTitle = uiString("windowTitle", QStringLiteral("%1").arg(APP_NAME));
    setWindowTitle(formatText(windowTitle));
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);

    // 780x570 is the intended dashboard size, but a 1366x768 screen at 150%
    // scaling only offers ~512 logical pixels of height. Fit the initial size to
    // the available screen instead of forcing a window that cannot be seen whole.
    int w = 780;
    int h = 570;
    if (QScreen *screen = QGuiApplication::primaryScreen())
    {
        const QRect avail = screen->availableGeometry();
        w = qMin(w, avail.width() - 60);
        h = qMin(h, avail.height() - 60);
    }
    setMinimumSize(660, 460);
    resize(qMax(660, w), qMax(460, h));
    ThemeUtils::applyThemeToWindow(this, ThemeUtils::isSystemDark());
}

void StartupWindow::setupDialogUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 14);
    root->setSpacing(12);

    setupDashboardHeader(root);
    setupDashboardActions(root);
    setupDashboardRecent(root);
    setupDashboardFooter(root);

    populateRecentEntries();
}

void StartupWindow::setupDashboardHeader(QVBoxLayout *root)
{
    auto *header = new QHBoxLayout();
    header->setSpacing(14);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(56, 56);
    QIcon appIcon(QStringLiteral(":/assets/icon.ico"));
    if (appIcon.isNull())
        appIcon = windowIcon();
    if (appIcon.isNull())
        appIcon = QApplication::windowIcon();
    if (!appIcon.isNull())
        m_iconLabel->setPixmap(appIcon.pixmap(56, 56));
    header->addWidget(m_iconLabel, 0, Qt::AlignVCenter);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(3);

    m_titleLabel = new QLabel(formatText(uiString("windowTitle", QString::fromUtf8(APP_NAME))), this);
    m_titleLabel->setObjectName(QStringLiteral("dashboardTitle"));
    titleCol->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(formatText(uiString("subtitle", QStringLiteral("截图 · 翻译 · 归档"))), this);
    m_subtitleLabel->setObjectName(QStringLiteral("dashboardSubtitle"));
    titleCol->addWidget(m_subtitleLabel);

    header->addLayout(titleCol, 1);

    m_versionPill = new QLabel(formatText(uiString("dashboard.version", QStringLiteral("v{version}"))), this);
    m_versionPill->setObjectName(QStringLiteral("versionPill"));
    m_versionPill->setAlignment(Qt::AlignCenter);
    m_versionPill->setToolTip(formatText(uiString("versionFormat", QStringLiteral("版本：{version}"))));
    m_versionLabel = m_versionPill;
    header->addWidget(m_versionPill, 0, Qt::AlignTop);

    root->addLayout(header);
}

void StartupWindow::setupDashboardActions(QVBoxLayout *root)
{
    const QString shotKey = prettyHotkey(m_cfg.screenshotHotkey.isEmpty()
                                             ? QStringLiteral("ctrl+alt+s")
                                             : m_cfg.screenshotHotkey);
    m_screenshotBtn = new QPushButton(
        QString("%1    %2").arg(uiString("actions.screenshot", QStringLiteral("开始截图翻译")), shotKey), this);
    m_screenshotBtn->setObjectName(QStringLiteral("primaryAction"));
    m_screenshotBtn->setFixedHeight(50);
    m_screenshotBtn->setCursor(Qt::PointingHandCursor);
    connect(m_screenshotBtn, &QPushButton::clicked, this, [this]()
            {
        m_action = Action::Screenshot;
        accept(); });
    root->addWidget(m_screenshotBtn);

    auto *secondary = new QHBoxLayout();
    secondary->setSpacing(10);

    const QString archiveKey = prettyHotkey(m_cfg.summaryHotkey.isEmpty()
                                                ? QStringLiteral("alt+s")
                                                : m_cfg.summaryHotkey);
    m_archiveBtn = new QPushButton(
        QString("%1    %2").arg(uiString("actions.archive", QStringLiteral("打开归档")), archiveKey), this);
    m_archiveBtn->setObjectName(QStringLiteral("secondaryAction"));
    m_archiveBtn->setFixedHeight(40);
    m_archiveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_archiveBtn, &QPushButton::clicked, this, [this]()
            {
        m_action = Action::Archive;
        accept(); });
    secondary->addWidget(m_archiveBtn, 1);

    m_settingsBtn = new QPushButton(uiString("actions.settings", QStringLiteral("设置")), this);
    m_settingsBtn->setObjectName(QStringLiteral("secondaryAction"));
    m_settingsBtn->setFixedHeight(40);
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    connect(m_settingsBtn, &QPushButton::clicked, this, [this]()
            {
        m_action = Action::Settings;
        accept(); });
    secondary->addWidget(m_settingsBtn, 1);

    root->addLayout(secondary);

    auto *meta = new QHBoxLayout();
    meta->setSpacing(16);

    m_profileLabel = new QLabel(this);
    m_profileLabel->setObjectName(QStringLiteral("profileMeta"));
    QString profileText = uiString("dashboard.profile", QStringLiteral("当前配置：{profile}"));
    profileText.replace("{profile}", m_profileName);
    m_profileLabel->setText(profileText);
    m_profileLabel->setVisible(!m_profileName.trimmed().isEmpty());
    meta->addWidget(m_profileLabel, 0);

    const bool apiReady = !m_cfg.apiKey.trimmed().isEmpty();
    m_apiStatusLabel = new QLabel(this);
    m_apiStatusLabel->setObjectName(QStringLiteral("apiStatus"));
    m_apiStatusLabel->setText(QString::fromUtf8("\u25CF ")
                              + uiString(apiReady ? "dashboard.apiReady" : "dashboard.apiMissing",
                                         apiReady ? QStringLiteral("API 已配置") : QStringLiteral("API 未配置")));
    m_apiStatusLabel->setProperty("state", apiReady ? "ok" : "warn");
    meta->addWidget(m_apiStatusLabel, 0);
    meta->addStretch(1);

    root->addLayout(meta);
}

void StartupWindow::setupDashboardRecent(QVBoxLayout *root)
{
    m_recentCard = new QFrame(this);
    m_recentCard->setObjectName(QStringLiteral("recentCard"));
    m_recentCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *cardLayout = new QVBoxLayout(m_recentCard);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    auto *recentTitle = new QLabel(uiString("dashboard.recentTitle", QStringLiteral("最近翻译")), m_recentCard);
    recentTitle->setObjectName(QStringLiteral("recentTitle"));
    head->addWidget(recentTitle, 0);
    head->addStretch(1);

    m_viewAllBtn = new QPushButton(uiString("dashboard.viewAll", QStringLiteral("查看全部")), m_recentCard);
    m_viewAllBtn->setObjectName(QStringLiteral("linkButton"));
    m_viewAllBtn->setFlat(true);
    m_viewAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_viewAllBtn, &QPushButton::clicked, this, [this]()
            {
        m_action = Action::Archive;
        accept(); });
    head->addWidget(m_viewAllBtn, 0);
    cardLayout->addLayout(head);

    m_recentList = new QListWidget(m_recentCard);
    m_recentList->setObjectName(QStringLiteral("recentList"));
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setFocusPolicy(Qt::NoFocus);
    m_recentList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setUniformItemSizes(true);
    m_recentList->setSpacing(2);
    m_recentList->setCursor(Qt::PointingHandCursor);
    m_recentList->viewport()->setAutoFillBackground(false);
    m_recentList->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    connect(m_recentList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
            {
        if (!item)
            return;
        m_requestedEntryId = item->data(Qt::UserRole).toString();
        if (!m_requestedEntryId.isEmpty())
            accept(); });
    cardLayout->addWidget(m_recentList);

    m_recentEmptyContainer = new QWidget(m_recentCard);
    m_recentEmptyContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *emptyLayout = new QVBoxLayout(m_recentEmptyContainer);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->addStretch(1);

    m_recentEmptyLabel = new QLabel(m_recentEmptyContainer);
    m_recentEmptyLabel->setObjectName(QStringLiteral("recentEmpty"));
    m_recentEmptyLabel->setWordWrap(true);
    m_recentEmptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_recentEmptyLabel);
    emptyLayout->addStretch(1);
    cardLayout->addWidget(m_recentEmptyContainer, 1);

    // Absorbs the card's spare height while the recent list is visible. Without it
    // QVBoxLayout spreads the extra space between the card header and the list.
    m_recentSpacer = new QWidget(m_recentCard);
    m_recentSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cardLayout->addWidget(m_recentSpacer, 1);

    root->addWidget(m_recentCard, 1);
}

void StartupWindow::setupDashboardFooter(QVBoxLayout *root)
{
    addUpdateRow(root);

    auto *bottom = new QHBoxLayout();
    bottom->setSpacing(8);

    m_hintLabel = new QLabel(formatText(uiString("hint", QStringLiteral("点击右上角 × 关闭"))), this);
    m_hintLabel->setObjectName(QStringLiteral("footerHint"));
    m_hintLabel->setWordWrap(false);
    bottom->addWidget(m_hintLabel, 1);

    m_privacyBtn = new QPushButton(formatText(uiString("buttons.privacy", QStringLiteral("隐私协议"))), this);
    m_privacyBtn->setObjectName(QStringLiteral("linkButton"));
    m_privacyBtn->setFlat(true);
    m_privacyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_privacyBtn, &QPushButton::clicked, this, &StartupWindow::openPrivacyPolicy);
    bottom->addWidget(m_privacyBtn, 0);

    root->addLayout(bottom);

    const QString cr = uiString("copyright", QString::fromUtf8(APP_COPYRIGHT));
    // Keep the footer to one short line; the full attribution stays in the tooltip.
    m_copyrightLabel = new QLabel(QStringLiteral("Copyright © 2026 Junlet · GPL-3.0"), this);
    m_copyrightLabel->setObjectName(QStringLiteral("footerCopyright"));
    m_copyrightLabel->setWordWrap(false);
    m_copyrightLabel->setToolTip(formatText(cr));
    root->addWidget(m_copyrightLabel);

    if (m_updateApiUrl.trimmed().isEmpty())
    {
        if (m_updateStatusLabel)
            m_updateStatusLabel->setVisible(false);
        if (m_checkUpdateBtn)
            m_checkUpdateBtn->setVisible(false);
    }
    if (m_repoUrl.trimmed().isEmpty() && m_openReleasesBtn)
        m_openReleasesBtn->setVisible(false);
    if (m_copyrightLabel && m_copyrightLabel->text().trimmed().isEmpty())
        m_copyrightLabel->setVisible(false);
}

void StartupWindow::populateRecentEntries()
{
    if (!m_recentList || !m_recentEmptyLabel)
        return;

    m_recentList->clear();

    QList<TranslationEntry> entries;
    if (m_historyManager)
    {
        entries = m_historyManager->queryEntries(QDate(1970, 1, 1), QDate::currentDate(),
                                                 QStringList(), QString(), 3, 0, nullptr);
    }

    if (entries.isEmpty())
    {
        m_recentList->hide();
        m_recentSpacer->hide();
        m_recentEmptyContainer->show();
        m_recentEmptyLabel->setText(formatText(uiString(
            "dashboard.recentEmpty", QStringLiteral("还没有翻译记录，按 {shot} 开始第一张。"))));
        return;
    }

    m_recentEmptyContainer->hide();
    m_recentSpacer->show();
    m_recentList->show();

    for (const TranslationEntry &entry : entries)
    {
        auto *item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, entry.id);
        item->setSizeHint(QSize(0, 52));
        const QString timeText = entry.timestamp.toString(QStringLiteral("MM-dd HH:mm"));
        const QString snippet = recentSnippet(entry.translatedMarkdown);
        item->setToolTip(timeText + QStringLiteral("\n") + snippet);

        auto *row = new QWidget(m_recentList);
        row->setObjectName(QStringLiteral("recentRow"));
        row->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(10, 5, 10, 5);
        rowLayout->setSpacing(2);

        auto *timeLabel = new QLabel(timeText, row);
        timeLabel->setObjectName(QStringLiteral("recentTime"));
        timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        rowLayout->addWidget(timeLabel);

        auto *snippetLabel = new QLabel(row);
        snippetLabel->setObjectName(QStringLiteral("recentSnippet"));
        snippetLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        snippetLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        snippetLabel->setMaximumWidth(560);
        snippetLabel->setToolTip(snippet);
        snippetLabel->setText(snippetLabel->fontMetrics().elidedText(snippet, Qt::ElideRight, 560));
        rowLayout->addWidget(snippetLabel);

        m_recentList->setItemWidget(item, row);
    }

    m_recentList->setFixedHeight(qMin(3, entries.size()) * 54);
}

QString StartupWindow::recentSnippet(const QString &markdown) const
{
    QString text = markdown;
    text.replace(QRegularExpression(QStringLiteral("[#>*_`~\\[\\]()]")), QStringLiteral(" "));
    text = text.simplified();
    if (text.size() > 90)
        text = text.left(87) + QChar(0x2026);
    return text.isEmpty() ? QStringLiteral("\u2026") : text;
}

void StartupWindow::addUpdateRow(QVBoxLayout *root)
{
    auto *updateRow = new QHBoxLayout();
    updateRow->setSpacing(8);

    m_updateStatusLabel = new QLabel(formatText(m_updateNotChecked.isEmpty() ? QStringLiteral("更新：未检查") : m_updateNotChecked), this);
    m_updateStatusLabel->setObjectName(QStringLiteral("updateStatus"));
    m_updateStatusLabel->setWordWrap(false);
    m_updateStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    updateRow->addWidget(m_updateStatusLabel, 1);

    m_checkUpdateBtn = new QPushButton(formatText(uiString("buttons.check", QStringLiteral("检查更新"))), this);
    m_checkUpdateBtn->setObjectName(QStringLiteral("linkButton"));
    m_checkUpdateBtn->setFlat(true);
    m_checkUpdateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_checkUpdateBtn, &QPushButton::clicked, this, [this]()
            { startUpdateCheck(true); });
    updateRow->addWidget(m_checkUpdateBtn, 0);

    m_openReleasesBtn = new QPushButton(formatText(uiString("buttons.open", QStringLiteral("打开发布页"))), this);
    m_openReleasesBtn->setObjectName(QStringLiteral("linkButton"));
    m_openReleasesBtn->setFlat(true);
    m_openReleasesBtn->setCursor(Qt::PointingHandCursor);
    m_openReleasesBtn->setEnabled(true);
    connect(m_openReleasesBtn, &QPushButton::clicked, this, &StartupWindow::openReleasesPage);
    updateRow->addWidget(m_openReleasesBtn, 0);

    root->addLayout(updateRow);
}

void StartupWindow::applyDashboardStyle()
{
    if (m_dashboardStyleApplying)
        return;
    m_dashboardStyleApplying = true;

    const bool dark = ThemeUtils::isSystemDark();
    const QString qss = dark ? QStringLiteral(R"QSS(
QDialog { background: #1b1b1b; }
QLabel#dashboardTitle { color: #f1f5f9; font-size: 23px; font-weight: 700; }
QLabel#dashboardSubtitle { color: #94a3b8; font-size: 12px; }
QLabel#versionPill { background: #1e3a8a; color: #dbeafe; border: 1px solid #3b82f6; border-radius: 10px; padding: 3px 10px; font-size: 11px; font-weight: 600; }
QPushButton#primaryAction { background: #2563eb; color: #ffffff; border: none; border-radius: 10px; font-size: 15px; font-weight: 600; }
QPushButton#primaryAction:hover { background: #3b82f6; }
QPushButton#primaryAction:pressed { background: #1d4ed8; }
QPushButton#secondaryAction { background: #242424; color: #e5e7eb; border: 1px solid #3a3a3a; border-radius: 8px; font-size: 13px; font-weight: 500; }
QPushButton#secondaryAction:hover { background: #2d2d2d; border-color: #4a4a4a; }
QLabel#profileMeta { color: #94a3b8; font-size: 12px; }
QLabel#apiStatus[state="ok"] { color: #4ade80; font-size: 12px; font-weight: 600; }
QLabel#apiStatus[state="warn"] { color: #fbbf24; font-size: 12px; font-weight: 600; }
QFrame#recentCard { background: #242424; border: 1px solid #343434; border-radius: 12px; }
QLabel#recentTitle { color: #e5e7eb; font-size: 13px; font-weight: 700; }
QListWidget#recentList { background: transparent; border: none; outline: 0; }
QListWidget#recentList::item { background: transparent; border-radius: 8px; }
QListWidget#recentList::item:hover { background: #2d2d2d; }
QListWidget#recentList::item:selected { background: #1f3a5a; }
QLabel#recentTime { color: #94a3b8; font-size: 11px; }
QLabel#recentSnippet { color: #e5e7eb; font-size: 12px; }
QWidget#recentRow { background: transparent; }
QLabel#recentEmpty { color: #808080; font-size: 12px; }
QPushButton#linkButton { background: transparent; border: none; color: #60a5fa; font-size: 12px; padding: 1px 3px; }
QPushButton#linkButton:hover { text-decoration: underline; }
QLabel#updateStatus { color: #9aa4b2; font-size: 11px; }
QLabel#footerHint { color: #d3b24c; font-size: 11px; font-weight: 600; }
QLabel#footerCopyright { color: #6b7280; font-size: 10px; }
)QSS") : QStringLiteral(R"QSS(
QDialog { background: #f6f8fb; }
QLabel#dashboardTitle { color: #0f172a; font-size: 23px; font-weight: 700; }
QLabel#dashboardSubtitle { color: #64748b; font-size: 12px; }
QLabel#versionPill { background: #e8f0fe; color: #1d4ed8; border: 1px solid #c7d2fe; border-radius: 10px; padding: 3px 10px; font-size: 11px; font-weight: 600; }
QPushButton#primaryAction { background: #2563eb; color: #ffffff; border: none; border-radius: 10px; font-size: 15px; font-weight: 600; }
QPushButton#primaryAction:hover { background: #1d4ed8; }
QPushButton#primaryAction:pressed { background: #1e40af; }
QPushButton#secondaryAction { background: #ffffff; color: #334155; border: 1px solid #d6dde6; border-radius: 8px; font-size: 13px; font-weight: 500; }
QPushButton#secondaryAction:hover { background: #f1f5f9; border-color: #b8c4d3; }
QLabel#profileMeta { color: #64748b; font-size: 12px; }
QLabel#apiStatus[state="ok"] { color: #15803d; font-size: 12px; font-weight: 600; }
QLabel#apiStatus[state="warn"] { color: #b45309; font-size: 12px; font-weight: 600; }
QFrame#recentCard { background: #ffffff; border: 1px solid #e3e8ef; border-radius: 12px; }
QLabel#recentTitle { color: #334155; font-size: 13px; font-weight: 700; }
QListWidget#recentList { background: transparent; border: none; outline: 0; }
QListWidget#recentList::item { background: transparent; border-radius: 8px; }
QListWidget#recentList::item:hover { background: #f1f5f9; }
QListWidget#recentList::item:selected { background: #e8f0fe; }
QLabel#recentTime { color: #64748b; font-size: 11px; }
QLabel#recentSnippet { color: #1e293b; font-size: 12px; }
QWidget#recentRow { background: transparent; }
QLabel#recentEmpty { color: #94a3b8; font-size: 12px; }
QPushButton#linkButton { background: transparent; border: none; color: #2563eb; font-size: 12px; padding: 1px 3px; }
QPushButton#linkButton:hover { text-decoration: underline; }
QLabel#updateStatus { color: #64748b; font-size: 11px; }
QLabel#footerHint { color: #b24a4a; font-size: 11px; font-weight: 600; }
QLabel#footerCopyright { color: #94a3b8; font-size: 10px; }
)QSS");

    setStyleSheet(qss);
    if (m_apiStatusLabel)
    {
        m_apiStatusLabel->style()->unpolish(m_apiStatusLabel);
        m_apiStatusLabel->style()->polish(m_apiStatusLabel);
    }

    m_dashboardStyleApplying = false;
}

void StartupWindow::updateHintColor()
{
    if (!m_hintLabel || m_updatingHintColor)
        return;

    m_updatingHintColor = true;

    const QColor bg = palette().color(QPalette::Window);
    const bool isDark = bg.lightness() < 128;
    const QColor desired = QColor(isDark ? QStringLiteral("#D3B24C") : QStringLiteral("#B24A4A"));

    QPalette pal = m_hintLabel->palette();
    if (pal.color(QPalette::WindowText) != desired)
    {
        pal.setColor(QPalette::WindowText, desired);
        m_hintLabel->setPalette(pal);
    }

    m_updatingHintColor = false;
}

void StartupWindow::loadUiConfig()
{
    m_uiConfig = QJsonObject();

    // Determine language-specific config file
    QString lang = TranslationManager::instance().getLanguage();
    QString langSuffix = (lang == "en") ? "_en" : "";
    
    const QString path = QCoreApplication::applicationDirPath() + "/assets/startup_window" + langSuffix + ".json";
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            m_uiConfig = doc.object();
    }

    m_currentVersion = uiString("version", QString::fromUtf8(APP_VERSION));
    m_updateApiUrl = uiString("repoApiLatest", QString());
    m_repoUrl = uiString("repoUrl", QString::fromUtf8(APP_REPO_URL));

    m_updateNotChecked = uiString("update.statusNotChecked", QStringLiteral("Update: Not checked"));
    m_updateChecking = uiString("update.statusChecking", QStringLiteral("Update: Checking..."));
    m_updateNetworkError = uiString("update.statusNetworkError", QStringLiteral("Update: Check failed (network error)"));
    m_updateParseError = uiString("update.statusParseError", QStringLiteral("Update: Check failed (parse error)"));
    m_updateNoVersion = uiString("update.statusNoVersion", QStringLiteral("Update: No version info obtained"));
    m_updateLatestTpl = uiString("update.statusLatest", QStringLiteral("Update: Already latest ({latest})"));
    m_updateNewTpl = uiString("update.statusNew", QStringLiteral("Update: New version found ({latest})"));
}

QString StartupWindow::uiString(const QString &path, const QString &fallback) const
{
    if (m_uiConfig.isEmpty())
        return fallback;

    const QStringList keys = path.split('.', Qt::SkipEmptyParts);
    QJsonValue v(m_uiConfig);
    for (const QString &k : keys)
    {
        if (!v.isObject())
            return fallback;
        v = v.toObject().value(k);
    }
    if (!v.isString())
        return fallback;
    const QString s = v.toString();
    return s.isEmpty() ? fallback : s;
}

QString StartupWindow::formatText(QString text) const
{
    const QString version = m_currentVersion.isEmpty() ? QString::fromUtf8(APP_VERSION) : m_currentVersion;
    const QString shot = prettyHotkey(m_cfg.screenshotHotkey.isEmpty() ? QStringLiteral("ctrl+alt+s") : m_cfg.screenshotHotkey);
    const QString view = prettyHotkey(m_cfg.summaryHotkey.isEmpty() ? QStringLiteral("alt+s") : m_cfg.summaryHotkey);
    const QString edit = m_cfg.editHotkey.isEmpty() ? QStringLiteral("e") : m_cfg.editHotkey;

    text.replace("{version}", version);
    text.replace("{shot}", shot);
    text.replace("{view}", view);
    text.replace("{edit}", edit);
    return text;
}

void StartupWindow::openPrivacyPolicy()
{
    const QString privacyTitle = uiString("privacyDialog.title",
                                          uiString("buttons.privacy", QStringLiteral("Privacy Policy")));
    const QString privacyMissingText = uiString("privacyDialog.missingFile",
                                                QStringLiteral("Privacy policy file not found."));
    const QString closeText = uiString("buttons.close", QStringLiteral("Close"));

    const QString markdown = loadPrivacyPolicyMarkdown();
    if (markdown.trimmed().isEmpty())
    {
        QMessageBox::warning(this, privacyTitle, privacyMissingText);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(privacyTitle);
    dlg.setModal(true);
    dlg.resize(680, 520);
    ThemeUtils::applyThemeToWindow(&dlg, ThemeUtils::isSystemDark());

    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *viewer = new QTextBrowser(&dlg);
    viewer->setOpenExternalLinks(true);
    viewer->setMarkdown(markdown);
    layout->addWidget(viewer);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    if (QPushButton *closeButton = buttons->button(QDialogButtonBox::Close))
        closeButton->setText(closeText);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(buttons);

    m_showingChildDialog = true;
    dlg.exec();
    m_showingChildDialog = false;
}
