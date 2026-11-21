#ifndef ZOTEROCLIENT_H
#define ZOTEROCLIENT_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// A collection (folder) inside the local Zotero library.
struct ZoteroCollectionInfo
{
    QString key;
    QString name;
    QString parentKey;
};

// One Zotero note. The app maps one translation entry to exactly one note.
struct ZoteroNote
{
    QString heading; // First line of the note; Zotero derives the item title from it
    QString html;    // Body HTML (already converted from Markdown)
    QStringList tags;
    QStringList collectionKeys;
};

struct ZoteroImportSummary
{
    int total = 0;
    int created = 0;
    int failed = 0;
    QStringList errors;
    QString message; // Extra human readable hint (authorization, connectivity, ...)
    bool denied = false;
    bool unreachable = false;
};

// Talks to the Zotero desktop client through its local HTTP API
// (http://127.0.0.1:23119/api/, documented at
//  https://www.zotero.org/support/dev/web_api/v3/local_api).
//
// Reads need no authentication. Writes require a local API key that the user
// grants through a Zotero dialog: we ask for one on demand via
// POST /api/local/authorize and reuse it for the whole import when the user
// picked "Always Allow".
class ZoteroClient : public QObject
{
    Q_OBJECT
public:
    explicit ZoteroClient(QObject *parent = nullptr);

    static QString apiBaseUrl();
    static QString applicationName();

    // GET / -> validates that Zotero is running with the local API enabled.
    void probeServer();
    // GET /users/0/collections -> available collections of the local library.
    void fetchCollections();
    // Creates one note per ZoteroNote, in batches of up to 50.
    void importNotes(const QList<ZoteroNote> &notes);

signals:
    void serverProbed(bool ok, const QString &zoteroVersion, const QString &errorMessage);
    void collectionsFetched(const QList<ZoteroCollectionInfo> &collections, const QString &errorMessage);
    void importProgress(int processed, int total);
    void importFinished(const ZoteroImportSummary &summary);

private:
    QNetworkRequest makeRequest(const QString &path) const;
    void requestServerId(std::function<void(bool)> done);
    void authorize(std::function<void(bool, const QString &)> done);
    void startBatch();
    void handleBatchReply(QNetworkReply *reply);
    void continueAfterBatch();
    void finishImport();

    QNetworkAccessManager *m_network = nullptr;

    QString m_serverId;
    QString m_zoteroVersion;
    QString m_localKey;
    bool m_keyRemembered = false;

    QList<ZoteroNote> m_notes;
    ZoteroImportSummary m_summary;
    int m_nextIndex = 0;
    int m_batchSize = 50;
    int m_retryDepth = 0;
    bool m_importing = false;
};

#endif // ZOTEROCLIENT_H
