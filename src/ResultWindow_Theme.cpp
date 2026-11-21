#include "ResultWindow.h"

#include "EmbedWebView.h"
#include "ThemeUtils.h"
#include "ColorUtils.h"

#include <QCoreApplication>
#include <QFile>

static QString readTextFileUtf8OrEmpty(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

void ResultWindow::updateTheme(bool isDark)
{
    ThemeUtils::applyThemeToWindow(this, isDark);
    if (m_webView)
    {
        QColor bg = isDark ? QColor(30, 30, 30) : QColor(255, 255, 255);
        m_webView->setBackgroundColor(bg.red(), bg.green(), bg.blue(), 255);
        QString toggleJs = QString("document.documentElement.classList.toggle('dark-mode', %1); document.body.classList.toggle('dark-mode', %1);")
                               .arg(isDark ? "true" : "false");
        m_webView->eval(toggleJs.toStdString());
    }
    const QString qssPath = QCoreApplication::applicationDirPath() + QString(isDark ? "/assets/qss/status_indicator_dark.qss" : "/assets/qss/status_indicator_light.qss");
    const QString statusQss = readTextFileUtf8OrEmpty(qssPath);
    if (m_toolBar)
        m_toolBar->setStyleSheet(statusQss);

    // Bottom pager bar: same treatment as the archive window's pager.
    if (m_pagerBar)
    {
        m_pagerBar->setStyleSheet(QString(
            "QWidget#resultPagerBar{background:%1;border-top:1px solid %2;}"
            "QWidget#resultPagerBar QLabel{color:%3;font-size:12px;}"
            "QWidget#resultPagerBar QToolButton{color:%3;border:none;background:transparent;}"
            "QWidget#resultPagerBar QToolButton:hover{background:%4;border-radius:4px;}"
            "QWidget#resultPagerBar QToolButton:disabled{color:%5;}"
            "QWidget#resultPagerBar QLabel#contextBadge{color:%3;background:%4;"
            "border-radius:8px;padding:1px 6px;font-size:11px;}")
            .arg(isDark ? "#1f1f1f" : "#f6f7f9",
                 isDark ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.08)",
                 isDark ? "#e0e0e0" : "#111111",
                 isDark ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.06)",
                 isDark ? "#6e6e6e" : "#999999"));
    }

    // Only adjust pagination text/icon color with theme; keep transparent background.
    if (m_pagingGroup)
    {
        const QString color = isDark ? "#e0e0e0" : "#111111";
        const QString disabled = isDark ? "#6e6e6e" : "#999999";
        const QString pagingStyle = QString(
            "QWidget#pagingGroup QLabel{color:%1;}"
            "QWidget#pagingGroup QToolButton{color:%1;border:none;background:transparent;}"
            "QWidget#pagingGroup QToolButton:disabled, QWidget#pagingGroup QLabel:disabled{color:%2;}")
                                         .arg(color, disabled);
        m_pagingGroup->setStyleSheet(pagingStyle);
    }
}

void ResultWindow::setConfig(const AppConfig &config)
{
    m_config = config;

    if (m_webView)
    {
        const QString mark = ColorUtils::normalizeCssColor(m_config.highlightMarkColor, "#ffeb3b");
        const QString markDark = ColorUtils::normalizeCssColor(m_config.highlightMarkColorDark, "#d4af37");
        const QString js = QString(
                               "(()=>{try{document.documentElement.style.setProperty('--mark-bg', '%1');"
                               "document.documentElement.style.setProperty('--mark-bg-dark', '%2');}catch(e){}})();")
                               .arg(mark, markDark);
        m_webView->eval(js.toStdString());

        const QString debugJs = QString(
                                    "(()=>{try{if(window.__setAdvancedDebugVisible){window.__setAdvancedDebugVisible(%1);}}catch(e){}})();")
                                    .arg((m_config.useAdvancedApiMode && m_config.showAdvancedDebugInResultWindow) ? "true" : "false");
        m_webView->eval(debugJs.toStdString());
    }

    // Continue-context toggle reflects the persisted setting until App pushes the
    // authoritative state (which also carries the conversation depth).
    if (m_contextAction)
        setContextInfo(m_config.inheritTranslationContext, m_contextDepth);

    // Apply default lock state from config (used when creating new result windows).
    if (m_lockAction)
    {
        const bool wantLocked = m_config.defaultResultWindowLocked;
        if (m_lockAction->isChecked() != wantLocked)
        {
            m_lockAction->setChecked(wantLocked);
            toggleLock();
        }
        else if (m_isLocked != wantLocked)
        {
            // Keep internal state consistent with the action state.
            toggleLock();
        }
    }
}
