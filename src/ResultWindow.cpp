
#include "ResultWindow.h"

#include "ThemeUtils.h"
#include "EmbedWebView.h"
#include "TagDialog.h"
#include "HistoryManager.h"
#include "TranslationManager.h"
#include "ColorUtils.h"

#include <QVBoxLayout>
#include <QDialog>
#include <QGridLayout>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollArea>

// From main.cpp - global debug mode flag
extern bool g_enableLogging;
#include <QHBoxLayout>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <QSizeGrip>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QUuid>
#include <QBuffer>
#include <QToolButton>
#include <QFontMetrics>

#include <string>
#include <functional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _WIN32
#include "WinKeyForwarder.h"
#endif

ResultWindow::ResultWindow(QWidget *parent) : QWidget(parent)
{
  qDebug() << "[RW] ctor this=" << (void *)this;
  setWindowTitle(TranslationManager::instance().tr("result_window_title"));
  resize(400, 450);
  setAttribute(Qt::WA_DeleteOnClose);

  m_webContainer = new QWidget(this);
  m_webView = std::make_unique<EmbedWebView>(m_webContainer);

  // DevTools shortcut for WebView debugging - only in debug mode
  if (g_enableLogging)
  {
    QShortcut *devToolsShortcut = new QShortcut(QKeySequence(Qt::Key_F12), this);
    devToolsShortcut->setContext(Qt::ApplicationShortcut);
    connect(devToolsShortcut, &QShortcut::activated, this, [this]()
            {
          if (m_webView) m_webView->openDevTools(); });
  }

  m_toolBar = new QWidget(this);
  m_toolBar->setObjectName("resultToolBar");
  m_toolBar->setAttribute(Qt::WA_StyledBackground, true);

  // --- Toolbar layout: fixed Left / centered Paging / fixed Right ---
  // Goal: < 1/1 > position must never shift with MODE text width changes.
  auto makeToolButton = [](QAction *a) -> QToolButton *
  {
    QToolButton *b = new QToolButton();
    b->setDefaultAction(a);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
  };

  // Actions (kept as QAction so other code can enable/disable them)
  m_lockAction = new QAction("🔓", this);
  m_lockAction->setCheckable(true);
  m_lockAction->setToolTip(tr("Lock Window"));
  connect(m_lockAction, &QAction::triggered, this, &ResultWindow::toggleLock);

  // Continue-context toggle: the next screenshot translation is sent together with the
  // earlier results of this conversation. App owns the context list itself.
  m_contextAction = new QAction("🔗", this);
  m_contextAction->setCheckable(true);
  m_contextAction->setToolTip(tr("Continue context"));
  connect(m_contextAction, &QAction::triggered, this, [this](bool checked)
          {
        emit inheritContextToggled(checked);
        setContextInfo(checked, m_contextDepth); });

  // Shortcuts to the app's other windows, so the result window can act as the hub.
  m_settingsAction = new QAction("⚙", this);
  m_settingsAction->setToolTip(tr("Settings"));
  connect(m_settingsAction, &QAction::triggered, this, &ResultWindow::settingsRequested);

  m_archiveAction = new QAction("🗂", this);
  m_archiveAction->setToolTip(tr("Archive"));
  connect(m_archiveAction, &QAction::triggered, this, &ResultWindow::archiveRequested);

  // Content actions, moved here from the in-page action bar so the window has ONE row.
  m_editAction = new QAction("✏️", this);
  m_editAction->setCheckable(true);
  m_editAction->setToolTip(tr("Edit"));
  connect(m_editAction, &QAction::triggered, this, [this]()
          {
    if (m_webView)
      m_webView->eval("toggleEdit();"); });

  m_tagAction = new QAction("🏷", this);
  m_tagAction->setToolTip(tr("Tags"));
  connect(m_tagAction, &QAction::triggered, this, &ResultWindow::openTagDialog);

  m_retranslateAction = new QAction("🔁", this);
  m_retranslateAction->setToolTip(tr("Retranslate"));
  connect(m_retranslateAction, &QAction::triggered, this, &ResultWindow::retranslateCurrent);

  m_copyImageAction = new QAction("📋", this);
  m_copyImageAction->setToolTip(tr("Copy image"));
  connect(m_copyImageAction, &QAction::triggered, this, [this]()
          { emit copyScreenshotRequested(currentEntryId()); });

  m_copyContextAction = new QAction("🗒", this);
  m_copyContextAction->setToolTip(tr("Copy context"));
  connect(m_copyContextAction, &QAction::triggered, this, [this]()
          { emit copyContextRequested(currentEntryId()); });

  m_prevAction = new QAction("<", this);
  connect(m_prevAction, &QAction::triggered, this, &ResultWindow::showPrevious);

  m_nextAction = new QAction(">", this);
  connect(m_nextAction, &QAction::triggered, this, &ResultWindow::showNext);

  // Create widgets
  m_lockBtn = makeToolButton(m_lockAction);
  m_lockBtn->setObjectName("lockToggleBtn");
  m_contextBtn = makeToolButton(m_contextAction);
  m_contextBtn->setObjectName("contextToggleBtn");
  m_settingsBtn = makeToolButton(m_settingsAction);
  m_archiveBtn = makeToolButton(m_archiveAction);
  m_editBtn = makeToolButton(m_editAction);
  m_tagBtn = makeToolButton(m_tagAction);
  m_retranslateBtn = makeToolButton(m_retranslateAction);
  m_copyImageBtn = makeToolButton(m_copyImageAction);
  m_copyContextBtn = makeToolButton(m_copyContextAction);
  m_prevBtn = makeToolButton(m_prevAction);
  m_nextBtn = makeToolButton(m_nextAction);

  m_pageLabel = new QLabel(" 1 / 1 ", this);
  m_pageLabel->setAlignment(Qt::AlignCenter);
  {
    const QFontMetrics fm(m_pageLabel->font());
    m_pageMinW = fm.horizontalAdvance(" 1 / 1 ");
    m_pageCompactW = fm.horizontalAdvance("1/1");
    m_pageMaxW = fm.horizontalAdvance(" 888 / 888 ");
    m_pageLabel->setMinimumWidth(0);
    m_pageLabel->setMaximumWidth(m_pageMaxW);
    m_pageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  }

  // The MODE pill was removed on request: the current state is already obvious from
  // the content itself (rendered text vs. raw editor), and the pill ate toolbar space.
  // The page indicator doubles as the thumbnail-browser entry point.
  m_pageLabel->setCursor(Qt::PointingHandCursor);
  m_pageLabel->setToolTip(tr("Click for thumbnails"));
  m_pageLabel->installEventFilter(this);

  m_statusMode = "view";

  // One flat row: every button sits on the same line as the lock button. The pager moved
  // to its own bottom bar, so the old left/centre/right balancing blocks are gone.
  m_leftBlock = new QWidget(this);
  {
    QHBoxLayout *l = new QHBoxLayout(m_leftBlock);
    l->setContentsMargins(4, 0, 4, 0);
    l->setSpacing(2);
    l->addWidget(m_editBtn);
    l->addWidget(m_tagBtn);
    l->addWidget(m_retranslateBtn);
    l->addWidget(m_copyImageBtn);
    l->addWidget(m_copyContextBtn);
    l->addWidget(m_lockBtn);
    l->addWidget(m_contextBtn);
    l->addWidget(m_archiveBtn);
    m_leftBlock->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  }

  {
    QHBoxLayout *bar = new QHBoxLayout(m_toolBar);
    bar->setContentsMargins(0, 0, 4, 0);
    bar->setSpacing(0);
    bar->addWidget(m_leftBlock);
    bar->addStretch();
    // Settings is pinned to the far right, away from the content actions.
    bar->addWidget(m_settingsBtn);
  }

  // Bottom pager: same idea as the archive window - navigation does not belong in the
  // action row.
  m_pagerBar = new QWidget(this);
  m_pagerBar->setObjectName("resultPagerBar");
  m_pagerBar->setAttribute(Qt::WA_StyledBackground, true);
  {
    QHBoxLayout *pb = new QHBoxLayout(m_pagerBar);
    pb->setContentsMargins(4, 0, 4, 0);
    pb->setSpacing(6);

    // Choose which saved profile's translation prompt is used, without leaving the window.
    m_promptBtn = new QToolButton(m_pagerBar);
    m_promptBtn->setObjectName("promptPickerBtn");
    m_promptBtn->setText(QString::fromUtf8("📝"));
    m_promptBtn->setAutoRaise(true);
    m_promptBtn->setFocusPolicy(Qt::NoFocus);
    m_promptBtn->setCursor(Qt::PointingHandCursor);
    m_promptBtn->setToolTip(tr("Switch profile"));
    connect(m_promptBtn, &QToolButton::clicked, this, [this]()
            {
        if (!m_promptBtn)
            return;
        emit promptPickerRequested(m_promptBtn->mapToGlobal(QPoint(0, -4))); });
    pb->addWidget(m_promptBtn);

    pb->addStretch();

    m_pagingGroup = new QWidget(m_pagerBar);
    m_pagingGroup->setObjectName("pagingGroup");
    QHBoxLayout *p = new QHBoxLayout(m_pagingGroup);
    p->setContentsMargins(0, 0, 0, 0);
    p->setSpacing(6);

    // Jump five entries at once: long conversations are painful one-by-one.
    m_prev5Btn = new QToolButton(m_pagingGroup);
    m_prev5Btn->setObjectName("prev5Btn");
    m_prev5Btn->setText(QString::fromUtf8("\u00AB"));
    m_prev5Btn->setAutoRaise(true);
    m_prev5Btn->setFocusPolicy(Qt::NoFocus);
    m_prev5Btn->setCursor(Qt::PointingHandCursor);
    m_prev5Btn->setToolTip(tr("Back 5 entries"));
    connect(m_prev5Btn, &QToolButton::clicked, this, [this]()
            { jumpToIndex(m_currentIndex - 5); });

    m_next5Btn = new QToolButton(m_pagingGroup);
    m_next5Btn->setObjectName("next5Btn");
    m_next5Btn->setText(QString::fromUtf8("\u00BB"));
    m_next5Btn->setAutoRaise(true);
    m_next5Btn->setFocusPolicy(Qt::NoFocus);
    m_next5Btn->setCursor(Qt::PointingHandCursor);
    m_next5Btn->setToolTip(tr("Forward 5 entries"));
    connect(m_next5Btn, &QToolButton::clicked, this, [this]()
            { jumpToIndex(m_currentIndex + 5); });

    p->addWidget(m_prev5Btn);
    p->addWidget(m_prevBtn);
    p->addWidget(m_pageLabel);
    p->addWidget(m_nextBtn);
    p->addWidget(m_next5Btn);
    m_pagingGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // Uniform square hit areas: ASCII "<" and the "«" glyph have very different widths,
    // which made the row look lopsided.
    const int navSize = 22;
    m_prev5Btn->setFixedSize(navSize, navSize);
    m_prevBtn->setFixedSize(navSize, navSize);
    m_nextBtn->setFixedSize(navSize, navSize);
    m_next5Btn->setFixedSize(navSize, navSize);

    pb->addWidget(m_pagingGroup);
    pb->addStretch();

    // Balance the row: an empty spacer as wide as the prompt button keeps the pager exactly
    // centred in the window instead of being pushed right by that button's width.
    auto *pagerBalancer = new QWidget(m_pagerBar);
    pagerBalancer->setFixedWidth(m_promptBtn->sizeHint().width());
    pb->addWidget(pagerBalancer);
  }

  updateToolbarResponsive();

  // --- JS BINDINGS ---
  m_webView->bind("log", [this](std::string seq, std::string req, void *arg)
                  {
         QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req));
         if (doc.isArray()) {
             qInfo() << "WebView Log:" << doc.array().at(1).toString();
         } });

  m_webView->bind("js_error", [this](std::string seq, std::string req, void *arg)
                  {
         QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req));
         if (doc.isArray()) {
             QJsonArray arr = doc.array();
             qWarning() << "JS ERROR:" << arr.at(1).toString() << "Line:" << (arr.count() > 2 ? arr.at(2).toInt() : 0);
         } });

  m_webView->bind("cmd_showScreenshot", [this](std::string seq, std::string req, void *arg)
                  {
         if (m_currentIndex >= 0 && m_currentIndex < m_history.size()) {
             emit screenshotRequested(m_history[m_currentIndex].id, m_history[m_currentIndex].originalBase64);
         } else if (!m_originalBase64.isEmpty()) {
             emit screenshotRequested(m_entryId, m_originalBase64);
         } });

  m_webView->bind("cmd_updateStatus", [this](std::string seq, std::string req, void *arg)
                  {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req));
        QString mode = doc.isArray() ? doc.array().at(0).toString() : QString::fromUtf8(req.c_str());
        QMetaObject::invokeMethod(this, [this, mode](){ 
            m_statusMode = mode;
            applyStatusLabelText();
            updateToolbarResponsive();
            if (m_editAction)
              m_editAction->setChecked(mode == "edit");
            if (mode == "view" || mode == "view_done") {
              requestFocusToWeb(false);
            }
        }, Qt::QueuedConnection); });

  m_webView->bind("cmd_updateContent", [this](std::string seq, std::string req, void *arg)
                  {
         QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req));
         QString newMd = doc.isArray() ? doc.array().at(0).toString() : QString::fromUtf8(req.c_str());
         m_currentMarkdown = newMd; 
         emit contentUpdated(newMd);
         emit contentUpdatedWithId(m_entryId, newMd); });

  m_webView->bind("cmd_showPrevious", [this](std::string seq, std::string req, void *arg)
                  { QMetaObject::invokeMethod(this, "showPrevious", Qt::QueuedConnection); });
  m_webView->bind("cmd_showNext", [this](std::string seq, std::string req, void *arg)
                  { QMetaObject::invokeMethod(this, "showNext", Qt::QueuedConnection); });
  m_webView->bind("cmd_openTagDialog", [this](std::string seq, std::string req, void *arg)
                  { QMetaObject::invokeMethod(this, "openTagDialog", Qt::QueuedConnection); });
  // Action-bar "retranslate" button. Deliberately NOT the F-hotkey path: an explicit
  // click must always re-send the entry on screen instead of walking to a newer one.
  m_webView->bind("cmd_retranslate", [this](std::string, std::string, void *)
                  { QMetaObject::invokeMethod(this, "retranslateCurrent", Qt::QueuedConnection); });
  // Clipboard actions. App owns the clipboard work (multi-image handling needs the
  // history entry, not just this window's cached copy).
  m_webView->bind("cmd_copyScreenshot", [this](std::string, std::string, void *)
                  { emit copyScreenshotRequested(currentEntryId()); });
  m_webView->bind("cmd_copyContext", [this](std::string, std::string, void *)
                  { emit copyContextRequested(currentEntryId()); });
  m_webView->bind("cmd_openDevTools", [this](std::string, std::string, void *)
                  {
        if (m_webView) {
            qDebug() << "[DevTools] Request from JS in ResultWindow";
            m_webView->openDevTools();
        } });

  connect(m_webView.get(), &EmbedWebView::ready, this, [this]()
          {
        setUpdatesEnabled(true);
        constexpr int kToolbarHeight = 46;
        constexpr int kPagerHeight = 30;
        const int webH = qMax(0, height() - kToolbarHeight - kPagerHeight);
        if (m_webContainer)
            m_webContainer->setGeometry(0, kToolbarHeight, width(), webH);
        if (m_pagerBar)
            m_pagerBar->setGeometry(0, kToolbarHeight + webH, width(), kPagerHeight);
        m_webView->setSize(width(), webH);
        // Apply initial theme
    updateTheme(ThemeUtils::isSystemDark());
    requestFocusToWeb(false); });

  updateTheme(ThemeUtils::isSystemDark());

