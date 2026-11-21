#include "SummaryWindow.h"

#include "EmbedWebView.h"
#include "HistoryManager.h"
#include "ThemeUtils.h"
#include "TranslationManager.h"

#include <QClipboard>
#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{
QString overflowHandleToolTipText()
{
    if (TranslationManager::instance().getLanguage() == QStringLiteral("zh"))
        return QStringLiteral("\u6a2a\u5411\u62d6\u52a8\u8c03\u6574\u7a97\u53e3\u5bbd\u5ea6\uff0c\u53cc\u51fb\u81ea\u52a8\u9002\u914d");

    return QStringLiteral("Drag horizontally to resize, double-click to auto-fit");
}
} // namespace

void SummaryWindow::refreshToolbarOverflowHint()
{
    if (!m_filterToolbar || !m_toolbarOverflowButton)
        return;

    if (!isVisible() || !m_filterToolbar->isVisible() ||
        m_filterToolbar->width() <= 0 || m_filterToolbar->height() <= 0)
    {
        m_toolbarOverflowButton->setProperty("overflowHintActive", false);
        m_toolbarOverflowButton->style()->unpolish(m_toolbarOverflowButton);
        m_toolbarOverflowButton->style()->polish(m_toolbarOverflowButton);
        m_toolbarOverflowButton->update();
        return;
    }

    bool foundInternalExtension = false;
    const QList<QToolButton *> buttons = m_filterToolbar->findChildren<QToolButton *>();
    for (QToolButton *candidate : buttons)
    {
        if (!candidate)
            continue;

        const QString className = QString::fromLatin1(candidate->metaObject()->className());
        if (className.contains(QStringLiteral("QToolBarExtension")))
        {
            foundInternalExtension = true;
            if (candidate != m_internalToolbarExtensionButton)
            {
                if (m_internalToolbarExtensionButton)
                    m_internalToolbarExtensionButton->removeEventFilter(this);
                m_internalToolbarExtensionButton = candidate;
                m_internalToolbarExtensionButton->installEventFilter(this);
                m_internalToolbarExtensionButton->setFocusPolicy(Qt::NoFocus);
                m_internalToolbarExtensionButton->setEnabled(false);
                m_internalToolbarExtensionButton->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                m_internalToolbarExtensionButton->setStyleSheet(QStringLiteral(
                    "QToolButton { min-width: 0px; max-width: 0px; width: 0px; "
                    "padding: 0px; margin: 0px; border: none; }"));
            }
            candidate->setMinimumWidth(0);
            candidate->setMaximumWidth(0);
            candidate->resize(0, candidate->height());
            candidate->hide();
            break;
        }
    }

    if (!foundInternalExtension)
    {
        if (m_internalToolbarExtensionButton)
            m_internalToolbarExtensionButton->removeEventFilter(this);
        m_internalToolbarExtensionButton = nullptr;
    }

    bool overflowActive = false;
    if (m_filterToolbar->layout())
    {
        overflowActive = m_filterToolbar->layout()->minimumSize().width() > m_filterToolbar->width();
    }

    // Only the filters still live inside the toolbar; the action buttons and the pager
    // are separate bars, so they cannot overflow it.
    const QList<QWidget *> groups = {m_filtersGroup};
    for (QWidget *group : groups)
    {
        if (!group)
            continue;

        if (!group->isVisible())
        {
            overflowActive = true;
            break;
        }

        const QRect groupRect = group->geometry();
        if (groupRect.isEmpty() ||
            groupRect.left() < 0 ||
            groupRect.top() < 0 ||
            groupRect.right() > m_filterToolbar->rect().right() ||
            groupRect.bottom() > m_filterToolbar->rect().bottom())
        {
            overflowActive = true;
            break;
        }
    }

    if (m_internalToolbarExtensionButton)
        m_internalToolbarExtensionButton->hide();

    m_toolbarOverflowButton->setProperty("overflowHintActive", overflowActive);
    m_toolbarOverflowButton->style()->unpolish(m_toolbarOverflowButton);
    m_toolbarOverflowButton->style()->polish(m_toolbarOverflowButton);
    m_toolbarOverflowButton->update();
}

void SummaryWindow::setHistoryManager(HistoryManager *historyManager)
{
    m_historyManager = historyManager;
    loadAvailableTags();
}

