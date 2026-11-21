#include "SummaryWindow.h"

#include "EmbedWebView.h"
#include "HistoryManager.h"
#include "TagDialog.h"
#include "TranslationManager.h"

#include <QComboBox>
#include <QMenu>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QApplication>
#include <QDate>
#include <QDateEdit>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace
{
// Sentinel "from" date for the All time preset. The earliest real entry is far
// newer than this, and QDateEdit's default minimum (1752) accepts it.
const QDate kArchiveAllTimeDate(1970, 1, 1);

QString overflowHandleToolTipText()
{
    if (TranslationManager::instance().getLanguage() == QStringLiteral("zh"))
        return QStringLiteral("\u6a2a\u5411\u62d6\u52a8\u8c03\u6574\u7a97\u53e3\u5bbd\u5ea6\uff0c\u53cc\u51fb\u81ea\u52a8\u9002\u914d");

    return QStringLiteral("Drag horizontally to resize, double-click to auto-fit");
}

class ClickToClearFocusWidget final : public QWidget
{
public:
    explicit ClickToClearFocusWidget(std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_onClick(std::move(onClick))
    {
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_onClick)
            m_onClick();
        event->accept();
    }

private:
    std::function<void()> m_onClick;
};
} // namespace

void SummaryWindow::setupFilterUI()
{
    m_filterToolbarShell = new QWidget(this);
    m_filterToolbarShell->setObjectName("archiveFilterToolbarShell");
    m_filterToolbarShell->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout *toolbarShellLayout = new QHBoxLayout(m_filterToolbarShell);
    // Breathing room: the old row was flush against the window edge, which made the
    // whole bar read as a clipped strip rather than a header.
    toolbarShellLayout->setContentsMargins(10, 5, 6, 5);
    toolbarShellLayout->setSpacing(6);

    m_filterToolbar = new QToolBar(m_filterToolbarShell);
    m_filterToolbar->setObjectName("archiveFilterToolbar");
    m_filterToolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_filterToolbar->setAutoFillBackground(true);
    m_filterToolbar->setMovable(false);
    m_filterToolbar->setFloatable(false);
    m_filterToolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_filterToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolbarShellLayout->addWidget(m_filterToolbar, 1);

    const auto clearToolbarFocus = [this]()
    {
        QWidget *fw = QApplication::focusWidget();
        if (fw && m_filterToolbar && m_filterToolbar->isAncestorOf(fw))
            fw->clearFocus();
        if (m_webView)
            m_webView->focusNative();
        else
            setFocus(Qt::OtherFocusReason);
    };

    const int controlHeight = qMax(28, QFontMetrics(m_filterToolbar->font()).lineSpacing() + 10);
    const int dateWidth = 117;

    // A 1px vertical rule between logical groups. Qt's toolbar separator is drawn by
    // the style and was invisible here, so use an explicit widget that the sheet can
    // colour consistently in both themes.
    const auto makeToolbarSeparator = [this]()
    {
        auto *separator = new QWidget(this);
        separator->setObjectName("archiveToolbarSeparator");
        separator->setAttribute(Qt::WA_StyledBackground, true);
        separator->setFixedSize(1, 18);
        separator->setFocusPolicy(Qt::NoFocus);
        return separator;
    };

    m_filtersGroup = new QWidget(this);
    m_filtersGroup->setObjectName("archiveFiltersGroup");
    m_filtersGroup->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QHBoxLayout *filtersLayout = new QHBoxLayout(m_filtersGroup);
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);
    filtersLayout->setAlignment(Qt::AlignVCenter);

    // One compact range button replaces the two date pickers + their labels.
    m_dateRangeBtn = new QPushButton(this);
    m_dateRangeBtn->setObjectName("dateRangeBtn");
    m_dateRangeBtn->setProperty("variant", "field");
    m_dateRangeBtn->setMinimumHeight(controlHeight);
    m_dateRangeBtn->setMinimumWidth(184);
    m_dateRangeBtn->setCursor(Qt::PointingHandCursor);
    m_dateRangeBtn->setToolTip(TranslationManager::instance().tr("filter_date_range"));
    connect(m_dateRangeBtn, &QPushButton::clicked, this, &SummaryWindow::showDateRangeMenu);
    filtersLayout->addWidget(m_dateRangeBtn);

    m_fromLabel = new QLabel(TranslationManager::instance().tr("filter_from_date") + ":", this);
    m_fromLabel->setProperty("role", "caption");
    m_fromLabel->setAlignment(Qt::AlignVCenter);
    m_fromLabel->setMinimumHeight(controlHeight);
    filtersLayout->addWidget(m_fromLabel);

    m_fromDateEdit = new QDateEdit(this);
    m_fromDateEdit->setObjectName("fromDateEdit");
    m_fromDateEdit->setCalendarPopup(true);
    m_fromDateEdit->setDate(QDate::currentDate().addMonths(-1));
    m_fromDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_fromDateEdit->setMinimumHeight(controlHeight);
    m_fromDateEdit->setFixedWidth(dateWidth);
    m_fromDateEdit->setFocusPolicy(Qt::ClickFocus);
    filtersLayout->addWidget(m_fromDateEdit);

    m_toLabel = new QLabel(TranslationManager::instance().tr("filter_to_date") + ":", this);
    m_toLabel->setProperty("role", "caption");
    m_toLabel->setAlignment(Qt::AlignVCenter);
    m_toLabel->setMinimumHeight(controlHeight);
    filtersLayout->addWidget(m_toLabel);

    m_toDateEdit = new QDateEdit(this);
    m_toDateEdit->setObjectName("toDateEdit");
    m_toDateEdit->setCalendarPopup(true);
    m_toDateEdit->setDate(QDate::currentDate());
    m_toDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_toDateEdit->setMinimumHeight(controlHeight);
    m_toDateEdit->setFixedWidth(dateWidth);
    m_toDateEdit->setFocusPolicy(Qt::ClickFocus);
    filtersLayout->addWidget(m_toDateEdit);

    // The two pickers and their labels are superseded by the single date-range button.
    // They stay alive as the data holders the filter logic reads, but must not be drawn.
    m_fromLabel->setVisible(false);
    m_fromDateEdit->setVisible(false);
    m_toLabel->setVisible(false);
    m_toDateEdit->setVisible(false);

    m_tagFilterBtn = new QPushButton(TranslationManager::instance().tr("filter_all_tags"), this);
    m_tagFilterBtn->setObjectName("tagFilterButton");
    m_tagFilterBtn->setProperty("variant", "field");
    m_tagFilterBtn->setMinimumHeight(controlHeight);
    m_tagFilterBtn->setMinimumWidth(84);
    m_tagFilterBtn->setMaximumWidth(170);
    m_tagFilterBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    filtersLayout->addWidget(m_tagFilterBtn);

    m_clearFilterBtn = new QPushButton(this);
    m_clearFilterBtn->setObjectName("clearFilterBtn");
    m_clearFilterBtn->setProperty("variant", "icon");
    m_clearFilterBtn->setMinimumHeight(controlHeight);
    m_clearFilterBtn->setFixedSize(controlHeight, controlHeight);
    m_clearFilterBtn->setText(QString::fromUtf8("\u2715"));
    m_clearFilterBtn->setCursor(Qt::PointingHandCursor);
    m_clearFilterBtn->setToolTip(TranslationManager::instance().tr("filter_clear"));
    filtersLayout->addWidget(m_clearFilterBtn);

    filtersLayout->addWidget(makeToolbarSeparator());

    // The placeholder already says what the field is for; the caption was pure width.
    m_searchLabel = new QLabel(TranslationManager::instance().tr("filter_search") + ":", this);
    m_searchLabel->setProperty("role", "caption");
    m_searchLabel->setAlignment(Qt::AlignVCenter);
    m_searchLabel->setVisible(false);
    filtersLayout->addWidget(m_searchLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText(TranslationManager::instance().tr("filter_search_placeholder"));
    m_searchEdit->setToolTip(TranslationManager::instance().tr("filter_search"));
    m_searchEdit->setMinimumHeight(controlHeight);
    // Wide enough to read a query at the default 1000px archive width, without
    // pushing the action cluster off a 1024px screen.
    m_searchEdit->setFixedWidth(280);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFocusPolicy(Qt::ClickFocus);
    filtersLayout->addWidget(m_searchEdit);

    m_filterToolbar->addWidget(m_filtersGroup);

    QWidget *spacer = new ClickToClearFocusWidget(clearToolbarFocus, this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_filterToolbar->addWidget(spacer);

    // Pagination gets its own bar at the bottom of the window. It is not a filter, and
    // inside the toolbar it was the first thing clipped once the window got narrow.
    m_paginationBar = new QWidget(this);
    m_paginationBar->setObjectName("archivePaginationBar");
    m_paginationBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout *paginationBarLayout = new QHBoxLayout(m_paginationBar);
    paginationBarLayout->setContentsMargins(6, 4, 6, 4);
    paginationBarLayout->setSpacing(6);
    paginationBarLayout->addStretch();

    m_paginationGroup = new QWidget(m_paginationBar);
    m_paginationGroup->setObjectName("archivePaginationGroup");
    m_paginationGroup->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QHBoxLayout *paginationLayout = new QHBoxLayout(m_paginationGroup);
    paginationLayout->setContentsMargins(0, 0, 0, 0);
    paginationLayout->setSpacing(6);
    paginationLayout->setAlignment(Qt::AlignVCenter);

    // Arrow-only buttons: the label is wasted space in a pager.
    const int navBtnSize = qMax(24, controlHeight);

    m_prevPageBtn = new QToolButton(m_paginationGroup);
    m_prevPageBtn->setObjectName("prevPageBtn");
    // Glyph instead of setArrowType(): a style-sheet colour applies to text reliably,
    // which is what keeps the arrow readable in the dark theme.
    m_prevPageBtn->setText(QString::fromUtf8("\u25C0"));
    m_prevPageBtn->setAutoRaise(true);
    m_prevPageBtn->setFocusPolicy(Qt::NoFocus);
    m_prevPageBtn->setCursor(Qt::PointingHandCursor);
    m_prevPageBtn->setToolTip(TranslationManager::instance().tr("btn_page_prev"));
    m_prevPageBtn->setFixedSize(navBtnSize, navBtnSize);
    paginationLayout->addWidget(m_prevPageBtn);

    m_pageInfoLabel = new QLabel(m_paginationGroup);
    m_pageInfoLabel->setObjectName("pageInfoLabel");
    m_pageInfoLabel->setProperty("role", "caption");
    m_pageInfoLabel->setMinimumHeight(controlHeight);
    m_pageInfoLabel->setAlignment(Qt::AlignCenter);
    m_pageInfoLabel->setMinimumWidth(140);
    paginationLayout->addWidget(m_pageInfoLabel);

    m_nextPageBtn = new QToolButton(m_paginationGroup);
    m_nextPageBtn->setObjectName("nextPageBtn");
    m_nextPageBtn->setText(QString::fromUtf8("\u25B6"));
    m_nextPageBtn->setAutoRaise(true);
    m_nextPageBtn->setFocusPolicy(Qt::NoFocus);
    m_nextPageBtn->setCursor(Qt::PointingHandCursor);
    m_nextPageBtn->setToolTip(TranslationManager::instance().tr("btn_page_next"));
    m_nextPageBtn->setFixedSize(navBtnSize, navBtnSize);
    paginationLayout->addWidget(m_nextPageBtn);

    connect(m_prevPageBtn, &QToolButton::clicked, this, [this]()
            {
                if (!m_archiveUsePagination || m_currentPage <= 1)
                    return;
                --m_currentPage;
                refreshHtml(false);
            });
    connect(m_nextPageBtn, &QToolButton::clicked, this, [this]()
            {
                if (!m_archiveUsePagination || m_currentPage >= m_totalPages)
                    return;
                ++m_currentPage;
                refreshHtml(false);
            });

    paginationBarLayout->addWidget(m_paginationGroup);
    paginationBarLayout->addStretch();

    m_actionsGroup = new QWidget(this);
    m_actionsGroup->setObjectName("archiveActionsGroup");
    m_actionsGroup->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QHBoxLayout *actionsLayout = new QHBoxLayout(m_actionsGroup);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(6);
    actionsLayout->setAlignment(Qt::AlignVCenter);

    // Sort order for the list. Backed by the SQL ORDER BY, so pagination follows too.
    m_sortCombo = new QComboBox(m_actionsGroup);
    m_sortCombo->setObjectName("archiveSortCombo");
    m_sortCombo->addItem(TranslationManager::instance().tr("sort_newest_first"), true);
    m_sortCombo->addItem(TranslationManager::instance().tr("sort_oldest_first"), false);
    m_sortCombo->setMinimumHeight(controlHeight);
    m_sortCombo->setMinimumWidth(112);
    m_sortCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_sortCombo->setToolTip(TranslationManager::instance().tr("tip_sort_order"));
    actionsLayout->addWidget(m_sortCombo);

    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int)
            {
        if (!m_sortCombo)
            return;
        const bool newestFirst = m_sortCombo->currentData().toBool();
        m_sortNewestFirst = newestFirst;
        if (m_historyManager)
            m_historyManager->setSortNewestFirst(newestFirst);
        m_currentPage = 1;
        emit sortOrderChanged(newestFirst);
        refreshHtml(false); });

    // Sorting is a view option; selection and the operation menu are actions.
    actionsLayout->addWidget(makeToolbarSeparator());

    m_selectionModeBtn = new QPushButton(TranslationManager::instance().tr("btn_selection_mode"), this);
    m_selectionModeBtn->setObjectName("selectionModeBtn");
    m_selectionModeBtn->setCheckable(true);
    m_selectionModeBtn->setProperty("variant", "toggle");
    m_selectionModeBtn->setMinimumHeight(controlHeight);
    m_selectionModeBtn->setCursor(Qt::PointingHandCursor);
    m_selectionModeBtn->setToolTip(QString("%1 (%2)")
                                       .arg(TranslationManager::instance().tr("tip_selection_mode"))
                                       .arg(m_selectionToggleKey.toUpper()));
    connect(m_selectionModeBtn, &QPushButton::toggled, this, &SummaryWindow::toggleSelectionMode);
    actionsLayout->addWidget(m_selectionModeBtn);

    // Everything else lives in ONE menu. The previous row of ten side-by-side buttons grew
    // wider than the window as soon as batch selection turned on, and could not adapt.
    // Batch-only entries are disabled (not shown/hidden), so the row never changes width.
    m_actionsMenuBtn = new QToolButton(this);
    m_actionsMenuBtn->setObjectName("archiveActionsMenuBtn");
    m_actionsMenuBtn->setText(TranslationManager::instance().tr("btn_actions_menu") + QString::fromUtf8(" \u25BE"));
    m_actionsMenuBtn->setPopupMode(QToolButton::InstantPopup);
    m_actionsMenuBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_actionsMenuBtn->setMinimumHeight(controlHeight);
    m_actionsMenuBtn->setCursor(Qt::PointingHandCursor);

    m_actionsMenu = new QMenu(m_actionsMenuBtn);

    m_batchSelectAllAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_select_all"));
    connect(m_batchSelectAllAction, &QAction::triggered, this, &SummaryWindow::onBatchSelectAll);

    m_batchAddTagAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_batch_add_tag"));
    connect(m_batchAddTagAction, &QAction::triggered, this, &SummaryWindow::onBatchAddTags);

    m_batchRemoveTagAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_batch_remove_tag"));
    connect(m_batchRemoveTagAction, &QAction::triggered, this, &SummaryWindow::onBatchRemoveTags);

    m_batchDeleteAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_batch_delete"));
    connect(m_batchDeleteAction, &QAction::triggered, this, &SummaryWindow::onBatchDelete);

    m_batchZoteroAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_batch_zotero"));
    connect(m_batchZoteroAction, &QAction::triggered, this, &SummaryWindow::onBatchImportZotero);

    m_actionsMenu->addSeparator();

    m_exportAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_export_selected"));
    connect(m_exportAction, &QAction::triggered, this, &SummaryWindow::onExport);

    m_copyContextAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_copy_context"));
    connect(m_copyContextAction, &QAction::triggered, this, &SummaryWindow::onCopyContext);

    m_copyImagesAction = m_actionsMenu->addAction(TranslationManager::instance().tr("btn_copy_images"));
    connect(m_copyImagesAction, &QAction::triggered, this, &SummaryWindow::onCopyImages);

    m_actionsMenuBtn->setMenu(m_actionsMenu);
    actionsLayout->addWidget(m_actionsMenuBtn);

    // Batch actions are pinned OUTSIDE the QToolBar. Anything added to the toolbar
    // is simply clipped once the window is narrower than its content, and "batch
    // select" disappearing was exactly the reported symptom. Filters may overflow;
    // the batch actions must stay reachable.
    toolbarShellLayout->addWidget(makeToolbarSeparator(), 0, Qt::AlignVCenter);
    toolbarShellLayout->addWidget(m_actionsGroup, 0, Qt::AlignVCenter);

    m_toolbarOverflowButton = new QToolButton(m_filterToolbarShell);
    m_toolbarOverflowButton->setObjectName("archiveOverflowHandle");
    m_toolbarOverflowButton->setText(QString::fromUtf8("\u21D4"));
    m_toolbarOverflowButton->setFocusPolicy(Qt::NoFocus);
    m_toolbarOverflowButton->setCursor(Qt::SizeHorCursor);
    m_toolbarOverflowButton->setMouseTracking(true);
    m_toolbarOverflowButton->setAutoRaise(false);
    m_toolbarOverflowButton->setFixedHeight(controlHeight);
    m_toolbarOverflowButton->setFixedWidth(16);
    m_toolbarOverflowButton->setToolTip(overflowHandleToolTipText());
    m_toolbarOverflowButton->setProperty("toolbarOverflowHandle", true);
    m_toolbarOverflowButton->setProperty("overflowHintActive", false);
    auto *overflowOpacity = new QGraphicsOpacityEffect(m_toolbarOverflowButton);
    overflowOpacity->setOpacity(0.55);
    m_toolbarOverflowButton->setGraphicsEffect(overflowOpacity);
    m_toolbarOverflowButton->installEventFilter(this);
    toolbarShellLayout->addWidget(m_toolbarOverflowButton, 0, Qt::AlignVCenter);

    QTimer::singleShot(0, this, [this]()
                       { refreshToolbarOverflowHint(); });

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout *>(layout());
    if (mainLayout)
    {
        mainLayout->insertWidget(0, m_filterToolbarShell);
        // Added last so the pager stays pinned below the content area.
        mainLayout->addWidget(m_paginationBar);
    }

    clearToolbarFocus();

    // The filter row's natural width is ~930px. Allow the window to go narrower than
    // that; the row clips and the grip turns red rather than the whole window refusing
    // to resize. 820 keeps the date range and tag filter fully readable.
    setMinimumWidth(qMax(minimumWidth(), 820));

    connect(m_fromDateEdit, &QDateEdit::dateChanged, this, &SummaryWindow::applyFilters);
    connect(m_toDateEdit, &QDateEdit::dateChanged, this, &SummaryWindow::applyFilters);

    connect(m_tagFilterBtn, &QPushButton::clicked, this, [this]()
            {
                if (!m_historyManager)
                    return;
                QStringList allTags = m_historyManager->getAllTags();
                TagDialog *dialog = new TagDialog(allTags, m_selectedTags, this, [this]() -> bool {
                    if (m_selectionModeBtn && m_selectionModeBtn->isChecked())
                    {
                        m_selectionModeBtn->setChecked(false);
                        return true;
                    }
                    return false;
                });
                connect(dialog, &TagDialog::tagsUpdated, this, [this](const QStringList &tags) {
                    m_selectedTags = tags;
                    if (m_tagFilterBtn)
                    {
                        if (m_selectedTags.isEmpty())
                        {
                            m_tagFilterBtn->setText(TranslationManager::instance().tr("filter_all_tags"));
                        }
                        else
                        {
                            QString text = m_selectedTags.join(", ");
                            if (text.size() > 28)
                                text = text.left(28) + "...";
                            m_tagFilterBtn->setText(text);
                        }
                    }
                    applyFilters();
                });
                dialog->exec();
                dialog->deleteLater();
            });

    if (!m_searchDebounceTimer)
    {
        m_searchDebounceTimer = new QTimer(this);
        m_searchDebounceTimer->setSingleShot(true);
        m_searchDebounceTimer->setInterval(180);
        connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]()
                { applyFilters(); });
    }

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]()
            {
                if (m_searchDebounceTimer)
                    m_searchDebounceTimer->start();
                else
                    applyFilters();
            });

    connect(m_clearFilterBtn, &QPushButton::clicked, this, [this]()
            {
                m_fromDateEdit->setDate(QDate::currentDate().addMonths(-1));
                m_toDateEdit->setDate(QDate::currentDate());
                m_selectedTags.clear();
                if (m_tagFilterBtn)
                    m_tagFilterBtn->setText(TranslationManager::instance().tr("filter_all_tags"));
                if (m_searchEdit)
                    m_searchEdit->clear();
                if (m_searchDebounceTimer)
                    m_searchDebounceTimer->stop();
                updateDateRangeLabel();
                applyFilters();
            });

    updateDateRangeLabel();
    updatePaginationUi();
}

