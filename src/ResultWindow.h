#ifndef RESULTWINDOW_H
#define RESULTWINDOW_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <memory>
#include <QJsonArray>
#include <QToolButton>
#include <QAction>
#include <QLabel>
#include <QList>
#include <QElapsedTimer>
#include <QWidget>
#include "TranslationManager.h" // For TranslationEntry struct if defined there, otherwise minimal struct needed here or forward decl
// Actually TranslationEntry is in HistoryManager.h usually, let's check or define a local struct/use shared.
// Based on App.cpp, TranslationManager seems to handle strings. HistoryManager handles persistence.
// TranslationEntry is used in App.cpp line 73. It's likely in HistoryManager.h or a common header.
// Let's assume it's available or we can use a struct here.
// Checking App.cpp includes: HistoryManager.h.
#include "HistoryManager.h"
#include "ConfigManager.h"

class EmbedWebView;

class ResultWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ResultWindow(QWidget *parent = nullptr);
    ~ResultWindow();

    void setContent(const QString &markdown, const QString &originalBase64, const QStringList &originalBase64List,
                    const QString &prompt, const QString &entryId = "");
    void externalContentUpdate(const QString &markdown);

    // Locked Window Methods
    bool isLocked() const { return m_isLocked; }
    void addEntry(const TranslationEntry &entry);
    void updateNavigation();

    QString entryId() const { return m_entryId; }
    void showLoading();
    void showError(const QString &msg);

    void configureHotkeys(const QString &viewToggle, const QString &editToggle,
                          const QString &screenshotToggle, const QString &boldKey,
                          const QString &underlineKey, const QString &highlightKey,
                          const QString &prevKey, const QString &nextKey,
                          const QString &tagKey, const QString &retranslateKey);
    QString retranslateHotkey() const { return m_retranslateKey; }

    void setHistoryManager(class HistoryManager *historyManager);

    void setConfig(const AppConfig &config);
    // Reflects the continue-context state owned by App (see inheritContextToggled).
    void setContextInfo(bool enabled, int depth);
    // Number of earlier translations this entry was translated with (0 = standalone).
    // Rendered as a marker line above the translation.
    void setContextUsed(int turns);
    void focusEditor(); // Restore focus to WebView
    // Transient in-page message (reuses the copy toast of the document).
    void showToast(const QString &text);
    void updateTheme(bool isDark);
    void updateLanguage();

signals:
    void closed();
    void retranslateRequested(const QStringList &base64Images); // Retranslate current image set with current config
    void screenshotRequested(const QString &entryId, const QString &base64);  // New signal
    void contentUpdated(const QString &newMarkdown);                          // Legacy Signal
    void contentUpdatedWithId(const QString &id, const QString &newMarkdown); // Signal for persistence with ID
    void tagsUpdated(const QString &id, const QStringList &tags);             // Signal for tag updates
    void lockStateChanged(bool locked);                                       // Signal for lock state changes
    // The user flipped the "continue context" toggle in this window.
    void inheritContextToggled(bool enabled);
    // Copy this entry's source screenshot(s) / this conversation to the clipboard.
    void copyScreenshotRequested(const QString &entryId);
    void copyContextRequested(const QString &entryId);
    // Toolbar shortcuts to the app's other windows.
    void settingsRequested();
    void archiveRequested();
    // Pick a translation prompt from the saved profiles; anchor is the global popup point.
    void promptPickerRequested(const QPoint &globalPos);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
public slots:
    void triggerScreenshotFromNative();
    void triggerRetranslateFromNative();
    void retranslateCurrent(); // Re-send the entry currently on screen, whatever the lock state
private slots:
    void toggleLock();
    void showPrevious();
    void showNext();
    void openTagDialog();

