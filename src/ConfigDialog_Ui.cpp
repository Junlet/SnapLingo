#include "ConfigDialog.h"

#include "ThemeUtils.h"
#include "TranslationManager.h"

#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QScrollArea>
#include <QTabBar>
#include <QTimer>

ConfigDialog::ConfigDialog(ConfigManager *configManager, HistoryManager *historyManager, QWidget *parent)
    : QDialog(parent), m_configManager(configManager), m_historyManager(historyManager)
{
    setWindowTitle("Settings");

    m_testNam = new QNetworkAccessManager(this);

    m_isDarkTheme = ThemeUtils::isSystemDark();
    setupDialogUi();
    updateTheme(m_isDarkTheme);

    updateProfileList();
    loadFromConfig();
    setupProfilesWatcher();
    retranslateUi();
}

void ConfigDialog::refreshStoragePathPlaceholder()
{
    if (!m_storagePathEdit)
        return;

    m_storagePathEdit->setPlaceholderText(QDir::toNativeSeparators(ConfigManager::defaultStoragePath()));
}

bool ConfigDialog::validateStoragePathInput(const QString &pathText, QString *resolvedPath)
{
    const QString resolved = ConfigManager::resolveStoragePath(pathText);
    QString errorMessage;
    if (!ConfigManager::ensureWritableDirectory(resolved, &errorMessage))
    {
        QMessageBox::warning(
            this, "Storage Path Not Writable",
            QString("The selected storage directory is not writable:\n%1\n\nReason: %2\n\nPlease choose another directory.")
                .arg(QDir::toNativeSeparators(resolved),
                     errorMessage.isEmpty() ? QString("Write access denied.") : errorMessage));
        return false;
    }

    if (resolvedPath)
        *resolvedPath = resolved;
    return true;
}

void ConfigDialog::browseForStoragePath()
{
    const QString currentText = m_storagePathEdit ? m_storagePathEdit->text().trimmed() : QString();
    const QString initialDir = ConfigManager::resolveStoragePath(currentText);
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Storage Directory", initialDir);
    if (dir.isEmpty())
        return;

    QString resolvedPath;
    if (!validateStoragePathInput(dir, &resolvedPath))
        return;

    if (m_storagePathEdit)
        m_storagePathEdit->setText(QDir::toNativeSeparators(resolvedPath));
}

void ConfigDialog::setupDialogUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupHeader(mainLayout);

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("settingsBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 14, 20, 14);
    bodyLayout->setSpacing(12);

    setupProfileSection(bodyLayout);
    setupTabs(bodyLayout);

    mainLayout->addWidget(body, 1);
    setupActionButtons(mainLayout);

    resize(1000, 720);
    setMinimumSize(920, 620);
}

void ConfigDialog::setupHeader(QVBoxLayout *mainLayout)
{
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("settingsHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 14);
    headerLayout->setSpacing(14);

    auto *iconLabel = new QLabel(header);
    iconLabel->setFixedSize(40, 40);
    QIcon icon(QStringLiteral(":/assets/icon.ico"));
    if (!icon.isNull())
        iconLabel->setPixmap(icon.pixmap(40, 40));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    m_headerTitle = new QLabel(TranslationManager::instance().tr("settings_title"), header);
    m_headerTitle->setObjectName(QStringLiteral("settingsTitle"));
    titleCol->addWidget(m_headerTitle);

    m_headerSubtitle = new QLabel(header);
    m_headerSubtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    titleCol->addWidget(m_headerSubtitle);
    headerLayout->addLayout(titleCol, 1);

    mainLayout->addWidget(header);
}

void ConfigDialog::setupTabs(QVBoxLayout *layout)
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("settingsPages"));
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setTabPosition(QTabWidget::North);
    layout->addWidget(m_tabWidget, 1);

    setupGeneralTab();
    setupTranslationTab();
    setupArchiveTab();
    setupOtherTab();
    setupAdvancedApiTab();

    // Translation and Archive are two halves of the same "interface" surface:
    // put them side by side instead of making the user switch tabs for related
    // screenshot/archive settings.
    const QList<QWidget *> mergedPages = {m_transTab, m_archiveTab};
    const QList<QWidget *> allPages = {m_generalTab, m_transTab, m_archiveTab, m_otherTab, m_advancedApiTab};
    for (QWidget *page : allPages)
    {
        if (page && page->layout())
            page->layout()->setContentsMargins(0, 0, 0, 0);
    }
    for (QWidget *page : mergedPages)
    {
        const int index = m_tabWidget->indexOf(page);
        if (index >= 0)
            m_tabWidget->removeTab(index);
    }

    m_interfaceTab = new QWidget(this);
    m_interfaceTab->setObjectName(QStringLiteral("settingsInterfacePage"));
    auto *interfaceLayout = new QHBoxLayout(m_interfaceTab);
    interfaceLayout->setContentsMargins(0, 0, 0, 0);
    interfaceLayout->setSpacing(14);

    auto makeColumn = [this](QWidget *page)
    {
        auto *scroll = new QScrollArea(m_interfaceTab);
        scroll->setObjectName(QStringLiteral("settingsColumnScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        return scroll;
    };
    interfaceLayout->addWidget(makeColumn(m_transTab), 1);
    interfaceLayout->addWidget(makeColumn(m_archiveTab), 1);
    m_tabWidget->insertTab(1, m_interfaceTab, QStringLiteral("Interface"));

    if (auto *transLayout = qobject_cast<QVBoxLayout *>(m_transTab->layout()))
        transLayout->setSpacing(12);
    if (auto *archiveLayout = qobject_cast<QVBoxLayout *>(m_archiveTab->layout()))
        archiveLayout->setSpacing(12);

    m_tabWidget->setCurrentIndex(0);
}
