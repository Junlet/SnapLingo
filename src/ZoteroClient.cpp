#include "ZoteroClient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>

namespace
{
constexpr int kRequestTimeoutMs = 15000;
constexpr int kMaxBatchSize = 50;
constexpr int kMaxRetryDepth = 3;

// 32 character hex identifier used to make a write request idempotent.
QString randomWriteToken()
{
    QString token;
    token.reserve(32);
    for (int i = 0; i < 4; ++i)
        token += QStringLiteral("%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    return token;
}

QString composeNoteHtml(const ZoteroNote &note)
{
    QString html;
    const QString heading = note.heading.trimmed();
    if (!heading.isEmpty())
        html += QStringLiteral("<h1>%1</h1>").arg(heading.toHtmlEscaped());
    html += note.html;
    return html;
}

QJsonObject buildNoteObject(const ZoteroNote &note)
{
    QJsonObject item;
    item.insert(QStringLiteral("itemType"), QStringLiteral("note"));
    item.insert(QStringLiteral("note"), composeNoteHtml(note));

    QJsonArray tags;
    for (const QString &tag : note.tags)
    {
        const QString trimmed = tag.trimmed();
        if (trimmed.isEmpty())
            continue;
        QJsonObject tagObject;
        tagObject.insert(QStringLiteral("tag"), trimmed);
        tags.append(tagObject);
    }
    if (!tags.isEmpty())
        item.insert(QStringLiteral("tags"), tags);

    if (!note.collectionKeys.isEmpty())
        item.insert(QStringLiteral("collections"), QJsonArray::fromStringList(note.collectionKeys));

    return item;
}

QString trimmedReplyBody(const QByteArray &body, int maxLength = 400)
{
    QString text = QString::fromUtf8(body).trimmed();
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (text.size() > maxLength)
        text = text.left(maxLength) + QStringLiteral("...");
    return text;
}
} // namespace

ZoteroClient::ZoteroClient(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

QString ZoteroClient::apiBaseUrl()
{
    return QStringLiteral("http://127.0.0.1:23119/api");
}

QString ZoteroClient::applicationName()
{
    return QStringLiteral("AI Screenshot Translator");
}

QNetworkRequest ZoteroClient::makeRequest(const QString &path) const
{
    QNetworkRequest request(QUrl(apiBaseUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Zotero-API-Version", "3");
    if (!m_serverId.isEmpty())
        request.setRawHeader("Zotero-Server-ID", m_serverId.toUtf8());
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}

void ZoteroClient::probeServer()
{
    QNetworkRequest request(QUrl(apiBaseUrl() + QStringLiteral("/")));
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]()
            {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netErrorText = reply->errorString();
        const QString serverId = QString::fromUtf8(reply->rawHeader("Zotero-Server-ID"));
        const QString version = QString::fromUtf8(reply->rawHeader("X-Zotero-Version"));
        reply->deleteLater();

        if (status == 403) {
            emit serverProbed(false, version, QStringLiteral("disabled"));
            return;
        }
        if (status != 200 || netError != QNetworkReply::NoError) {
            if (status == 0 || netError == QNetworkReply::ConnectionRefusedError ||
                netError == QNetworkReply::HostNotFoundError || netError == QNetworkReply::TimeoutError) {
                emit serverProbed(false, version, QStringLiteral("unreachable"));
                return;
            }
            emit serverProbed(false, version, netErrorText.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : netErrorText);
            return;
        }
        if (serverId.isEmpty()) {
            // Zotero answered but does not expose the write capable local API (Zotero < 10).
            emit serverProbed(false, version, QStringLiteral("unsupported"));
            return;
        }

        m_serverId = serverId;
        m_zoteroVersion = version;
        emit serverProbed(true, version, QString()); });
}

void ZoteroClient::fetchCollections()
{
    QNetworkRequest request = makeRequest(QStringLiteral("/users/0/collections"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]()
            {
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();

        if (status != 200 || netError != QNetworkReply::NoError) {
            emit collectionsFetched(QList<ZoteroCollectionInfo>(), QStringLiteral("HTTP %1").arg(status));
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            emit collectionsFetched(QList<ZoteroCollectionInfo>(), QStringLiteral("invalid JSON"));
            return;
        }

        QList<ZoteroCollectionInfo> collections;
        for (const QJsonValue &value : doc.array()) {
            const QJsonObject data = value.toObject().value(QStringLiteral("data")).toObject();
            ZoteroCollectionInfo info;
            info.key = data.value(QStringLiteral("key")).toString();
            info.name = data.value(QStringLiteral("name")).toString();
            const QJsonValue parent = data.value(QStringLiteral("parentCollection"));
            if (parent.isString())
                info.parentKey = parent.toString();
            if (!info.key.isEmpty())
                collections.append(info);
        }
        emit collectionsFetched(collections, QString()); });
}

void ZoteroClient::importNotes(const QList<ZoteroNote> &notes)
{
    if (m_importing || notes.isEmpty())
        return;

    m_notes = notes;
    m_summary = ZoteroImportSummary();
    m_summary.total = notes.size();
    m_nextIndex = 0;
    m_batchSize = kMaxBatchSize;
    m_retryDepth = 0;
    m_importing = true;

    requestServerId([this](bool ok)
                    {
        if (!ok) {
            m_summary.unreachable = true;
            finishImport();
            return;
        }
        authorize([this](bool authorized, const QString &message) {
            if (!authorized) {
                if (!message.isEmpty())
                    m_summary.message = message;
                finishImport();
                return;
            }
            startBatch();
        }); });
}

void ZoteroClient::requestServerId(std::function<void(bool)> done)
{
    if (!m_serverId.isEmpty())
    {
        done(true);
        return;
    }

    QNetworkRequest request(QUrl(apiBaseUrl() + QStringLiteral("/")));
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]()
            {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString serverId = QString::fromUtf8(reply->rawHeader("Zotero-Server-ID"));
        const QString version = QString::fromUtf8(reply->rawHeader("X-Zotero-Version"));
        reply->deleteLater();

        if (status == 200 && !serverId.isEmpty()) {
            m_serverId = serverId;
            m_zoteroVersion = version;
            done(true);
            return;
        }
        done(false); });
}

