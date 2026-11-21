#include "ZoteroImportDialog.h"

#include "TranslationManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace
{
// Local helper: named "ztr" because "tr" would be shadowed by QObject::tr inside
// the class member functions below.
QString ztr(const QString &key)
{
    return TranslationManager::instance().tr(key);
}

QString statusTextForError(const QString &errorCode)
{
    if (errorCode == QLatin1String("unreachable"))
        return ztr(QStringLiteral("zotero_status_unreachable"));
    if (errorCode == QLatin1String("disabled"))
        return ztr(QStringLiteral("zotero_status_disabled"));
    if (errorCode == QLatin1String("unsupported"))
        return ztr(QStringLiteral("zotero_status_unsupported"));
    if (errorCode.isEmpty())
        return ztr(QStringLiteral("zotero_status_unreachable"));
    return ztr(QStringLiteral("zotero_status_error")).arg(errorCode);
}
} // namespace

ZoteroImportDialog::ZoteroImportDialog(const QList<ZoteroNote> &notes, int skippedCount,
                                       ZoteroClient *client, QWidget *parent)
    : QDialog(parent), m_notes(notes), m_skippedCount(skippedCount), m_client(client)
{
    buildUi();

    if (!m_client)
    {
        setStatus(statusTextForError(QStringLiteral("unreachable")), true);
        return;
    }

    connect(m_client, &ZoteroClient::serverProbed, this, &ZoteroImportDialog::onServerProbed);
    connect(m_client, &ZoteroClient::collectionsFetched, this, &ZoteroImportDialog::onCollectionsFetched);
    connect(m_client, &ZoteroClient::importProgress, this, &ZoteroImportDialog::onProgress);
    connect(m_client, &ZoteroClient::importFinished, this, &ZoteroImportDialog::onFinished);

    m_client->probeServer();
}

void ZoteroImportDialog::buildUi()
{
    setWindowTitle(ztr(QStringLiteral("zotero_dialog_title")));
    setModal(true);
    setMinimumWidth(480);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(ztr(QStringLiteral("zotero_status_checking")), this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    QFormLayout *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_collectionCombo = new QComboBox(this);
    m_collectionCombo->setMinimumWidth(280);
    m_collectionCombo->addItem(ztr(QStringLiteral("zotero_target_none")), QString());
    m_collectionCombo->setEnabled(false);
    formLayout->addRow(ztr(QStringLiteral("zotero_target_label")), m_collectionCombo);
    mainLayout->addLayout(formLayout);

    m_tagsCheck = new QCheckBox(ztr(QStringLiteral("zotero_include_tags")), this);
    m_tagsCheck->setChecked(true);
    mainLayout->addWidget(m_tagsCheck);

    m_countLabel = new QLabel(this);
    QString countText = ztr(QStringLiteral("zotero_count_label")).arg(m_notes.size());
    if (m_skippedCount > 0)
        countText += QStringLiteral("\n") + ztr(QStringLiteral("zotero_skipped_label")).arg(m_skippedCount);
    m_countLabel->setText(countText);
    m_countLabel->setWordWrap(true);
    mainLayout->addWidget(m_countLabel);

    m_hintLabel = new QLabel(ztr(QStringLiteral("zotero_hint_authorize")), this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setProperty("role", "caption");
    mainLayout->addWidget(m_hintLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, qMax(1, m_notes.size()));
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultLabel->setVisible(false);
    mainLayout->addWidget(m_resultLabel);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_importBtn = new QPushButton(ztr(QStringLiteral("zotero_import_btn")), this);
    m_importBtn->setEnabled(false);
    m_closeBtn = new QPushButton(ztr(QStringLiteral("zotero_close")), this);
    buttonLayout->addWidget(m_importBtn);
    buttonLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(buttonLayout);

    connect(m_importBtn, &QPushButton::clicked, this, &ZoteroImportDialog::startImport);
    connect(m_closeBtn, &QPushButton::clicked, this, &ZoteroImportDialog::accept);
}

void ZoteroImportDialog::setStatus(const QString &text, bool isError)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #d9534f;") : QString());
}

QString ZoteroImportDialog::collectionDisplayName(const ZoteroCollectionInfo &info,
                                                 const QHash<QString, ZoteroCollectionInfo> &byKey) const
{
    QStringList parts;
    QString key = info.key;
    QSet<QString> visited;
    while (!key.isEmpty() && byKey.contains(key) && !visited.contains(key))
    {
        visited.insert(key);
        const ZoteroCollectionInfo current = byKey.value(key);
        parts.prepend(current.name);
        key = current.parentKey;
    }
    if (parts.isEmpty())
        return info.name;
    return parts.join(QStringLiteral(" / "));
}

void ZoteroImportDialog::onServerProbed(bool ok, const QString &zoteroVersion, const QString &errorMessage)
{
    if (!ok)
    {
        m_serverReady = false;
        m_importBtn->setEnabled(false);
        setStatus(statusTextForError(errorMessage), true);
        return;
    }

    m_serverReady = true;
    const QString version = zoteroVersion.isEmpty() ? QStringLiteral("?") : zoteroVersion;
    setStatus(ztr(QStringLiteral("zotero_status_ready")).arg(version));
    m_importBtn->setEnabled(!m_notes.isEmpty());
    m_collectionCombo->setEnabled(true);

    if (m_client)
        m_client->fetchCollections();
}

void ZoteroImportDialog::onCollectionsFetched(const QList<ZoteroCollectionInfo> &collections,
                                              const QString &errorMessage)
{
    if (!m_collectionCombo)
        return;

    if (!errorMessage.isEmpty())
    {
        m_collectionCombo->setToolTip(ztr(QStringLiteral("zotero_collections_failed")).arg(errorMessage));
        return;
    }

    QHash<QString, ZoteroCollectionInfo> byKey;
    for (const ZoteroCollectionInfo &info : collections)
        byKey.insert(info.key, info);

    m_collectionCombo->clear();
    m_collectionCombo->addItem(ztr(QStringLiteral("zotero_target_none")), QString());
    for (const ZoteroCollectionInfo &info : collections)
        m_collectionCombo->addItem(collectionDisplayName(info, byKey), info.key);
}

void ZoteroImportDialog::startImport()
{
    if (!m_client || !m_serverReady || m_notes.isEmpty())
        return;

    const QString collectionKey = m_collectionCombo->currentData().toString();
    const bool withTags = m_tagsCheck->isChecked();

    QList<ZoteroNote> notes = m_notes;
    for (ZoteroNote &note : notes)
    {
        note.collectionKeys = collectionKey.isEmpty() ? QStringList() : (QStringList() << collectionKey);
        if (!withTags)
            note.tags.clear();
    }

    m_importBtn->setEnabled(false);
    m_closeBtn->setEnabled(false);
    m_collectionCombo->setEnabled(false);
    m_tagsCheck->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, notes.size());
    m_progressBar->setValue(0);
    m_resultLabel->setVisible(false);
    setStatus(ztr(QStringLiteral("zotero_importing_wait")));

    m_client->importNotes(notes);
}