void SummaryWindow::updateDateRangeLabel()
{
    if (!m_dateRangeBtn || !m_fromDateEdit || !m_toDateEdit)
        return;

    TranslationManager &tm = TranslationManager::instance();
    if (m_fromDateEdit->date() == kArchiveAllTimeDate)
    {
        m_dateRangeBtn->setText(tm.tr("date_range_all") + QString::fromUtf8(" \u25BE"));
        return;
    }

    m_dateRangeBtn->setText(QString("%1 ~ %2 %3")
                                .arg(m_fromDateEdit->date().toString("yyyy-MM-dd"),
                                     m_toDateEdit->date().toString("yyyy-MM-dd"),
                                     QString::fromUtf8("\u25BE")));
}

void SummaryWindow::showDateRangeMenu()
{
    if (!m_dateRangeBtn || !m_fromDateEdit || !m_toDateEdit)
        return;

    TranslationManager &tm = TranslationManager::instance();
    const QDate today = QDate::currentDate();

    QMenu menu(this);

    // "Last N days" means N days INCLUDING today, so the window is today-(N-1)..today.
    auto addPreset = [&](const QString &text, int days)
    {
        QAction *action = menu.addAction(text);
        connect(action, &QAction::triggered, this, [this, today, days]()
                {
            m_fromDateEdit->setDate(days <= 1 ? today : today.addDays(-(days - 1)));
            m_toDateEdit->setDate(today);
            updateDateRangeLabel();
            applyFilters(); });
    };

    addPreset(tm.tr("date_range_today"), 1);
    addPreset(tm.tr("date_range_3d"), 3);
    addPreset(tm.tr("date_range_7d"), 7);
    addPreset(tm.tr("date_range_30d"), 30);
    addPreset(tm.tr("date_range_90d"), 90);
    addPreset(tm.tr("date_range_1y"), 365);

    // Entries older than 90 days used to be unreachable from the toolbar.
    QAction *allTime = menu.addAction(tm.tr("date_range_all"));
    connect(allTime, &QAction::triggered, this, [this, today]()
            {
        m_fromDateEdit->setDate(kArchiveAllTimeDate);
        m_toDateEdit->setDate(today);
        updateDateRangeLabel();
        applyFilters(); });

    menu.addSeparator();
    QAction *custom = menu.addAction(tm.tr("date_range_custom"));
    connect(custom, &QAction::triggered, this, &SummaryWindow::openDateRangeDialog);

    menu.exec(m_dateRangeBtn->mapToGlobal(QPoint(0, m_dateRangeBtn->height())));
}

