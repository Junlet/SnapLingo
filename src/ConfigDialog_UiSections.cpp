#include "ConfigDialog.h"

#include "TranslationManager.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>

void ConfigDialog::setupProfileSection(QVBoxLayout *mainLayout)
{
    TranslationManager &tm = TranslationManager::instance();

    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("profileBar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(14, 8, 14, 8);
    barLayout->setSpacing(8);

    auto *caption = new QLabel(tm.tr("grp_profiles"), bar);
    caption->setObjectName(QStringLiteral("profileCaption"));
    barLayout->addWidget(caption, 0);

    m_profileCombo = new QComboBox(bar);
    m_profileCombo->setObjectName(QStringLiteral("profileCombo"));
    m_profileCombo->setMinimumWidth(200);
    m_profileCombo->setMaximumWidth(300);
    m_profileCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(m_profileCombo, &QComboBox::currentTextChanged, this, &ConfigDialog::onProfileChanged);
    barLayout->addWidget(m_profileCombo, 0);

    auto makeProfileButton = [this, bar](const QString &text)
    {
        auto *button = new QPushButton(text, bar);
        button->setObjectName(QStringLiteral("profileActionButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(28);
        button->setMinimumWidth(52);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        return button;
    };

    m_newProfileBtn = makeProfileButton(tm.tr("btn_new"));
    connect(m_newProfileBtn, &QPushButton::clicked, this, &ConfigDialog::newProfile);
    barLayout->addWidget(m_newProfileBtn);

    m_deleteProfileBtn = makeProfileButton(tm.tr("btn_delete"));
    connect(m_deleteProfileBtn, &QPushButton::clicked, this, &ConfigDialog::deleteProfile);
    barLayout->addWidget(m_deleteProfileBtn);

    m_copyProfileBtn = makeProfileButton(tm.tr("btn_copy"));
    connect(m_copyProfileBtn, &QPushButton::clicked, this, &ConfigDialog::copyProfile);
    barLayout->addWidget(m_copyProfileBtn);

    m_renameProfileBtn = makeProfileButton(tm.tr("btn_rename"));
    connect(m_renameProfileBtn, &QPushButton::clicked, this, &ConfigDialog::renameProfile);
    barLayout->addWidget(m_renameProfileBtn);

    m_importProfileBtn = makeProfileButton(tm.tr("btn_import"));
    m_importProfileBtn->setToolTip(tm.tr("import_profile_title"));
    connect(m_importProfileBtn, &QPushButton::clicked, this, &ConfigDialog::importProfile);
    barLayout->addWidget(m_importProfileBtn);

    m_exportProfileBtn = makeProfileButton(tm.tr("btn_export"));
    m_exportProfileBtn->setToolTip(tm.tr("export_profile_title"));
    connect(m_exportProfileBtn, &QPushButton::clicked, this, &ConfigDialog::exportProfile);
    barLayout->addWidget(m_exportProfileBtn);

    barLayout->addStretch(1);

    auto *hint = new QLabel(tm.tr("settings_profile_hint"), bar);
    hint->setObjectName(QStringLiteral("profileHint"));
    barLayout->addWidget(hint, 0);

    mainLayout->addWidget(bar);
}

void ConfigDialog::setupActionButtons(QVBoxLayout *mainLayout)
{
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("settingsFooter"));
    auto *btnLayout = new QHBoxLayout(footer);
    btnLayout->setContentsMargins(20, 10, 20, 10);
    btnLayout->setSpacing(10);

    m_saveStatusLabel = new QLabel(footer);
    m_saveStatusLabel->setObjectName("saveStatusLabel");
    m_saveStatusLabel->setVisible(false);
    btnLayout->addWidget(m_saveStatusLabel);

    btnLayout->addStretch();

    auto *closeBtn = new QPushButton("Close", footer);
    closeBtn->setProperty("isCloseBtn", true);
    closeBtn->setMinimumHeight(34);
    closeBtn->setMinimumWidth(88);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    // Apply: takes effect now, not written to the profile file.
    auto *applyBtn = new QPushButton("Apply", footer);
    applyBtn->setProperty("isApplyBtn", true);
    applyBtn->setMinimumHeight(34);
    applyBtn->setMinimumWidth(92);
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(applyBtn, &QPushButton::clicked, this, &ConfigDialog::apply);
    btnLayout->addWidget(applyBtn);

    // Save: writes the profile. Neither button closes the dialog.
    auto *saveBtn = new QPushButton("Save", footer);
    saveBtn->setProperty("isSaveBtn", true);
    saveBtn->setDefault(true);
    saveBtn->setMinimumHeight(34);
    saveBtn->setMinimumWidth(92);
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &ConfigDialog::save);
    btnLayout->addWidget(saveBtn);

    mainLayout->addWidget(footer);
}
