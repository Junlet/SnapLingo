#include "ConfigDialog.h"
#include "HotkeyEdit.h"
#include "TranslationManager.h"

#include <QCoreApplication>
#include <QFormLayout>

void ConfigDialog::setupOtherTab()
{
    m_otherTab = new QWidget();
    auto *otherMainLayout = new QVBoxLayout(m_otherTab);

    auto *grpShortcuts = new QGroupBox("Shortcut Settings", m_otherTab);
    grpShortcuts->setObjectName("grpShortcuts");
    auto *shortcutsLayout = new QFormLayout(grpShortcuts);

    m_hotkeyEdit = new HotkeyEdit(this);
    shortcutsLayout->addRow("Screenshot Hotkey:", m_hotkeyEdit);

    m_summaryHotkeyEdit = new HotkeyEdit(this);
    shortcutsLayout->addRow("Summary Hotkey:", m_summaryHotkeyEdit);

    m_settingsHotkeyEdit = new HotkeyEdit(this);
    shortcutsLayout->addRow("Settings Hotkey:", m_settingsHotkeyEdit);

    otherMainLayout->addWidget(grpShortcuts);

    auto *grpAdvanced = new QGroupBox("Advanced Settings", m_otherTab);
    grpAdvanced->setObjectName("grpAdvanced");
    auto *advLayout = new QVBoxLayout(grpAdvanced);

    m_launchAtStartupCheck = new QCheckBox("Launch at Startup", this);
    advLayout->addWidget(m_launchAtStartupCheck);

    m_enableUmamiAnalyticsCheck = new QCheckBox("Enable Umami Analytics", this);
    advLayout->addWidget(m_enableUmamiAnalyticsCheck);

    m_debugModeCheck = new QCheckBox("Enable Debug Mode", this);
    advLayout->addWidget(m_debugModeCheck);

    auto *quitRow = new QWidget(this);
    auto *quitRowLayout = new QHBoxLayout(quitRow);
    quitRowLayout->setContentsMargins(0, 0, 0, 0);
    quitRowLayout->setSpacing(8);
    m_quitHotkeyLabel = new QLabel("Quit Hotkey:", this);
    m_quitHotkeyEdit = new HotkeyEdit(this);
    m_quitHotkeyEdit->setPlaceholderText("alt+q");
    m_enableQuitHotkeyCheck = new QCheckBox("Enable", this);
    quitRowLayout->addWidget(m_quitHotkeyLabel);
    quitRowLayout->addWidget(m_quitHotkeyEdit, 1);
    quitRowLayout->addWidget(m_enableQuitHotkeyCheck);
    advLayout->addWidget(quitRow);

    connect(m_enableQuitHotkeyCheck, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_quitHotkeyEdit)
            m_quitHotkeyEdit->setEnabled(on); });
    m_quitHotkeyEdit->setEnabled(false);

    otherMainLayout->addWidget(grpAdvanced);

    auto *grpAbout = new QGroupBox(TranslationManager::instance().tr("grp_about"), m_otherTab);
    grpAbout->setObjectName(QStringLiteral("grpAbout"));
    auto *aboutLayout = new QVBoxLayout(grpAbout);
    auto *versionLabel = new QLabel(QStringLiteral("SnapLingo %1").arg(QCoreApplication::applicationVersion()), grpAbout);
    versionLabel->setObjectName(QStringLiteral("aboutVersionLabel"));
    aboutLayout->addWidget(versionLabel);
    auto *licenseLabel = new QLabel(QStringLiteral("Copyright © 2026 Junlet · GPL-3.0"), grpAbout);
    licenseLabel->setObjectName(QStringLiteral("aboutLicenseLabel"));
    aboutLayout->addWidget(licenseLabel);
    auto *basedLabel = new QLabel(TranslationManager::instance().tr("about_based_on"), grpAbout);
    basedLabel->setObjectName(QStringLiteral("aboutBasedLabel"));
    basedLabel->setWordWrap(true);
    aboutLayout->addWidget(basedLabel);
    otherMainLayout->addWidget(grpAbout);

    otherMainLayout->addStretch();

    m_tabWidget->addTab(m_otherTab, "Other");
}