#ifdef _WIN32
  // Register this window for native key forwarding (WebView2 often consumes WM_KEYDOWN).
  WinKeyForwarder::instance().registerResultWindow(this);
#endif
}

void ResultWindow::updateToolbarBalance()
{
  // No-op: toolbar layout is stable via fixed Left/Center/Right blocks.
}

void ResultWindow::triggerScreenshotFromNative()
{
#ifdef _WIN32
  {
    QString msg = QString("[RW] triggerScreenshotFromNative entryId=%1 idx=%2 hist=%3 origB64=%4")
                      .arg(m_entryId)
                      .arg(m_currentIndex)
                      .arg(m_history.size())
                      .arg(m_originalBase64.size());
    QByteArray line = msg.toUtf8();
    WinKeyForwarder::trace(line.constData());
  }
#endif
  // Native hotkey path: do not rely on JS keydown or Qt shortcuts.
  if (m_currentIndex >= 0 && m_currentIndex < m_history.size())
  {
#ifdef _WIN32
    {
      const auto &e = m_history[m_currentIndex];
      QString msg = QString("[RW] emit screenshotRequested from history id=%1 b64=%2")
                        .arg(e.id)
                        .arg(e.originalBase64.size());
      QByteArray line = msg.toUtf8();
      WinKeyForwarder::trace(line.constData());
    }
#endif
    emit screenshotRequested(m_history[m_currentIndex].id, m_history[m_currentIndex].originalBase64);
    return;
  }
  if (!m_originalBase64.isEmpty())
  {
#ifdef _WIN32
    {
      QString msg = QString("[RW] emit screenshotRequested from current entryId=%1 b64=%2")
                        .arg(m_entryId)
                        .arg(m_originalBase64.size());
      QByteArray line = msg.toUtf8();
      WinKeyForwarder::trace(line.constData());
    }
#endif
    emit screenshotRequested(m_entryId, m_originalBase64);
    return;
  }

#ifdef _WIN32
  WinKeyForwarder::trace("[RW] NO EMIT (no valid index and originalBase64 empty)");
#endif
}

