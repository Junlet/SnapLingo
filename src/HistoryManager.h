#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QFileSystemWatcher>
#include <QMap>
#include <QSqlDatabase>
#include <QDate>
#include "TranslationEntry.h"

class HistoryManager : public QObject
{
    Q_OBJECT
public:
    HistoryManager(QObject *parent = nullptr);

    static QString normalizeMarkdown(const QString &raw);

    // Set the base storage path. Handles relative/absolute paths.
    void setStoragePath(const QString &path);
    QString getStoragePath() const;

    // Save a new entry to disk (image + metadata)
    void saveEntry(const TranslationEntry &entry);

    // Load all valid entries from disk
    QList<TranslationEntry> loadEntries(); // Non-const now because it updates internal cache
    QList<TranslationEntry> queryEntries(const QDate &fromDate,
                                         const QDate &toDate,
                                         const QStringList &tags,
                                         const QString &searchText,
                                         int limit,
                                         int offset,
                                         int *totalCount = nullptr);

    // Delete an entry from disk
    bool deleteEntry(const QString &id);
    bool deleteEntries(const QStringList &ids);

    // Order used by queryEntries(): newest first (default) or oldest first.
    void setSortNewestFirst(bool newestFirst) { m_sortNewestFirst = newestFirst; }
    bool sortNewestFirst() const { return m_sortNewestFirst; }

    // Update the content of an entry
    bool updateEntryContent(const QString &id, const QString &newMarkdown);

    // Update the tags of an entry
    bool updateEntryTags(const QString &id, const QStringList &tags);
    bool addTagsToEntries(const QStringList &ids, const QStringList &tags);
    bool removeTagsFromEntries(const QStringList &ids, const QStringList &tags);

    // Get all unique tags across all entries
    QStringList getAllTags();

    // Get a single entry by ID (helper for App.cpp)
    TranslationEntry getEntryById(const QString &id);

    // Metadata-only lookup (no image decoding) for batch features such as the
    // Zotero export. Returns false when the entry does not exist.
    bool getEntryMetadata(const QString &id, TranslationEntry *out);

    // Import legacy history.json into SQLite storage.
    // If jsonPath is empty, defaults to "<storage>/history.json".
    bool importLegacyJson(const QString &jsonPath = QString(), int *importedCount = nullptr, QString *errorMessage = nullptr);
    bool exportHistoryJson(const QString &jsonPath, int *exportedCount = nullptr, QString *errorMessage = nullptr);

signals:
    void entryMarkdownChanged(const QString &id, const QString &newMarkdown);
    void historyFileChanged();

private slots:
    void onFileChanged(const QString &path);

private:
    QString m_basePath;
    QFileSystemWatcher *m_watcher;
    QString m_watchedDbPath;
    QString m_dbConnectionName;
    QSqlDatabase m_db;
    QMap<QString, QString> m_markdownCache;
    QMap<QString, TranslationEntry> m_entryCache;
    QStringList m_allTagsCache;
    bool m_tagsCacheDirty = true;
    bool m_ignoreNextChange = false;
    bool m_sortNewestFirst = true;

    QString getImagesPath() const;
    QString getJsonPath() const;
    QString getDbPath() const;
    void ensureDirectories() const;
    bool ensureDatabaseReady();
    void pruneMissingImageEntries();
    void closeDatabase();
    bool maybeAutoImportLegacyJson();
    bool importLegacyJsonInternal(const QString &jsonPath, int *importedCount, QString *errorMessage);
};

#endif // HISTORYMANAGER_H