void ZoteroClient::authorize(std::function<void(bool, const QString &)> done)
{
    if (!m_localKey.isEmpty())
    {
        done(true, QString());
        return;
    }

    QNetworkRequest request = makeRequest(QStringLiteral("/local/authorize"));
    QJsonObject payload;
    payload.insert(QStringLiteral("appName"), applicationName());
    QNetworkReply *reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]()
            {
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netErrorText = reply->errorString();
        reply->deleteLater();

        if (status == 403) {
            m_summary.denied = true;
            done(false, QStringLiteral("denied"));
            return;
        }
        if (status == 429) {
            done(false, QStringLiteral("rate-limited"));
            return;
        }
        if (status != 200 || netError != QNetworkReply::NoError) {
            if (status == 0)
                m_summary.unreachable = true;
            done(false, netErrorText.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : netErrorText);
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        const QJsonObject object = doc.object();
        const QString key = object.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            done(false, trimmedReplyBody(body));
            return;
        }

        m_localKey = key;
        m_keyRemembered = object.value(QStringLiteral("remember")).toBool();
        qDebug() << "[Zotero] local API key granted, reusable:" << m_keyRemembered;
        done(true, QString()); });
}

void ZoteroClient::startBatch()
{
    if (m_nextIndex >= m_notes.size())
    {
        finishImport();
        return;
    }

    const int count = qMin(m_batchSize, m_notes.size() - m_nextIndex);
    QJsonArray items;
    for (int i = m_nextIndex; i < m_nextIndex + count; ++i)
        items.append(buildNoteObject(m_notes.at(i)));

    QNetworkRequest request = makeRequest(QStringLiteral("/users/0/items"));
    request.setRawHeader("Zotero-API-Key", m_localKey.toUtf8());
    request.setRawHeader("Zotero-Write-Token", randomWriteToken().toUtf8());

    QNetworkReply *reply = m_network->post(request, QJsonDocument(items).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]()
            { handleBatchReply(reply); });
}