void SummaryWindow::loadAvailableTags()
{
    if (!m_historyManager)
        return;
    // Tag list is fetched on-demand for the dialog; keep selected button text up-to-date.
    if (m_tagFilterBtn)
    {
        if (m_selectedTags.isEmpty())
            m_tagFilterBtn->setText(TranslationManager::instance().tr("filter_all_tags"));
        else
        {
            QString text = m_selectedTags.join(", ");
            if (text.size() > 28)
                text = text.left(28) + "...";
            m_tagFilterBtn->setText(text);
        }
    }
}

void SummaryWindow::applyFilters()
{
    m_currentPage = 1;
    refreshHtml(false);
}

QList<TranslationEntry> SummaryWindow::getFilteredEntries() const
{
    QList<TranslationEntry> filtered;

    QDate fromDate = m_fromDateEdit ? m_fromDateEdit->date() : QDate();
    QDate toDate = m_toDateEdit ? m_toDateEdit->date() : QDate();
    const QStringList selectedTags = m_selectedTags;
    const QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();

    for (const TranslationEntry &entry : m_entries)
    {
        QDate entryDate = entry.timestamp.date();
        if (m_fromDateEdit && entryDate < fromDate)
            continue;
        if (m_toDateEdit && entryDate > toDate)
            continue;

        if (!selectedTags.isEmpty())
        {
            bool anyMatch = false;
            for (const QString &t : selectedTags)
            {
                if (entry.tags.contains(t))
                {
                    anyMatch = true;
                    break;
                }
            }
            if (!anyMatch)
                continue;
        }

        // Search by translation content
        if (!searchText.isEmpty())
        {
            const QString content = entry.translatedMarkdown.toLower();
            if (!content.contains(searchText))
                continue;
        }

        filtered.append(entry);
    }

    // Keep the fallback list (used for export/copy and when no HistoryManager is
    // attached) in the same order the archive shows. It used to hard-code newest
    // first, so exporting after choosing "oldest first" produced a different order.
    std::sort(filtered.begin(), filtered.end(), [this](const TranslationEntry &a, const TranslationEntry &b)
              {
                  return m_sortNewestFirst ? a.timestamp > b.timestamp : a.timestamp < b.timestamp;
              });

    return filtered;
}

// Toggle Selection Mode Logic
void SummaryWindow::toggleSelectionMode()
{
    m_selectionMode = m_selectionModeBtn->isChecked();
    if (m_selectionModeBtn)
    {
        const QString key = m_selectionMode ? "btn_cancel_selection_mode" : "btn_selection_mode";
        m_selectionModeBtn->setText(TranslationManager::instance().tr(key));
    }
    if (m_batchDeleteBtn)
        m_batchDeleteBtn->setVisible(m_selectionMode);
    if (m_batchAddTagBtn)
        m_batchAddTagBtn->setVisible(m_selectionMode);
    if (m_batchRemoveTagBtn)
        m_batchRemoveTagBtn->setVisible(m_selectionMode);
    if (m_batchZoteroBtn)
        m_batchZoteroBtn->setVisible(m_selectionMode);
    if (m_selectAllBtn)
        m_selectAllBtn->setVisible(m_selectionMode);

    // Batch-only menu entries are ENABLED rather than shown/hidden, so the action row
    // keeps a constant width no matter the window size or the selection state.
    const QList<QAction *> batchActions = {m_batchSelectAllAction, m_batchAddTagAction,
                                           m_batchRemoveTagAction, m_batchDeleteAction,
                                           m_batchZoteroAction};
    for (QAction *action : batchActions)
    {
        if (action)
            action->setEnabled(m_selectionMode);
    }

    // QToolBar may overflow widgets when width is tight; force a relayout when toggling.
    if (m_actionsGroup)
        m_actionsGroup->adjustSize();
    if (m_filterToolbar)
    {
        m_filterToolbar->updateGeometry();
        if (m_filterToolbar->layout())
            m_filterToolbar->layout()->invalidate();
    }
    refreshToolbarOverflowHint();
    QTimer::singleShot(0, this, [this]()
                       { refreshToolbarOverflowHint(); });

    // Reset Select All state when toggling mode
    m_allSelected = false;
    if (m_selectAllBtn)
    {
        m_selectAllBtn->setText(TranslationManager::instance().tr("btn_select_all"));
    }
    if (m_batchSelectAllAction)
        m_batchSelectAllAction->setText(TranslationManager::instance().tr("btn_select_all"));

    // Notify JS to show/hide checkboxes
    m_webView->eval(QString("if(window.toggleSelectionMode) window.toggleSelectionMode(%1);").arg(m_selectionMode ? "true" : "false").toStdString());
}