private:
    void updateToolbarBalance();
    void updateToolbarResponsive();
    void applyStatusLabelText();

    // Action-bar helpers (the in-page buttons that mirror the hotkeys).
    QStringList currentBase64Images() const;
    QString currentEntryId() const;
    void jumpToIndex(int index);
    void showThumbnailBrowser();
    void pushActionBarLabels();
    std::unique_ptr<EmbedWebView> m_webView;
    QWidget *m_webContainer;

    // UI Elements
    // UI Elements
    QWidget *m_toolBar = nullptr;
    QAction *m_lockAction = nullptr;
    QAction *m_contextAction = nullptr; // checkable: continue translation context
    QAction *m_settingsAction = nullptr;
    QAction *m_archiveAction = nullptr;
    // Content actions that used to live in the in-page action bar (now a single row here).
    QAction *m_editAction = nullptr;
    QAction *m_tagAction = nullptr;
    QAction *m_retranslateAction = nullptr;
    QAction *m_copyImageAction = nullptr;
    QAction *m_copyContextAction = nullptr;
    QAction *m_prevAction = nullptr;
    QAction *m_nextAction = nullptr;
    QToolButton *m_lockBtn = nullptr;
    QToolButton *m_contextBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QToolButton *m_archiveBtn = nullptr;
    QToolButton *m_editBtn = nullptr;
    QToolButton *m_tagBtn = nullptr;
    QToolButton *m_retranslateBtn = nullptr;
    QToolButton *m_copyImageBtn = nullptr;
    QToolButton *m_copyContextBtn = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QToolButton *m_prev5Btn = nullptr; // jump 5 entries at once
    QToolButton *m_next5Btn = nullptr;
    int m_contextDepth = 0; // how many earlier translations the conversation holds
    int m_contextUsed = 0;  // how many were used for the entry currently shown
    QWidget *m_leftBlock = nullptr;
    QWidget *m_centerBlock = nullptr;
    QWidget *m_rightBlock = nullptr;
    QWidget *m_pagingGroup = nullptr;
    QWidget *m_pagerBar = nullptr; // bottom bar holding the page controls
    QToolButton *m_promptBtn = nullptr; // bottom-left: choose a translation prompt
    QLabel *m_contextBadge = nullptr;   // shows the conversation depth when context is on
    QLabel *m_pageLabel = nullptr;
    QLabel *m_statusLabel = nullptr; // New status label

    int m_pageMinW = 0;
    int m_pageCompactW = 0;
    int m_pageMaxW = 0;
    int m_statusMinW = 0;
    int m_statusMaxW = 0;
    QString m_statusMode; // e.g. "view", "edit", "raw"

    // State
    bool m_isLocked = false;
    QList<TranslationEntry> m_history;
    int m_currentIndex = -1;

    QString m_originalBase64;
    QStringList m_originalBase64List;
    QString m_originalPrompt;
    QString m_entryId;
    AppConfig m_config;

    // Hotkey configuration
    // State for async retrieval
    QString m_currentMarkdown;
    QString m_currentProtectedText;
    QJsonArray m_currentMathBlocks;

    QString m_viewToggleKey;
    QString m_editToggleKey;
    QString m_screenshotToggleKey;
    QString m_boldKey;
    QString m_underlineKey;
    QString m_highlightKey;
    QString m_prevKey;
    QString m_nextKey;
    QString m_tagKey;
    QString m_retranslateKey;

    // Tag management
    class HistoryManager *m_historyManager = nullptr;
    QStringList m_currentTags;

    QString loadTemplate();

    struct ProtectedContent
    {
        QString text;
        QStringList mathBlocks;
    };

    ProtectedContent protectMath(const QString &markdown);

    // New Members
    QList<QShortcut *> m_navShortcuts;
    void updateShortcuts();
    bool m_isFirstLoad = true;
    bool m_focusPending = false;
    QElapsedTimer m_lastFocus;
    void requestFocusToWeb(bool allowActivate = false);
    bool m_htmlLoaded = false;
};

#endif // RESULTWINDOW_H