void ZoteroClient::handleBatchReply(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    reply->deleteLater();

    const int count = qMin(m_batchSize, m_notes.size() - m_nextIndex);
    const int firstIndex = m_nextIndex;

    // A single use key is consumed by the first successful write request.
    if (status == 401 && m_retryDepth < kMaxRetryDepth)
    {
        ++m_retryDepth;
        m_localKey.clear();
        authorize([this](bool ok, const QString &message)
                  {
            if (!ok) {
                if (!message.isEmpty())
                    m_summary.message = message;
                finishImport();
                return;
            }
            startBatch(); });
        return;
    }

    // Zotero-Server-ID missing or stale.
    if (status == 428 && m_retryDepth < kMaxRetryDepth)
    {
        ++m_retryDepth;
        m_serverId.clear();
        requestServerId([this](bool ok)
                        {
            if (!ok) {
                m_summary.unreachable = true;
                finishImport();
                return;
            }
            startBatch(); });
        return;
    }

    // Stale write token / library changed: retry with a fresh request.
    if (status == 412 && m_retryDepth < kMaxRetryDepth)
    {
        ++m_retryDepth;
        startBatch();
        return;
    }

    // Too many items in one request: halve the batch and try again.
    if (status == 413 && m_batchSize > 1)
    {
        m_batchSize = qMax(1, m_batchSize / 2);
        startBatch();
        return;
    }

    if (status == 403)
    {
        m_summary.denied = true;
        finishImport();
        return;
    }

    if (status != 200 || netError != QNetworkReply::NoError)
    {
        if (status == 0 || netError == QNetworkReply::ConnectionRefusedError)
            m_summary.unreachable = true;

        m_summary.failed += count;
        m_summary.errors << QStringLiteral("HTTP %1: %2")
                                .arg(status)
                                .arg(netErrorText.isEmpty() ? trimmedReplyBody(body) : netErrorText);

        m_nextIndex = firstIndex + count;
        m_retryDepth = 0;
        continueAfterBatch();
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        m_summary.failed += count;
        m_summary.errors << QStringLiteral("HTTP 200: %1").arg(trimmedReplyBody(body));
        m_nextIndex = firstIndex + count;
        m_retryDepth = 0;
        continueAfterBatch();
        return;
    }

    const QJsonObject result = doc.object();
    const QJsonObject successful = result.value(QStringLiteral("successful")).toObject();
    const QJsonObject failed = result.value(QStringLiteral("failed")).toObject();

    m_summary.created += successful.size();
    for (auto it = failed.begin(); it != failed.end(); ++it)
    {
        const QJsonObject failure = it.value().toObject();
        const int index = firstIndex + it.key().toInt();
        QString label = QStringLiteral("#%1").arg(index + 1);
        if (index >= 0 && index < m_notes.size() && !m_notes.at(index).heading.isEmpty())
            label += QStringLiteral(" %1").arg(m_notes.at(index).heading);

        m_summary.errors << QStringLiteral("%1: %2 (HTTP %3)")
                                .arg(label,
                                     failure.value(QStringLiteral("message")).toString(),
                                     QString::number(failure.value(QStringLiteral("code")).toInt()));
        ++m_summary.failed;
    }

    m_nextIndex = firstIndex + count;
    m_retryDepth = 0;
    continueAfterBatch();
}

void ZoteroClient::continueAfterBatch()
{
    emit importProgress(m_summary.created + m_summary.failed, m_summary.total);

    if (m_nextIndex >= m_notes.size())
    {
        finishImport();
        return;
    }

    if (m_summary.unreachable || m_summary.denied)
    {
        finishImport();
        return;
    }

    // Single use keys must be re-requested for every further write request.
    if (!m_keyRemembered)
    {
        m_localKey.clear();
        authorize([this](bool ok, const QString &message)
                  {
            if (!ok) {
                if (!message.isEmpty())
                    m_summary.message = message;
                finishImport();
                return;
            }
            startBatch(); });
        return;
    }

    startBatch();
}

void ZoteroClient::finishImport()
{
    if (!m_importing)
        return;

    m_importing = false;
    // A single use key has already been consumed; a reusable one is kept so that
    // a following import does not ask for authorization again.
    if (!m_keyRemembered)
        m_localKey.clear();
    emit importFinished(m_summary);
}