void SummaryWindow::openDateRangeDialog()
{
    if (!m_fromDateEdit || !m_toDateEdit)
        return;

    TranslationManager &tm = TranslationManager::instance();

    QDialog dialog(this);
    dialog.setWindowTitle(tm.tr("filter_date_range"));
    auto *form = new QFormLayout(&dialog);

    auto *fromEdit = new QDateEdit(m_fromDateEdit->date(), &dialog);
    fromEdit->setCalendarPopup(true);
    fromEdit->setDisplayFormat("yyyy-MM-dd");
    fromEdit->setMinimumWidth(140);
    form->addRow(tm.tr("filter_from_date"), fromEdit);

    auto *toEdit = new QDateEdit(m_toDateEdit->date(), &dialog);
    toEdit->setCalendarPopup(true);
    toEdit->setDisplayFormat("yyyy-MM-dd");
    toEdit->setMinimumWidth(140);
    form->addRow(tm.tr("filter_to_date"), toEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (fromEdit->date() > toEdit->date())
    {
        QMessageBox::warning(this, tm.tr("filter_date_range"), tm.tr("date_range_invalid"));
        return;
    }

    m_fromDateEdit->setDate(fromEdit->date());
    m_toDateEdit->setDate(toEdit->date());
    updateDateRangeLabel();
    applyFilters();
}