void ResultWindow::toggleLock()
{
  m_isLocked = m_lockAction->isChecked();
  m_lockAction->setText(m_isLocked ? "🔒" : "🔓");
#ifdef _WIN32
  SetWindowPos((HWND)this->winId(), m_isLocked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
  setWindowFlags(m_isLocked ? (windowFlags() | Qt::WindowStaysOnTopHint) : (windowFlags() & ~Qt::WindowStaysOnTopHint));
  show();
#endif
  emit lockStateChanged(m_isLocked);
}
void ResultWindow::resizeEvent(QResizeEvent *event)
{
  constexpr int kToolbarHeight = 46;
  constexpr int kPagerHeight = 30;
  const int webH = qMax(0, height() - kToolbarHeight - kPagerHeight);
  if (m_webContainer)
    m_webContainer->setGeometry(0, kToolbarHeight, width(), webH);
  if (m_webView)
    m_webView->setSize(width(), webH);
  if (m_pagerBar)
  {
    m_pagerBar->setGeometry(0, kToolbarHeight + webH, width(), kPagerHeight);
    m_pagerBar->raise();
  }
  if (m_toolBar)
  {
    m_toolBar->setGeometry(0, 0, width(), kToolbarHeight);
    m_toolBar->raise();
    updateToolbarResponsive();
  }
  QSizeGrip *grip = findChild<QSizeGrip *>();
  if (grip)
  {
    grip->move(width() - grip->width(), height() - grip->height());
    grip->raise();
  }
  QWidget::resizeEvent(event);
}

void ResultWindow::applyStatusLabelText()
{
  if (!m_statusLabel)
    return;
  const QString mode = m_statusMode.isEmpty() ? QStringLiteral("view") : m_statusMode;
  const QString up = mode.toUpper();

  const QFontMetrics fm(m_statusLabel->font());
  const int avail = qMax(0, m_statusLabel->maximumWidth());
  const QString full = QStringLiteral("MODE: ") + up;
  const QString shortText = up;
  const QString tiny = up.isEmpty() ? QString() : up.left(1);

  auto fits = [&](const QString &s) -> bool
  {
    // Leave some padding for the pill style.
    return (fm.horizontalAdvance(s) + 12) <= avail;
  };

  if (fits(full))
    m_statusLabel->setText(full);
  else if (fits(shortText))
    m_statusLabel->setText(shortText);
  else
    m_statusLabel->setText(tiny);
}

void ResultWindow::updateToolbarResponsive()
{
  // The toolbar is now a single flat row and the pager lives on its own full-width
  // bottom bar, so there is no width juggling left to do. Only the pager text and the
  // action states need refreshing.
  if (!m_prevBtn || !m_nextBtn)
    return;

  if (m_pageLabel)
  {
    const int cur = qMax(0, m_currentIndex) + 1;
    const int total = qMax(1, m_history.size());
    m_pageLabel->setText(QString(" %1 / %2 ").arg(cur).arg(total));
  }

  m_prevBtn->setEnabled(m_currentIndex > 0);
  m_nextBtn->setEnabled(m_currentIndex < m_history.size() - 1);
  if (m_prev5Btn)
    m_prev5Btn->setEnabled(m_currentIndex > 0);
  if (m_next5Btn)
    m_next5Btn->setEnabled(m_currentIndex >= 0 && m_currentIndex < m_history.size() - 1);

  if (m_pagerBar)
    m_pagerBar->setVisible(!m_history.isEmpty());
}
void ResultWindow::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);

  if (m_isFirstLoad)
  {
    m_isFirstLoad = false;
    requestFocusToWeb(false);
  }
}
void ResultWindow::closeEvent(QCloseEvent *event)
{
  emit closed();
  event->accept();
}
bool ResultWindow::event(QEvent *event)
{
  if (event->type() == QEvent::WindowActivate)
  {
    requestFocusToWeb(false);
  }
  return QWidget::event(event);
}
void ResultWindow::requestFocusToWeb(bool allowActivate)
{
  if (m_focusPending)
    return;

  m_focusPending = true;

  QTimer::singleShot(0, this, [this, allowActivate]()
                     {
                       m_focusPending = false;

                       if (!isVisible())
                         return;

                       if (!isActiveWindow())
                       {
                         if (allowActivate)
                         {
                           activateWindow();
                           raise();
                         }
                         else
                         {
                           return;
                         }
                       }

                       if (m_lastFocus.isValid() && m_lastFocus.elapsed() < 80)
                         return;

                       m_lastFocus.restart();

                       // 避免 Qt 先 setFocus/clearFocus 导致 WebView 页面触发 blur。
                       // 这里直接把焦点给到 WebView 即可。
                       focusEditor();

#ifdef _WIN32
                       HWND f = GetFocus();
                       wchar_t cls[256] = {0};
                       if (f)
                         GetClassNameW(f, cls, 255);
                       qDebug() << "[Focus] GetFocus=" << (void *)f << " class=" << QString::fromWCharArray(cls);
#endif
                     });
}
void ResultWindow::focusEditor()
{
  if (m_webView)
  {
    m_webView->focusNative();
    QTimer::singleShot(0, this, [this]()
                       {
        if (m_webView)
          m_webView->eval(R"JS((()=>{try{
  const kt = document.getElementById('keytrap');
  if (kt && kt.focus) { kt.focus({preventScroll:true}); return; }
  document.body.tabIndex = 0;
  document.body.focus({preventScroll:true});
}catch(e){}})();
)JS"); });
  }
}
void ResultWindow::showLoading() {}
void ResultWindow::showError(const QString &m) {}
ResultWindow::~ResultWindow()
{
#ifdef _WIN32
  WinKeyForwarder::instance().unregisterResultWindow(this);
#endif
  qDebug() << "[RW] dtor this=" << (void *)this;
}

