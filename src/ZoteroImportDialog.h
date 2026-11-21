#ifndef ZOTEROIMPORTDIALOG_H
#define ZOTEROIMPORTDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>

#include "ZoteroClient.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

// Batch import dialog: shows the Zotero connection state, lets the user pick a
// target collection and creates one note per selected translation.
class ZoteroImportDialog : public QDialog
{
    Q_OBJECT
public:
    ZoteroImportDialog(const QList<ZoteroNote> &notes, int skippedCount,
                       ZoteroClient *client, QWidget *parent = nullptr);

    bool importSucceeded() const { return m_summary.created > 0; }

private:
    void buildUi();
    void startImport();
    void setStatus(const QString &text, bool isError = false);
    QString collectionDisplayName(const ZoteroCollectionInfo &info,
                                  const QHash<QString, ZoteroCollectionInfo> &byKey) const;

    void onServerProbed(bool ok, const QString &zoteroVersion, const QString &errorMessage);
    void onCollectionsFetched(const QList<ZoteroCollectionInfo> &collections, const QString &errorMessage);
    void onProgress(int processed, int total);
    void onFinished(const ZoteroImportSummary &summary);

    QList<ZoteroNote> m_notes;
    int m_skippedCount = 0;
    ZoteroClient *m_client = nullptr;
    ZoteroImportSummary m_summary;
    bool m_serverReady = false;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_resultLabel = nullptr;
    QComboBox *m_collectionCombo = nullptr;
    QCheckBox *m_tagsCheck = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};

#endif // ZOTEROIMPORTDIALOG_H