void ZoteroImportDialog::onProgress(int processed, int total)
{
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(processed);
    setStatus(ztr(QStringLiteral("zotero_importing")).arg(processed).arg(total));
}

void ZoteroImportDialog::onFinished(const ZoteroImportSummary &summary)
{
    m_summary = summary;

    m_closeBtn->setEnabled(true);
    m_progressBar->setRange(0, qMax(1, summary.total));
    m_progressBar->setValue(summary.created + summary.failed);

    QString text;
    if (summary.created > 0 && summary.failed == 0)
        text = ztr(QStringLiteral("zotero_result_ok")).arg(summary.created);
    else if (summary.created > 0)
        text = ztr(QStringLiteral("zotero_result_partial")).arg(summary.created).arg(summary.failed);
    else
        text = ztr(QStringLiteral("zotero_result_none"));

    if (summary.denied)
        text += QStringLiteral("\n") + ztr(QStringLiteral("zotero_msg_denied"));
    else if (summary.unreachable)
        text += QStringLiteral("\n") + ztr(QStringLiteral("zotero_msg_unreachable"));
    else if (summary.message == QLatin1String("rate-limited"))
        text += QStringLiteral("\n") + ztr(QStringLiteral("zotero_msg_rate_limited"));
    else if (!summary.message.isEmpty())
        text += QStringLiteral("\n") + summary.message;

    QStringList errors = summary.errors;
    if (!errors.isEmpty())
    {
        const int shown = qMin(10, errors.size());
        QStringList head = errors.mid(0, shown);
        if (errors.size() > shown)
            head << QStringLiteral("... (%1)").arg(errors.size() - shown);
        text += QStringLiteral("\n") + ztr(QStringLiteral("zotero_result_errors")) + QStringLiteral("\n")
                + head.join(QStringLiteral("\n"));
    }

    m_resultLabel->setText(text);
    m_resultLabel->setVisible(true);
    setStatus(summary.created > 0 ? ztr(QStringLiteral("zotero_result_ok")).arg(summary.created)
                                  : ztr(QStringLiteral("zotero_result_none")),
              summary.created == 0);
}
