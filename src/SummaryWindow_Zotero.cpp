#include "SummaryWindow.h"

#include "EmbedWebView.h"
#include "HistoryManager.h"
#include "TranslationManager.h"
#include "ZoteroClient.h"
#include "ZoteroImportDialog.h"

#include <QDir>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>

namespace
{
// Named "ztr" because an unqualified "tr" would resolve to QObject::tr inside
// the SummaryWindow member functions below.
QString ztr(const QString &key)
{
    return TranslationManager::instance().tr(key);
}

QString escapeHtml(const QString &text)
{
    return text.toHtmlEscaped();
}

// Markdown -> the restricted HTML used by Zotero notes. Qt converts headings,
// lists, tables, code blocks, bold and italic; inline LaTeX is kept as plain
// text because Zotero does not render KaTeX.
QString markdownToNoteBody(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);

    QString html = document.toHtml();
    static const QRegularExpression bodyPattern(QStringLiteral("<body[^>]*>(.*)</body>"),
                                                QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = bodyPattern.match(html);
    if (match.hasMatch())
        html = match.captured(1);

    return html.trimmed();
}

QString entryHeading(const TranslationEntry &entry)
{
    if (!entry.timestamp.isValid())
        return ztr(QStringLiteral("zotero_note_heading_fallback"));
    return ztr(QStringLiteral("zotero_note_heading"))
        .arg(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

QString entryMetadataHtml(const TranslationEntry &entry)
{
    QStringList rows;

    if (entry.timestamp.isValid())
    {
        rows << QStringLiteral("<p><strong>%1：</strong>%2</p>")
                    .arg(escapeHtml(ztr(QStringLiteral("zotero_note_time"))),
                         escapeHtml(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }

    if (!entry.localImagePaths.isEmpty())
    {
        QStringList paths;
        for (const QString &path : entry.localImagePaths)
            paths << QStringLiteral("<code>%1</code>").arg(escapeHtml(QDir::toNativeSeparators(path)));
        rows << QStringLiteral("<p><strong>%1：</strong>%2</p>")
                    .arg(escapeHtml(ztr(QStringLiteral("zotero_note_image"))),
                         paths.join(QStringLiteral("<br>")));
    }

    if (!entry.tags.isEmpty())
    {
        rows << QStringLiteral("<p><strong>%1：</strong>%2</p>")
                    .arg(escapeHtml(ztr(QStringLiteral("zotero_note_tags"))),
                         escapeHtml(entry.tags.join(QStringLiteral(", "))));
    }

    return rows.join(QString());
}

// One translation entry becomes exactly one Zotero note.
ZoteroNote buildZoteroNote(const TranslationEntry &entry)
{
    ZoteroNote note;
    note.heading = entryHeading(entry);

    QString body = markdownToNoteBody(entry.translatedMarkdown);
    const QString metadata = entryMetadataHtml(entry);
    if (!metadata.isEmpty())
        body += QStringLiteral("<hr>") + metadata;

    note.html = body;
    note.tags = entry.tags;
    return note;
}
} // namespace

void SummaryWindow::importEntriesToZotero(const QStringList &ids)
{
    if (ids.isEmpty())
    {
        QMessageBox::information(this,
                                 ztr(QStringLiteral("zotero_dialog_title")),
                                 ztr(QStringLiteral("zotero_nothing_selected")));
        return;
    }

    if (!m_historyManager)
        return;

    QList<ZoteroNote> notes;
    int skipped = 0;
    for (const QString &id : ids)
    {
        TranslationEntry entry;
        if (!m_historyManager->getEntryMetadata(id, &entry) || entry.translatedMarkdown.trimmed().isEmpty())
        {
            ++skipped;
            continue;
        }
        notes << buildZoteroNote(entry);
    }

    if (notes.isEmpty())
    {
        QMessageBox::information(this,
                                 ztr(QStringLiteral("zotero_dialog_title")),
                                 ztr(QStringLiteral("zotero_nothing_selected")));
        return;
    }

    // Kept as a member so that an "Always Allow" authorization stays valid for
    // later imports during the same session.
    if (!m_zoteroClient)
        m_zoteroClient = std::make_unique<ZoteroClient>();

    ZoteroImportDialog dialog(notes, skipped, m_zoteroClient.get(), this);
    dialog.exec();

    if (dialog.importSucceeded() && m_selectionModeBtn && m_selectionModeBtn->isChecked())
        m_selectionModeBtn->setChecked(false);
}

void SummaryWindow::onBatchImportZotero()
{
    if (!m_webView)
        return;

    // The id array is JSON encoded on purpose: unlike the ...ids spread used by
    // the other batch actions this also covers the "nothing selected" case.
    m_webView->eval("if(window.getSelectedIds) { window.cmd_batchImportZotero(JSON.stringify(window.getSelectedIds())); }");
}