QString ResultWindow::currentEntryId() const
{
  if (m_currentIndex >= 0 && m_currentIndex < m_history.size())
    return m_history.at(m_currentIndex).id;
  return m_entryId;
}

void ResultWindow::jumpToIndex(int index)
{
  if (m_history.isEmpty())
    return;

  const int target = qBound(0, index, m_history.size() - 1);
  if (target == m_currentIndex)
    return;

  m_currentIndex = target;
  TranslationEntry entry = m_history.at(target);

  // Refresh from storage so a stale cached "Processing..." never comes back.
  if (m_historyManager && !entry.id.isEmpty())
  {
    const TranslationEntry fresh = m_historyManager->getEntryById(entry.id);
    if (!fresh.id.isEmpty())
    {
      m_history[target] = fresh;
      entry = fresh;
    }
  }

  setContent(entry.translatedMarkdown, entry.originalBase64, entry.originalBase64List, entry.prompt, entry.id);
  updateNavigation();
}

bool ResultWindow::eventFilter(QObject *watched, QEvent *event)
{
  // Clicking the page indicator opens the thumbnail browser: the two entry points were
  // merged so the toolbar does not need a separate button.
  if (watched == m_pageLabel && event->type() == QEvent::MouseButtonRelease)
  {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton)
    {
      showThumbnailBrowser();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ResultWindow::showThumbnailBrowser()
{
  if (m_history.isEmpty())
    return;

  // On-demand thumbnail index of the entries this window is paging through. Opened from
  // the toolbar, never shown by default.
  auto *dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Thumbnails"));
  dialog->resize(600, 460);

  auto *scroll = new QScrollArea(dialog);
  scroll->setWidgetResizable(true);

  auto *host = new QWidget(scroll);
  auto *grid = new QGridLayout(host);
  grid->setContentsMargins(8, 8, 8, 8);
  grid->setSpacing(8);

  const int columns = 4;
  for (int i = 0; i < m_history.size(); ++i)
  {
    const TranslationEntry &entry = m_history.at(i);

    auto *button = new QToolButton(host);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(120, 80));
    button->setFixedSize(140, 118);
    button->setText(QString::number(i + 1));
    button->setToolTip(entry.timestamp.toString("yyyy-MM-dd HH:mm:ss"));

    QImage image;
    image.loadFromData(QByteArray::fromBase64(entry.originalBase64.toLatin1()));
    if (!image.isNull())
    {
      const QPixmap thumb = QPixmap::fromImage(
          image.scaled(120, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      button->setIcon(QIcon(thumb));
    }

    if (i == m_currentIndex)
    {
      button->setCheckable(true);
      button->setChecked(true);
    }

    const int target = i;
    connect(button, &QToolButton::clicked, this, [this, dialog, target]()
            {
      jumpToIndex(target);
      dialog->close(); });

    grid->addWidget(button, i / columns, i % columns);
  }

  scroll->setWidget(host);

  auto *layout = new QVBoxLayout(dialog);
  layout->addWidget(scroll);
  dialog->show();
}

void ResultWindow::showToast(const QString &text)
{
  if (!m_webView || text.isEmpty())
    return;

  // Hand the string to JS as a real JSON array so quoting/escaping cannot break out.
  QJsonArray args;
  args.append(text);
  const QString js = QString("if(window.showCopyToast) window.showCopyToast.apply(null, %1);")
                         .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
  m_webView->eval(js.toStdString());
}

void ResultWindow::setContextUsed(int turns)
{
  m_contextUsed = qMax(0, turns);
  if (!m_webView)
    return;

  m_webView->eval(QString("window.__setContextTurns && window.__setContextTurns(%1);")
                      .arg(m_contextUsed)
                      .toStdString());
}

void ResultWindow::setContextInfo(bool enabled, int depth)
{
  m_contextDepth = qMax(0, depth);

  TranslationManager &tm = TranslationManager::instance();

  if (!m_contextAction)
    return;

  // The signal is hooked to triggered() (not toggled()), so setChecked() here cannot
  // bounce back into App and start a feedback loop.
  m_contextAction->setChecked(enabled);

  QString tip = tm.tr("tip_continue_context");
  if (enabled)
    tip += "\n" + tm.tr("tip_context_depth").arg(m_contextDepth);
  m_contextAction->setToolTip(tip);
}

void ResultWindow::updateLanguage()
{
    // Update window title
    setWindowTitle(TranslationManager::instance().tr("result_window_title"));
    
    // Update toolbar tooltips
    if (m_lockAction)
        m_lockAction->setToolTip(tr("Lock Window"));

    // Re-label the in-page action buttons without reloading the document.
    pushActionBarLabels();

    if (m_settingsAction)
        m_settingsAction->setToolTip(TranslationManager::instance().tr("settings_title"));
    if (m_archiveAction)
        m_archiveAction->setToolTip(TranslationManager::instance().tr("summary_title"));

    // Refresh the context tooltip in the new language.
    setContextInfo(m_contextAction && m_contextAction->isChecked(), m_contextDepth);
}