void SummaryWindow::onBatchSelectAll()
{
    m_allSelected = !m_allSelected;

    // Update button text
    if (m_selectAllBtn)
    {
        QString key = m_allSelected ? "btn_deselect_all" : "btn_select_all";
        m_selectAllBtn->setText(TranslationManager::instance().tr(key));
    }

    // execute JS
    QString js = QString("if(window.selectAllEntries) window.selectAllEntries(%1);").arg(m_allSelected ? "true" : "false");
    m_webView->eval(js.toStdString());
}

void SummaryWindow::onBatchDelete()
{
    m_webView->eval("if(window.getSelectedIds) { let ids = window.getSelectedIds(); if(ids.length > 0) window.cmd_batchDelete(...ids); }");
    if (m_selectionModeBtn)
        m_selectionModeBtn->setChecked(false);
}

void SummaryWindow::onBatchAddTags()
{
    m_webView->eval("if(window.getSelectedIds) { let ids = window.getSelectedIds(); if(ids.length > 0) window.cmd_batchAddTags(...ids); }");
    if (m_selectionModeBtn)
        m_selectionModeBtn->setChecked(false);
}

void SummaryWindow::onBatchRemoveTags()
{
    m_webView->eval("if(window.getSelectedIds) { let ids = window.getSelectedIds(); if(ids.length > 0) window.cmd_batchRemoveTags(...ids); }");
    if (m_selectionModeBtn)
        m_selectionModeBtn->setChecked(false);
}

void SummaryWindow::onExport()
{
    if (!m_webView)
    {
        exportEntries(QStringList());
        return;
    }

    // The page owns the checkbox state, so ask it for the checked ids and let it call
    // back into cmd_exportSelection. With nothing checked this exports the filter result.
    m_webView->eval("if (window.cmd_exportSelection) {"
                    "  var ids = (window.getSelectedIds ? window.getSelectedIds() : null) || [];"
                    "  window.cmd_exportSelection.apply(null, ids);"
                    "}");
}

void SummaryWindow::exportEntries(const QStringList &ids)
{
    TranslationManager &tm = TranslationManager::instance();

    QList<TranslationEntry> rows;
    if (!ids.isEmpty())
    {
        for (const QString &id : ids)
        {
            if (!m_historyManager)
                continue;
            const TranslationEntry entry = m_historyManager->getEntryById(id);
            if (!entry.id.isEmpty())
                rows.append(entry);
        }
    }
    else
    {
        // No explicit selection: export everything the current filter matches, not
        // just the entries of the page currently on screen.
        rows = getFilteredEntries();
    }

    if (rows.isEmpty())
    {
        QMessageBox::information(this, tm.tr("export_title"), tm.tr("export_no_entries"));
        return;
    }

    const QString jsonFilter = tm.tr("export_filter_json");
    const QString mdFilter = tm.tr("export_filter_md");
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this,
        tm.tr("export_title"),
        QString("translations_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        QString("%1;;%2").arg(jsonFilter, mdFilter),
        &selectedFilter);

    if (path.isEmpty())
        return;

    const bool wantMarkdown =
        path.endsWith(".md", Qt::CaseInsensitive) ||
        path.endsWith(".markdown", Qt::CaseInsensitive) ||
        (!path.endsWith(".json", Qt::CaseInsensitive) && selectedFilter == mdFilter);
    if (wantMarkdown && !path.endsWith(".md", Qt::CaseInsensitive))
        path += ".md";

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tm.tr("export_title"),
                             tm.tr("export_failed").arg(out.errorString()));
        return;
    }

    if (wantMarkdown)
    {
        QByteArray buffer;
        for (const TranslationEntry &entry : rows)
        {
            buffer += QString("## %1\n\n").arg(entry.timestamp.toString("yyyy-MM-dd HH:mm:ss")).toUtf8();
            if (entry.contextTurns > 0)
                buffer += QString("> \xF0\x9F\x94\x97 %1 (%2)\n\n")
                              .arg(QStringLiteral("continued from context"))
                              .arg(entry.contextTurns)
                              .toUtf8();
            if (!entry.tags.isEmpty())
                buffer += QString("> Tags: %1\n\n").arg(entry.tags.join(", ")).toUtf8();
            buffer += entry.translatedMarkdown.toUtf8();
            buffer += "\n\n---\n\n";
        }
        out.write(buffer);
    }
    else
    {
        QJsonArray arr;
        for (const TranslationEntry &entry : rows)
        {
            QJsonObject obj;
            obj["id"] = entry.id;
            obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
            obj["tags"] = QJsonArray::fromStringList(entry.tags);
            // Persisted outside the app too: tells whether this entry inherited context.
            obj["context_turns"] = entry.contextTurns;
            obj["prompt"] = entry.prompt;
            obj["markdown"] = entry.translatedMarkdown;
            arr.append(obj);
        }

        QJsonObject root;
        root["app"] = "AI Screenshot Translator";
        root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["count"] = arr.size();
        root["entries"] = arr;
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    out.close();

    QMessageBox::information(this, tm.tr("export_title"),
                             tm.tr("export_done").arg(rows.size()).arg(QDir::toNativeSeparators(path)));
}

