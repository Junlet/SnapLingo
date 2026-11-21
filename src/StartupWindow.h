#ifndef STARTUPWINDOW_H
#define STARTUPWINDOW_H

#include <QDialog>
#include <QPointer>
#include <QJsonObject>

#include "ConfigManager.h"

class QLabel;
class QPushButton;
class QListWidget;
class QFrame;
class QNetworkAccessManager;
class QNetworkReply;
class QEvent;
class QKeyEvent;
class QVBoxLayout;
class HistoryManager;

class StartupWindow : public QDialog
{
    Q_OBJECT
public:
    // What the user picked. Read after exec() returns, so the caller can act once the
    // modal window is gone (starting a screenshot while it is open would fight for focus).
    enum class Action
    {
        None,
        Screenshot,
        Archive,
        Settings
    };

    explicit StartupWindow(const AppConfig &cfg, HistoryManager *historyManager = nullptr,
                           const QString &profileName = QString(), QWidget *parent = nullptr);
    Action requestedAction() const { return m_action; }
    // Set only when the user clicked one of the recent rows in the dashboard.
    QString requestedEntryId() const { return m_requestedEntryId; }

private slots:
    void startUpdateCheck(bool forceNetwork = false);
    void openReleasesPage();
    void openPrivacyPolicy();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr int kUpdateCheckTimeoutMs = 10000;

    void setupWindowChrome();
    void setupDialogUi();
    void setupDashboardHeader(QVBoxLayout *root);
    void setupDashboardActions(QVBoxLayout *root);
    void setupDashboardRecent(QVBoxLayout *root);
    void setupDashboardFooter(QVBoxLayout *root);
    void populateRecentEntries();
    void applyDashboardStyle();
    QString recentSnippet(const QString &markdown) const;
    void addUpdateRow(QVBoxLayout *root);
    void setUpdateStatus(const QString &text);
    void updateHintColor();
    void updateUpdateHighlight(bool hasNewVersion);
    void onUpdateReplyFinished(QNetworkReply *reply);
    QString buildUpdateNetworkErrorStatus(const QNetworkReply *reply) const;

    bool applyCachedUpdateStatusIfFresh();
    void saveUpdateCache(const QString &status,
                         const QString &latestVer = QString(),
                         const QString &latestUrl = QString(),
                         const QString &displayText = QString());

    void loadUiConfig();
    QString formatText(QString text) const;
    QString uiString(const QString &path, const QString &fallback) const;

    AppConfig m_cfg;
    HistoryManager *m_historyManager = nullptr;
    QString m_profileName;
    QString m_requestedEntryId;
    bool m_dashboardStyleApplying = false;

    // UI config (loaded from assets/startup_window.json)
    QJsonObject m_uiConfig;
    QString m_currentVersion;
    QString m_updateApiUrl;
    QString m_repoUrl;
    QString m_updateNotChecked;
    QString m_updateChecking;
    QString m_updateNetworkError;
    QString m_updateParseError;
    QString m_updateNoVersion;
    QString m_updateLatestTpl;
    QString m_updateNewTpl;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_versionPill = nullptr;
    QLabel *m_profileLabel = nullptr;
    QLabel *m_apiStatusLabel = nullptr;
    QListWidget *m_recentList = nullptr;
    QLabel *m_recentEmptyLabel = nullptr;
    QWidget *m_recentEmptyContainer = nullptr;
    QWidget *m_recentSpacer = nullptr;
    QFrame *m_recentCard = nullptr;
    QPushButton *m_viewAllBtn = nullptr;
    QPushButton *m_screenshotBtn = nullptr;
    QPushButton *m_archiveBtn = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    Action m_action = Action::None;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_copyrightLabel = nullptr;
    QLabel *m_hotkeysLabel = nullptr;
    QLabel *m_hintLabel = nullptr;

    bool m_updatingHintColor = false;

    QLabel *m_updateStatusLabel = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;
    QPushButton *m_openReleasesBtn = nullptr;
    QPushButton *m_privacyBtn = nullptr;

    QPointer<QNetworkAccessManager> m_nam;
    QPointer<QNetworkReply> m_reply;

    QString m_latestTag;
    QString m_latestUrl;

    bool m_hasNewVersion = false;
    bool m_showingChildDialog = false;
};

#endif // STARTUPWINDOW_H
