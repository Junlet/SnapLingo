#include "App.h"

#include "ThemeUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include "HintPopup.h"

#ifdef _WIN32
#include "WinKeyForwarder.h"
#endif

namespace
{

void bringWindowToFront(QWidget *window)
{
    if (!window)
        return;

    window->show();
    window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    window->raise();
    window->activateWindow();

    // Some Windows setups ignore the first activation attempt.
    QTimer::singleShot(0, window, [window]()
                       {
        if (!window)
            return;
        window->raise();
        window->activateWindow(); });
    QTimer::singleShot(150, window, [window]()
                       {
        if (!window)
            return;
        window->raise();
        window->activateWindow(); });
}

} // namespace

void App::showSummary()
{
    // Track summary window open
    if (m_analytics)
        m_analytics->trackSummaryWindowOpened();

    if (m_summaryWindow)
    {
        if (m_summaryWindow->isVisible())
        {
            if (m_summaryWindow->isActiveWindow())
            {
                // Save geometry before hiding
                AppConfig cfg = m_configManager.getConfig();
                cfg.summaryWindowGeometry = m_summaryWindow->saveGeometry();
                m_configManager.setConfig(cfg);
                m_configManager.saveConfig();
                m_summaryWindow->captureScrollPosition();
                QSettings settings(ConfigManager::settingsIniPath(), QSettings::IniFormat);
                settings.setValue("summaryWindow/scrollY", m_summaryWindow->getLastScrollY());
                m_summaryWindow->hide();
            }
            else
            {
                m_summaryWindow->setWindowState((m_summaryWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
                m_summaryWindow->raise();
                m_summaryWindow->activateWindow();
            }
        }
        else
        {
            // Refresh archive from storage before showing
            m_summaryWindow->reloadFromStorage(false);

            // Fix white flash: Disable updates during show/restore
            m_summaryWindow->setUpdatesEnabled(false);

            // Restore geometry before showing to prevent flicker
            AppConfig cfg = m_configManager.getConfig();
            if (!cfg.summaryWindowGeometry.isEmpty())
            {
                m_summaryWindow->restoreGeometry(cfg.summaryWindowGeometry);
            }
            m_summaryWindow->show();
            m_summaryWindow->setWindowState((m_summaryWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            m_summaryWindow->raise();
            m_summaryWindow->activateWindow();

            // Show archive-window hints unless user opted out.
            HintPopup::maybeShow(HintPopup::Kind::ArchiveWindow, m_summaryWindow, m_configManager.getConfig());

            // Re-enable updates in next event loop
            QTimer::singleShot(0, m_summaryWindow, [w = m_summaryWindow]()
                               {
                if (!w) return;
                w->setUpdatesEnabled(true);
                w->update(); });
        }
    }
}

void App::showResult(const QString &entryId)
{
    TranslationEntry entry = m_historyManager.getEntryById(entryId);
    if (entry.id.isEmpty())
        return;

    AppConfig cfg = m_configManager.getConfig();

    // If there are locked ResultWindows, reuse them instead of creating a new one.
    // Multiple locked windows should stay in sync by receiving the same new entry.
    QList<ResultWindow *> lockedWindows;
    pruneActiveWindows();
    for (auto w : m_activeWindows)
    {
        ResultWindow *rw = qobject_cast<ResultWindow *>(w.data());
        if (rw && rw->isLocked())
        {
            lockedWindows.append(rw);
        }
    }

    if (!lockedWindows.isEmpty())
    {
        ResultWindow *windowToActivate = nullptr;
        for (ResultWindow *rw : lockedWindows)
        {
            if (!rw)
                continue;

            // Ensure screenshot toggle ("s") works for reused locked windows as well.
            connect(rw, &ResultWindow::screenshotRequested,
                    this, &App::onResultWindowScreenshotRequested,
                    Qt::UniqueConnection);

            // Connect retranslate signal for locked windows
            connect(rw, &ResultWindow::retranslateRequested,
                    this, &App::onRetranslateRequested,
                    Qt::UniqueConnection);

            // Continue-context toggle, plus the current conversation depth.
            connect(rw, &ResultWindow::inheritContextToggled,
                    this, &App::onInheritContextToggled,
                    Qt::UniqueConnection);
            connect(rw, &ResultWindow::copyScreenshotRequested,
                    this, &App::onCopyScreenshotRequested,
                    Qt::UniqueConnection);
            connect(rw, &ResultWindow::copyContextRequested,
                    this, &App::onCopyContextRequested,
                    Qt::UniqueConnection);
            connect(rw, &ResultWindow::settingsRequested,
                    this, &App::showConfig,
                    Qt::UniqueConnection);
            connect(rw, &ResultWindow::archiveRequested,
                    this, &App::showSummary,
                    Qt::UniqueConnection);
            connect(rw, &ResultWindow::promptPickerRequested,
                    this, &App::showPromptPicker,
                    Qt::UniqueConnection);

            AppConfig cfgForLocked = cfg;
            cfgForLocked.defaultResultWindowLocked = rw->isLocked();
            rw->setConfig(cfgForLocked);
            rw->configureHotkeys(cfg.viewToggleHotkey, cfg.editHotkey, cfg.screenshotToggleHotkey,
                                 cfg.boldHotkey, cfg.underlineHotkey, cfg.highlightHotkey,
                                 cfg.prevResultShortcut, cfg.nextResultShortcut,
                                 cfg.tagHotkey, cfg.retranslateHotkey);
            rw->setHistoryManager(&m_historyManager);
            rw->addEntry(entry);
            rw->setContextInfo(m_configManager.getConfig().inheritTranslationContext,
                               static_cast<int>(m_translationContext.size()));

            if (rw->windowState() & Qt::WindowMinimized)
                rw->setWindowState(rw->windowState() & ~Qt::WindowMinimized);
            rw->show();
            rw->raise();
            windowToActivate = rw;
        }

        if (windowToActivate)
        {
            bringWindowToFront(windowToActivate);
        }
        return;
    }

    ResultWindow *window = new ResultWindow();

    // Determine initial lock state for newly created ResultWindow
    cfg.defaultResultWindowLocked = m_preferredLockState;

    window->setConfig(cfg);
    window->configureHotkeys(cfg.viewToggleHotkey, cfg.editHotkey, cfg.screenshotToggleHotkey,
                             cfg.boldHotkey, cfg.underlineHotkey, cfg.highlightHotkey,
                             cfg.prevResultShortcut, cfg.nextResultShortcut,
                             cfg.tagHotkey, cfg.retranslateHotkey);
    window->setHistoryManager(&m_historyManager);

    // Connect Screenshot Request (Fixes 's' hotkey)
    connect(window, &ResultWindow::screenshotRequested,
            this, &App::onResultWindowScreenshotRequested,
            Qt::UniqueConnection);

    // Connect Retranslate Request (Ctrl+R)
    connect(window, &ResultWindow::retranslateRequested,
            this, &App::onRetranslateRequested,
            Qt::UniqueConnection);

    connect(window, &ResultWindow::inheritContextToggled,
            this, &App::onInheritContextToggled,
            Qt::UniqueConnection);

    connect(window, &ResultWindow::copyScreenshotRequested,
            this, &App::onCopyScreenshotRequested,
            Qt::UniqueConnection);
    connect(window, &ResultWindow::copyContextRequested,
            this, &App::onCopyContextRequested,
            Qt::UniqueConnection);

    connect(window, &ResultWindow::settingsRequested,
            this, &App::showConfig,
            Qt::UniqueConnection);
    connect(window, &ResultWindow::archiveRequested,
            this, &App::showSummary,
            Qt::UniqueConnection);

    connect(window, &ResultWindow::promptPickerRequested,
            this, &App::showPromptPicker,
            Qt::UniqueConnection);

    window->setContent(entry.translatedMarkdown, entry.originalBase64, entry.originalBase64List, entry.prompt, entry.id);
    window->setContextUsed(entry.contextTurns);
    window->setContextInfo(m_configManager.getConfig().inheritTranslationContext,
                           static_cast<int>(m_translationContext.size()));
    bringWindowToFront(window);

    // Show result-window hints unless user opted out.
    HintPopup::maybeShow(HintPopup::Kind::ResultWindow, window, cfg);

    trackActiveWindow(window);

    connect(window, &ResultWindow::closed, this, [this, window]()
            {
         m_activeWindows.removeAll(window);
         bool wasLocked = window->isLocked();
         bool otherLocked = false;
         for(auto w : m_activeWindows) {
             if (auto rw = qobject_cast<ResultWindow*>(w.data())) {
                 if (rw != window && rw->isVisible() && rw->isLocked()) {
                     otherLocked = true;
                     break;
                 }
             }
         }

         if (!otherLocked) {
             AppConfig c = m_configManager.getConfig();
             if (c.lockBehavior == 0) {
                 m_preferredLockState = false;
             } else {
                 m_preferredLockState = wasLocked;
             }
         } });

    connect(window, &ResultWindow::contentUpdatedWithId, this, [this](const QString &id, const QString &newMarkdown)
            { m_historyManager.updateEntryContent(id, newMarkdown); });

    connect(window, &ResultWindow::tagsUpdated, this, [this](const QString &id, const QStringList &tags)
            { m_historyManager.updateEntryTags(id, tags); });

    connect(window, &ResultWindow::lockStateChanged, this, [this](bool locked)
            {
        if (m_analytics)
            m_analytics->trackResultWindowLocked(locked); });
}

void App::onResultWindowScreenshotRequested(const QString &entryId, const QString &base64)
{
#ifdef _WIN32
    {
        QString msg = QString("[APP] got screenshotRequested id=%1 b64=%2")
                          .arg(entryId)
                          .arg(base64.size());
        QByteArray line = msg.toUtf8();
        WinKeyForwarder::trace(line.constData());
    }
#endif
    Q_UNUSED(base64);
    restorePreview(entryId);
}

void App::cancelPreviewImageRelease(const QString &entryId)
{
    auto timer = m_previewReleaseTimers.value(entryId);
    if (timer)
    {
        timer->stop();
        timer->deleteLater();
    }
    m_previewReleaseTimers.remove(entryId);
}

void App::schedulePreviewImageRelease(const QString &entryId)
{
    if (entryId.isEmpty())
        return;

    cancelPreviewImageRelease(entryId);

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, entryId, timer]()
            {
                m_previewReleaseTimers.remove(entryId);
                timer->deleteLater();

                // If the preview is visible again, keep cache alive.
                if (m_previewCards.contains(entryId) && m_previewCards[entryId])
                    return;

                m_previewImageCache.remove(entryId);
#ifdef _WIN32
                {
                    QString msg = QString("[APP] preview cache released entryId=%1").arg(entryId);
                    QByteArray line = msg.toUtf8();
                    WinKeyForwarder::trace(line.constData());
                }
#endif
            });
    m_previewReleaseTimers[entryId] = timer;
    timer->start(5000);
}

void App::restorePreview(const QString &entryId)
{
#ifdef _WIN32
    {
        QString msg = QString("[APP] restorePreview entryId=%1").arg(entryId);
        QByteArray line = msg.toUtf8();
        WinKeyForwarder::trace(line.constData());
    }
#endif

    if (entryId.isEmpty())
    {
#ifdef _WIN32
        WinKeyForwarder::trace("[APP] restorePreview abort: empty entryId");
#endif
        return;
    }

    // Toggle behavior: if already open for this entry, close it.
    if (m_previewCards.contains(entryId) && m_previewCards[entryId])
    {
#ifdef _WIN32
        WinKeyForwarder::trace("[APP] restorePreview toggle: closing existing PreviewCard");
#endif
        m_previewCards[entryId]->close();
        schedulePreviewImageRelease(entryId);
        // QPointer will null it out automatically because WA_DeleteOnClose is set
        return;
    }

    cancelPreviewImageRelease(entryId);

    AppConfig cfg = m_configManager.getConfig();
    QList<QPixmap> pixmaps = m_previewImageCache.value(entryId);
    QPixmap pixmap = pixmaps.value(0);

    if (pixmap.isNull())
    {
        TranslationEntry entry = m_historyManager.getEntryById(entryId);
        if (entry.id.isEmpty())
        {
#ifdef _WIN32
            WinKeyForwarder::trace("[APP] restorePreview abort: entry not found in history");
#endif
            return;
        }

#ifdef _WIN32
        {
            QString msg = QString("[APP] entry found: b64=%1").arg(entry.originalBase64.size());
            QByteArray line = msg.toUtf8();
            WinKeyForwarder::trace(line.constData());
        }
#endif

        const QStringList base64Images = entry.originalBase64List.isEmpty()
                                             ? QStringList{entry.originalBase64}
                                             : entry.originalBase64List;
        for (const QString &base64Image : base64Images)
        {
            QByteArray bytes = QByteArray::fromBase64(base64Image.toLatin1());
#ifdef _WIN32
            {
                QString msg = QString("[APP] decoded bytes=%1").arg(bytes.size());
                QByteArray line = msg.toUtf8();
                WinKeyForwarder::trace(line.constData());
            }
#endif
            QPixmap decoded;
            decoded.loadFromData(bytes);
            if (!decoded.isNull())
                pixmaps.append(decoded);
        }

        if (base64Images.isEmpty())
        {
#ifdef _WIN32
            WinKeyForwarder::trace("[APP] restorePreview abort: entry.originalBase64 empty");
#endif
        }

        pixmap = pixmaps.value(0);
        if (pixmap.isNull())
        {
#ifdef _WIN32
            WinKeyForwarder::trace("[APP] restorePreview abort: pixmap.isNull (decode failed)");
#endif
            return;
        }
    }

#ifdef _WIN32
    {
        const qreal rawDpr = pixmap.devicePixelRatio();
        const QSize rawPx = pixmap.size();
        const QSizeF rawDip = pixmap.deviceIndependentSize();
        QString msg = QString("[APP] pixmap OK rawPx=%1x%2 rawDpr=%3 rawDip=%4x%5")
                          .arg(rawPx.width())
                          .arg(rawPx.height())
                          .arg(rawDpr, 0, 'f', 3)
                          .arg(rawDip.width(), 0, 'f', 1)
                          .arg(rawDip.height(), 0, 'f', 1);
        QByteArray line = msg.toUtf8();
        WinKeyForwarder::trace(line.constData());
    }
#endif

    // If the screenshot came from a HiDPI monitor, the captured image is usually in physical pixels,
    // but decoding from PNG loses the original DPR metadata. Restore an appropriate DPR so the
    // PreviewCard (which sizes itself by deviceIndependentSize) doesn't jump larger after toggling.
    {
        qreal desiredDpr = 1.0;
        const QList<QScreen *> screens = QGuiApplication::screens();
        const int idx = cfg.targetScreenIndex;
        if (idx >= 0 && idx < screens.size() && screens[idx])
            desiredDpr = screens[idx]->devicePixelRatio();
        else if (QGuiApplication::primaryScreen())
            desiredDpr = QGuiApplication::primaryScreen()->devicePixelRatio();

        if (desiredDpr <= 0.0)
            desiredDpr = 1.0;

        if (qAbs(pixmap.devicePixelRatio() - desiredDpr) > 0.001)
        {
            pixmap.setDevicePixelRatio(desiredDpr);
#ifdef _WIN32
            {
                const QSize px = pixmap.size();
                const QSizeF dip = pixmap.deviceIndependentSize();
                QString msg = QString("[APP] pixmap DPR adjusted to %1 -> dip=%2x%3 (px=%4x%5)")
                                  .arg(desiredDpr, 0, 'f', 3)
                                  .arg(dip.width(), 0, 'f', 1)
                                  .arg(dip.height(), 0, 'f', 1)
                                  .arg(px.width())
                                  .arg(px.height());
                QByteArray line = msg.toUtf8();
                WinKeyForwarder::trace(line.constData());
            }
#endif
        }
    }

    // Keep one decoded image in-memory per preview entry; released 5s after close.
    for (QPixmap &image : pixmaps)
    {
        if (qAbs(image.devicePixelRatio() - pixmap.devicePixelRatio()) > 0.001)
            image.setDevicePixelRatio(pixmap.devicePixelRatio());
    }
    m_previewImageCache[entryId] = pixmaps;

    PreviewCard *card = new PreviewCard(pixmaps);
    card->setZoomSensitivity(cfg.zoomSensitivity);
    card->setBorderColor(cfg.cardBorderColor);
    card->setUseBorder(cfg.useCardBorder);
    card->setNavigationHotkeys(cfg.prevResultShortcut, cfg.nextResultShortcut);

    // Restore geometry if available
    if (m_previewGeometries.contains(entryId))
    {
        QRect geom = m_previewGeometries[entryId];
        card->resize(geom.size());
        card->move(geom.topLeft());
    }

    card->show();
    trackActiveWindow(card);
    m_previewCards[entryId] = card;

    connect(card, &PreviewCard::closedWithGeometry, this, [this, entryId](QPoint pos, QSize size)
            { m_previewGeometries[entryId] = QRect(pos, size); });

    connect(card, &PreviewCard::closed, [this, card, entryId]()
            {
                m_activeWindows.removeAll(card);
                m_previewCards.remove(entryId);
                schedulePreviewImageRelease(entryId);
            });
}

void App::onRetranslateRequested(const QStringList &base64Images)
{
    // Track retranslation
    if (m_analytics)
        m_analytics->trackRetranslation();

    if (base64Images.isEmpty())
    {
        qWarning() << "[App] onRetranslateRequested: empty base64 image list";
        return;
    }

    AppConfig cfg = m_configManager.getConfig();

    if (!cfg.useAdvancedApiMode && cfg.apiKey.isEmpty())
    {
        qWarning() << "[App] onRetranslateRequested: no API key configured";
        return;
    }

    // Generate new entryId for this retranslation
    QString entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    qDebug() << "[App] Retranslating image, new entryId:" << entryId;

    // Create entry in history
    TranslationEntry entry;
    entry.id = entryId;
    entry.timestamp = QDateTime::currentDateTime();
    entry.prompt = cfg.promptText;
    entry.translatedMarkdown = "Processing...";
    // Same rule as the screenshot path: record how much context this request carried.
    entry.contextTurns = (cfg.inheritTranslationContext && !m_translationContext.isEmpty())
                             ? static_cast<int>(m_translationContext.size())
                             : 0;
    entry.originalBase64 = base64Images.first();
    entry.originalBase64List = base64Images;

    m_historyManager.saveEntry(entry);

    // Show result window
    if (cfg.showResultWindow)
    {
        showResult(entryId);
    }

    // Convert QString apiProvider to enum
    ApiProvider provider = ApiProvider::OpenAI; // default
    QString providerStr = cfg.apiProvider.trimmed().toLower();
    if (providerStr == "gemini")
        provider = ApiProvider::Gemini;
    else if (providerStr == "claude")
        provider = ApiProvider::Claude;

    m_apiClient->configure(cfg.apiKey, cfg.baseUrl, cfg.modelName, provider, cfg.useProxy,
                           cfg.proxyUrl, cfg.endpointPath, cfg.useAdvancedApiMode, cfg.advancedApiTemplate);

    // Track translation started (retranslation)
    if (m_analytics)
        m_analytics->trackTranslationStarted(cfg.apiProvider, cfg.useAdvancedApiMode);

    // Store entryId in heap to pass as context
    QList<QByteArray> base64Bytes;
    base64Bytes.reserve(base64Images.size());
    for (const QString &base64Image : base64Images)
        base64Bytes.append(base64Image.toLatin1());

    m_apiClient->processImages(base64Bytes, buildContextPrompt(cfg.promptText), entryId);

    qDebug() << "[App] Retranslation request sent for entry:" << entryId;
}

QString App::buildContextPrompt(const QString &basePrompt) const
{
    if (m_translationContext.isEmpty())
        return basePrompt;

    QStringList turns;
    for (const QString &entry : m_translationContext)
    {
        const QString text = entry.trimmed();
        if (!text.isEmpty())
            turns << text;
    }

    if (turns.isEmpty())
        return basePrompt;

    // Raw previous translations only: no explanatory wrapper, no instruction text and no
    // depth/size cap. Joined with a plain separator so the model can still tell the
    // turns apart, which is markup rather than prompting.
    return basePrompt + "\n\n" + turns.join("\n\n---\n\n");
}

void App::appendTranslationToContext(const QString &translation)
{
    const QString text = translation.trimmed();
    if (text.isEmpty())
        return;

    // A retranslation produces the same text again; do not stack duplicates.
    if (!m_translationContext.isEmpty() && m_translationContext.last() == text)
        return;

    m_translationContext.append(text);
    pushContextStateToWindows();
}

void App::pushContextStateToWindows()
{
    const bool enabled = m_configManager.getConfig().inheritTranslationContext;
    const int depth = static_cast<int>(m_translationContext.size());

    pruneActiveWindows();
    for (auto w : m_activeWindows)
    {
        if (auto rw = qobject_cast<ResultWindow *>(w.data()))
            rw->setContextInfo(enabled, depth);
    }
}

void App::onInheritContextToggled(bool enabled)
{
    AppConfig cfg = m_configManager.getConfig();
    if (cfg.inheritTranslationContext != enabled)
    {
        cfg.inheritTranslationContext = enabled;
        m_configManager.setConfig(cfg); // persisted, so the toggle survives a restart
    }

    if (!enabled)
        m_translationContext.clear();

    pushContextStateToWindows();
    qDebug() << "[App] inherit translation context ->" << enabled
             << "depth=" << m_translationContext.size();
}

void App::notifyCopied(const QString &entryId, const QString &text)
{
    if (text.isEmpty())
        return;

    pruneActiveWindows();
    for (auto w : m_activeWindows)
    {
        if (auto rw = qobject_cast<ResultWindow *>(w.data()))
        {
            if (entryId.isEmpty() || rw->entryId() == entryId)
                rw->showToast(text);
        }
    }
}

void App::showPromptPicker(const QPoint &globalPos)
{
    const QStringList profiles = m_configManager.listProfiles();
    if (profiles.isEmpty())
        return;

    const QString current = m_configManager.currentProfileName();

    QMenu menu;
    menu.setToolTipsVisible(true);
    for (const QString &name : profiles)
    {
        const QString prompt = m_configManager.profilePromptText(name);

        QAction *action = menu.addAction(name);
        action->setCheckable(true);
        action->setChecked(name == current);

        const QString firstLine = prompt.trimmed().section('\n', 0, 0).trimmed().left(60);
        action->setToolTip(firstLine.isEmpty() ? tr("(empty prompt)") : firstLine);

        connect(action, &QAction::triggered, this, [this, name]()
                { switchToProfile(name); });
    }

    menu.exec(globalPos);
}

void App::switchToProfile(const QString &profileName)
{
    if (profileName == m_configManager.currentProfileName())
        return;

    // Loads the whole profile: API settings, prompt, hotkeys, theme, storage...
    if (!m_configManager.loadProfile(profileName))
        return;

    // Re-apply everything to the running app so the switch takes effect immediately.
    updateConfig(m_configManager.getConfig());

    // A different profile is normally a different task, so the previous conversation must
    // not be carried into it. Made configurable because some users switch between near-
    // identical profiles and want the history kept.
    if (!m_configManager.getConfig().keepContextOnProfileSwitch)
    {
        m_translationContext.clear();
        pushContextStateToWindows();
    }

    notifyCopied(QString(), tr("toast_profile_switched").arg(profileName));
}

void App::onCopyScreenshotRequested(const QString &entryId)
{
    TranslationEntry entry = m_historyManager.getEntryById(entryId);
    QStringList images = entry.originalBase64List.isEmpty()
                             ? QStringList{entry.originalBase64}
                             : entry.originalBase64List;
    images.removeAll(QString());
    if (images.isEmpty())
        return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    QImage first;
    first.loadFromData(QByteArray::fromBase64(images.first().toLatin1()));

    if (images.size() == 1)
    {
        if (first.isNull())
            return;
        clipboard->setImage(first);
        notifyCopied(entryId, tr("toast_image_copied"));
        return;
    }

    // The Windows clipboard can only carry ONE bitmap, so for a batch translation we copy
    // the first image as a bitmap AND every image as a file. Apps that accept attachments
    // (chat inputs, Explorer) then get all of them.
    QDir dir(QDir(QDir::tempPath()).filePath("ai-screenshot-translator/clipboard"));
    if (!dir.mkpath("."))
        return;

    QStringList files;
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < images.size(); ++i)
    {
        QImage img;
        img.loadFromData(QByteArray::fromBase64(images.at(i).toLatin1()));
        if (img.isNull())
            continue;
        const QString path = dir.filePath(QString("shot_%1_%2.png").arg(stamp).arg(i + 1));
        if (img.save(path, "PNG"))
            files << path;
        if (i == 0)
            first = img;
    }

    if (files.isEmpty())
        return;

    auto *mime = new QMimeData();
    if (!first.isNull())
        mime->setImageData(first);
    QList<QUrl> urls;
    urls.reserve(files.size());
    for (const QString &file : files)
        urls << QUrl::fromLocalFile(file);
    mime->setUrls(urls);
    clipboard->setMimeData(mime); // the clipboard takes ownership

    notifyCopied(entryId, tr("toast_images_copied").arg(files.size()));
}

void App::onCopyContextRequested(const QString &entryId)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    // Translations only: the prompt is NOT included, on request. Join the same way the
    // request context does so the pasted text matches what the model saw.
    QString text;
    if (!m_translationContext.isEmpty())
    {
        text = m_translationContext.join("\n\n---\n\n");
    }
    else
    {
        // No conversation yet: copy the entry being shown so the button is still useful.
        TranslationEntry entry = m_historyManager.getEntryById(entryId);
        text = entry.translatedMarkdown.trimmed();
    }

    if (text.isEmpty())
        return;

    clipboard->setText(text);
    notifyCopied(entryId, tr("toast_context_copied")
                              .arg(qMax(1, static_cast<int>(m_translationContext.size()))));
}