void SummaryWindow::onCopyContext()
{
    if (!m_webView)
    {
        copyEntriesAsContext(QStringList());
        return;
    }

    // The page owns the checkbox state; ask it for the checked ids (empty = filter result).
    m_webView->eval("if (window.cmd_copyContextSelection) {"
                    "  var ids = (window.getSelectedIds ? window.getSelectedIds() : null) || [];"
                    "  window.cmd_copyContextSelection.apply(null, ids);"
                    "}");
}

void SummaryWindow::copyEntriesAsContext(const QStringList &ids)
{
    TranslationManager &tm = TranslationManager::instance();

    QList<TranslationEntry> rows;
    if (!ids.isEmpty())
    {
        for (const QString &id : ids)
        {
            if (!m_historyManager)
                continue;
            const TranslationEntry entry = m_historyManager->getEntryById(id);
            if (!entry.id.isEmpty())
                rows.append(entry);
        }
    }
    else
    {
        rows = getFilteredEntries();
    }

    QStringList parts;
    parts.reserve(rows.size());
    for (const TranslationEntry &entry : rows)
    {
        const QString text = entry.translatedMarkdown.trimmed();
        if (!text.isEmpty())
            parts << text;
    }

    if (parts.isEmpty())
        return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    clipboard->setText(parts.join("\n\n---\n\n"));
    QToolTip::showText(QCursor::pos(), tm.tr("toast_context_copied").arg(parts.size()), this);
}

void SummaryWindow::onCopyImages()
{
    if (!m_webView)
    {
        copyImagesAsFiles(QStringList());
        return;
    }

    m_webView->eval("if (window.cmd_copyImagesSelection) {"
                    "  var ids = (window.getSelectedIds ? window.getSelectedIds() : null) || [];"
                    "  window.cmd_copyImagesSelection.apply(null, ids);"
                    "}");
}

void SummaryWindow::copyImagesAsFiles(const QStringList &ids)
{
    TranslationManager &tm = TranslationManager::instance();

    QList<TranslationEntry> rows;
    if (!ids.isEmpty())
    {
        for (const QString &id : ids)
        {
            if (!m_historyManager)
                continue;
            const TranslationEntry entry = m_historyManager->getEntryById(id);
            if (!entry.id.isEmpty())
                rows.append(entry);
        }
    }
    else
    {
        rows = getFilteredEntries();
    }

    QDir dir(QDir(QDir::tempPath()).filePath("ai-screenshot-translator/clipboard"));
    if (!dir.mkpath("."))
        return;

    QStringList files;
    QImage first;
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();

    for (int r = 0; r < rows.size(); ++r)
    {
        const TranslationEntry &entry = rows.at(r);
        const QStringList images = entry.originalBase64List.isEmpty()
                                       ? QStringList{entry.originalBase64}
                                       : entry.originalBase64List;
        for (int i = 0; i < images.size(); ++i)
        {
            if (images.at(i).isEmpty())
                continue;
            QImage img;
            img.loadFromData(QByteArray::fromBase64(images.at(i).toLatin1()));
            if (img.isNull())
                continue;

            const QString path =
                dir.filePath(QString("entry_%1_%2_%3.png").arg(stamp).arg(r + 1).arg(i + 1));
            if (img.save(path, "PNG"))
                files << path;
            if (first.isNull())
                first = img;
        }
    }

    if (files.isEmpty())
        return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    // Same trick as the result window: one bitmap (for image targets) plus the full file
    // list (for attachment targets), because the clipboard carries only one bitmap.
    auto *mime = new QMimeData();
    if (!first.isNull())
        mime->setImageData(first);
    QList<QUrl> urls;
    urls.reserve(files.size());
    for (const QString &file : files)
        urls << QUrl::fromLocalFile(file);
    mime->setUrls(urls);
    clipboard->setMimeData(mime);

    QToolTip::showText(QCursor::pos(), tm.tr("toast_images_copied").arg(files.size()), this);
}

