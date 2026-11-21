#include "App.h"

#include "TranslationManager.h"

#include <QtGlobal>

void App::onApiSuccess(const QString &result, const QString &originalBase64, const QString &originalPrompt,
                       const QString &requestId, qint64 elapsedMs)
{
    Q_UNUSED(originalBase64);
    Q_UNUSED(originalPrompt);

    const QString entryId = requestId;
    const int durationMs = static_cast<int>(qBound<qint64>(0, elapsedMs, 2147483647LL));

    // Track translation completed (success)
    AppConfig cfg = m_configManager.getConfig();
    if (m_analytics)
        m_analytics->trackTranslationCompleted(cfg.apiProvider, true, durationMs);

    // The context marker is part of the translation TEXT on purpose: copying or exporting
    // the text then carries it along, and it stays readable outside this app.
    const TranslationEntry stored = m_historyManager.getEntryById(entryId);
    QString content = result;
    if (stored.contextTurns > 0)
    {
        content = QString("> %1\n\n%2")
                      .arg(TranslationManager::instance().tr("ab_context_note").arg(stored.contextTurns),
                           result);
    }

    if (m_summaryWindow)
    {
        m_summaryWindow->updateEntry(entryId, content);
    }

    m_historyManager.updateEntryContent(entryId, content);

    // Feed the "continue translation" conversation. Only while the toggle is on, so a
    // disabled conversation does not silently accumulate text that would apply later.
    if (cfg.inheritTranslationContext)
        appendTranslationToContext(result);
}

void App::onApiError(const QString &error, const QString &requestId, qint64 elapsedMs)
{
    const QString entryId = requestId;
    const int durationMs = static_cast<int>(qBound<qint64>(0, elapsedMs, 2147483647LL));

    // Track translation completed (failure)
    AppConfig cfg = m_configManager.getConfig();
    if (m_analytics)
        m_analytics->trackTranslationCompleted(cfg.apiProvider, false, durationMs);

    const QString errorText = "Error: " + error;
    if (m_summaryWindow)
    {
        m_summaryWindow->updateEntry(entryId, errorText);
    }
    // Persist the failure so history/result windows stay in sync
    m_historyManager.updateEntryContent(entryId, errorText);
}