void SummaryWindow::updateTheme(bool isDark)
{
    ThemeUtils::applyThemeToWindow(this, isDark);

    if (m_webView)
    {
        QColor bg = isDark ? QColor(30, 30, 30) : QColor(255, 255, 255);
        m_webView->setBackgroundColor(bg.red(), bg.green(), bg.blue(), 255);
        QString js = QString("if (typeof applyDarkMode === 'function') { applyDarkMode(%1); } else { document.documentElement.classList.toggle('dark-mode', %1); document.body.classList.toggle('dark-mode', %1); }").arg(isDark ? "true" : "false");
        m_webView->eval(js.toStdString());
    }

    if (m_filterToolbar)
    {
        const QString qssPath = isDark ? ":/assets/qss/archive_toolbar_dark.qss" : ":/assets/qss/archive_toolbar_light.qss";
        QFile f(qssPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString qss = QString::fromUtf8(f.readAll());
            if (m_filterToolbarShell)
                m_filterToolbarShell->setStyleSheet(qss);
            else
                m_filterToolbar->setStyleSheet(qss);
        }
        else
        {
            // Fallback: keep it readable even if resource is missing.
            const QString bg = isDark ? "#1f1f1f" : "#f6f7f9";
            const QString border = isDark ? "#343434" : "#d8dde6";
            const QString fallbackQss = QString(
                                            "QToolBar#archiveFilterToolbar{background:%1;border:none;border-bottom:1px solid %2;}"
                                            "QToolButton#archiveOverflowHandle{background:#2563eb;color:#eff6ff;border:1px solid #1d4ed8;}")
                                            .arg(bg, border);
            if (m_filterToolbarShell)
                m_filterToolbarShell->setStyleSheet(fallbackQss);
            else
                m_filterToolbar->setStyleSheet(fallbackQss);
        }
        refreshToolbarOverflowHint();
    }
}

void SummaryWindow::updateLanguage()
{
    // Update filter labels
    if (m_fromLabel)
        m_fromLabel->setText(TranslationManager::instance().tr("filter_from_date") + ":");
    if (m_toLabel)
        m_toLabel->setText(TranslationManager::instance().tr("filter_to_date") + ":");
    if (m_searchLabel)
        m_searchLabel->setText(TranslationManager::instance().tr("filter_search") + ":");
    
    // Update buttons
    if (m_tagFilterBtn)
    {
        if (m_selectedTags.isEmpty())
            m_tagFilterBtn->setText(TranslationManager::instance().tr("filter_all_tags"));
        else
        {
            QString text = m_selectedTags.join(", ");
            if (text.size() > 28) text = text.left(28) + "...";
            m_tagFilterBtn->setText(text);
        }
    }
    if (m_clearFilterBtn)
    {
        m_clearFilterBtn->setText(QString::fromUtf8("\u2715"));
        m_clearFilterBtn->setToolTip(TranslationManager::instance().tr("filter_clear"));
    }
    if (m_dateRangeBtn)
        m_dateRangeBtn->setToolTip(TranslationManager::instance().tr("filter_date_range"));
    updateDateRangeLabel();
    
    // Update batch buttons
    if (m_selectionModeBtn)
    {
        const QString key = m_selectionMode ? "btn_cancel_selection_mode" : "btn_selection_mode";
        m_selectionModeBtn->setText(TranslationManager::instance().tr(key));
    }
    if (m_selectAllBtn)
    {
        QString key = m_allSelected ? "btn_deselect_all" : "btn_select_all";
        m_selectAllBtn->setText(TranslationManager::instance().tr(key));
    }
    if (m_batchDeleteBtn)
        m_batchDeleteBtn->setText(TranslationManager::instance().tr("btn_batch_delete"));
    if (m_batchAddTagBtn)
        m_batchAddTagBtn->setText(TranslationManager::instance().tr("btn_batch_add_tag"));
    if (m_batchRemoveTagBtn)
        m_batchRemoveTagBtn->setText(TranslationManager::instance().tr("btn_batch_remove_tag"));
    if (m_exportBtn)
    {
        m_exportBtn->setText(TranslationManager::instance().tr("btn_export_selected"));
        m_exportBtn->setToolTip(TranslationManager::instance().tr("btn_export_tip"));
    }
    if (m_copyContextBtn)
    {
        m_copyContextBtn->setText(TranslationManager::instance().tr("btn_copy_context"));
        m_copyContextBtn->setToolTip(TranslationManager::instance().tr("btn_copy_context_tip"));
    }
    if (m_copyImagesBtn)
    {
        m_copyImagesBtn->setText(TranslationManager::instance().tr("btn_copy_images"));
        m_copyImagesBtn->setToolTip(TranslationManager::instance().tr("btn_copy_images_tip"));
    }
    if (m_actionsMenuBtn)
        m_actionsMenuBtn->setText(TranslationManager::instance().tr("btn_actions_menu") + QString::fromUtf8(" \u25BE"));
    if (m_batchSelectAllAction)
        m_batchSelectAllAction->setText(TranslationManager::instance().tr(m_allSelected ? "btn_deselect_all" : "btn_select_all"));
    if (m_batchAddTagAction)
        m_batchAddTagAction->setText(TranslationManager::instance().tr("btn_batch_add_tag"));
    if (m_batchRemoveTagAction)
        m_batchRemoveTagAction->setText(TranslationManager::instance().tr("btn_batch_remove_tag"));
    if (m_batchDeleteAction)
        m_batchDeleteAction->setText(TranslationManager::instance().tr("btn_batch_delete"));
    if (m_batchZoteroAction)
        m_batchZoteroAction->setText(TranslationManager::instance().tr("btn_batch_zotero"));
    if (m_exportAction)
        m_exportAction->setText(TranslationManager::instance().tr("btn_export_selected"));
    if (m_copyContextAction)
        m_copyContextAction->setText(TranslationManager::instance().tr("btn_copy_context"));
    if (m_copyImagesAction)
        m_copyImagesAction->setText(TranslationManager::instance().tr("btn_copy_images"));
    if (m_sortCombo)
    {
        m_sortCombo->setItemText(0, TranslationManager::instance().tr("sort_newest_first"));
        m_sortCombo->setItemText(1, TranslationManager::instance().tr("sort_oldest_first"));
        m_sortCombo->setToolTip(TranslationManager::instance().tr("tip_sort_order"));
    }
    if (m_selectionModeBtn)
    {
        m_selectionModeBtn->setToolTip(QString("%1 (%2)")
                                           .arg(TranslationManager::instance().tr("tip_selection_mode"))
                                           .arg(m_selectionToggleKey.toUpper()));
    }

    // The per-entry action labels live in the page; refresh them in place.
    if (m_webView)
    {
        QJsonObject labels;
        labels["ab_entry_edit"] = TranslationManager::instance().tr("ab_entry_edit");
        labels["ab_entry_source"] = TranslationManager::instance().tr("ab_entry_source");
        labels["ab_entry_shot"] = TranslationManager::instance().tr("ab_entry_shot");
        labels["ab_entry_delete"] = TranslationManager::instance().tr("ab_entry_delete");
        labels["ab_entry_delete_confirm"] = TranslationManager::instance().tr("ab_entry_delete_confirm");
        labels["ab_entry_bold"] = TranslationManager::instance().tr("ab_entry_bold");
        labels["ab_entry_underline"] = TranslationManager::instance().tr("ab_entry_underline");
        labels["ab_entry_highlight"] = TranslationManager::instance().tr("ab_entry_highlight");
        QString safe = QString::fromUtf8(QJsonDocument(labels).toJson(QJsonDocument::Compact));
        safe.replace("<", "\\u003C");
        safe.replace(">", "\\u003E");
        safe.replace("&", "\\u0026");
        m_webView->eval(QString("if(window.__setEntryActionLabels) window.__setEntryActionLabels(%1);")
                            .arg(safe)
                            .toStdString());
    }
    // Arrow buttons carry no label, so the translation goes into the tooltip.
    if (m_prevPageBtn)
        m_prevPageBtn->setToolTip(TranslationManager::instance().tr("btn_page_prev"));
    if (m_nextPageBtn)
        m_nextPageBtn->setToolTip(TranslationManager::instance().tr("btn_page_next"));
    updatePaginationUi();

    // Update search placeholder
    if (m_searchEdit)
        m_searchEdit->setPlaceholderText(TranslationManager::instance().tr("filter_search_placeholder"));

    if (m_toolbarOverflowButton)
        m_toolbarOverflowButton->setToolTip(overflowHandleToolTipText());
    
    // Refresh window title
    setWindowTitle(TranslationManager::instance().tr("summary_title"));
}

