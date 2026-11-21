#include "ConfigDialog.h"

#include "TranslationManager.h"

#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

void ConfigDialog::setupGeneralTab()
{
    m_generalTab = new QWidget();
    auto *tabLayout = new QVBoxLayout(m_generalTab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    auto *scroll = new QScrollArea(m_generalTab);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("settingsScrollContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 6, 0);
    contentLayout->setSpacing(12);

    TranslationManager &tm = TranslationManager::instance();
    auto *card = new QGroupBox(tm.tr("grp_general"), content);
    card->setObjectName(QStringLiteral("generalCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 18, 16, 14);
    cardLayout->setSpacing(10);

    m_generalFormLayout = new QFormLayout();
    auto *layout = m_generalFormLayout;
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->setFormAlignment(Qt::AlignTop);
    layout->setLabelAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);
    cardLayout->addLayout(layout);
    contentLayout->addWidget(card, 1);
    scroll->setWidget(content);
    tabLayout->addWidget(scroll, 1);

    m_languageCombo = new QComboBox(this);
    const QStringList languages = tm.availableLanguages();
    for (const QString &langCode : languages)
        m_languageCombo->addItem(tm.languageDisplayName(langCode), langCode);
    layout->addRow("Language:", m_languageCombo);

    connect(m_languageCombo, &QComboBox::currentIndexChanged, [this](int)
            {
        const QString lang = m_languageCombo->currentData().toString();
        TranslationManager::instance().setLanguage(lang);
        retranslateUi();

        AppConfig cfg = m_configManager->getConfig();
        cfg.language = lang;
        m_configManager->setConfig(cfg);
        
        // Notify App to update all windows
        emit languageChanged(lang); });

    m_screenCombo = new QComboBox(this);
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i)
        m_screenCombo->addItem(screens[i]->name(), i);
    layout->addRow("Capture Screen:", m_screenCombo);

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("API Key:", m_apiKeyEdit);

    m_apiProviderCombo = new QComboBox(this);
    m_apiProviderCombo->addItem("OpenAI", "openai");
    m_apiProviderCombo->addItem("Google Gemini", "gemini");
    m_apiProviderCombo->addItem("Anthropic Claude", "claude");
    layout->addRow("API Provider:", m_apiProviderCombo);

    m_baseUrlEdit = new QLineEdit(this);
    m_endpointPathEdit = new QLineEdit(this);
    m_endpointPathEdit->setPlaceholderText(TranslationManager::instance().tr("endpoint_placeholder"));
    m_endpointPathEdit->setText("/chat/completions");
    m_lastAutoEndpoint = "/chat/completions";
    m_endpointPathEdit->setMinimumWidth(160);

    connect(m_endpointPathEdit, &QLineEdit::textEdited, this, [this](const QString &)
            { m_lastAutoEndpoint.clear(); });

    connect(m_apiProviderCombo, &QComboBox::currentIndexChanged, this, [this](int)
            {
        if (m_isLoadingConfig)
            return;
        const QString provider = m_apiProviderCombo ? m_apiProviderCombo->currentData().toString() : QString();
        if (!provider.isEmpty() && provider != "advanced")
            m_lastRegularProvider = provider;
        maybeApplyEndpointDefaultForProvider(provider); });

    connect(m_apiProviderCombo, &QComboBox::currentIndexChanged, this, [this](int)
            {
        if (m_isLoadingConfig || m_isSyncingAdvanced)
            return;
        syncAdvancedTemplateFromRegular(); });

    auto *baseRowLayout = new QHBoxLayout();
    baseRowLayout->setContentsMargins(0, 0, 0, 0);
    baseRowLayout->setSpacing(8);
    baseRowLayout->addWidget(m_baseUrlEdit, 1);
    baseRowLayout->addWidget(m_endpointPathEdit, 0);
    auto *baseRowWidget = new QWidget(m_generalTab);
    baseRowWidget->setLayout(baseRowLayout);
    baseRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addRow("Base URL:", baseRowWidget);

    m_modelNameEdit = new QLineEdit(this);
    m_testConnectionBtn = new QPushButton("Test", this);
    m_testConnectionBtn->setObjectName("btnTestConnection");
    connect(m_testConnectionBtn, &QPushButton::clicked, this, &ConfigDialog::onTestConnection);

    auto *modelRowLayout = new QHBoxLayout();
    modelRowLayout->setContentsMargins(0, 0, 0, 0);
    modelRowLayout->setSpacing(8);
    modelRowLayout->addWidget(m_modelNameEdit, 1);
    modelRowLayout->addWidget(m_testConnectionBtn, 0);
    auto *modelRowWidget = new QWidget(m_generalTab);
    modelRowWidget->setLayout(modelRowLayout);
    modelRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addRow("Model:", modelRowWidget);

    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setMinimumHeight(90);
    m_promptEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addRow("Prompt:", m_promptEdit);

    m_proxyUrlEdit = new QLineEdit(this);
    m_proxyUrlEdit->setPlaceholderText("e.g., http://127.0.0.1:1080 or socks5://127.0.0.1:1080");
    m_useProxyCheck = new QCheckBox(this);
    m_proxyLabel = new QLabel(this);
    m_proxyUrlEdit->setPlaceholderText(TranslationManager::instance().tr("proxy_placeholder"));

    auto *proxyLayout = new QHBoxLayout();
    proxyLayout->addWidget(m_proxyUrlEdit);
    proxyLayout->addWidget(m_useProxyCheck);
    layout->addRow(m_proxyLabel, proxyLayout);

    m_storagePathEdit = new QLineEdit(this);
    refreshStoragePathPlaceholder();
    m_browseBtn = new QPushButton(TranslationManager::instance().tr("btn_browse"), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &ConfigDialog::browseForStoragePath);

    auto *storageLayout = new QHBoxLayout();
    storageLayout->addWidget(m_storagePathEdit);
    storageLayout->addWidget(m_browseBtn);
    layout->addRow(TranslationManager::instance().tr("lbl_storage"), storageLayout);

    m_showPreviewCheck = new QCheckBox("Show Preview Card after Screenshot", this);
    layout->addRow(m_showPreviewCheck);

    m_showResultCheck = new QCheckBox("Show Translation Result after Screenshot", this);
    layout->addRow(m_showResultCheck);

    // ---- Advanced API parameters, as plain fields ----
    // Appended at the END on purpose: retranslateUi() addresses the rows above by
    // index, so inserting in the middle would shift every translated label.
    m_advancedParamsGroup = new QGroupBox(tm.tr("grp_advanced_params"), m_generalTab);
    m_advancedParamsGroup->setObjectName(QStringLiteral("advancedParamsGroup"));
    auto *advForm = new QFormLayout(m_advancedParamsGroup);
    advForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advForm->setHorizontalSpacing(12);
    advForm->setVerticalSpacing(8);

    m_temperatureSpin = new QDoubleSpinBox(m_advancedParamsGroup);
    m_temperatureSpin->setRange(0.0, 2.0);
    m_temperatureSpin->setSingleStep(0.05);
    m_temperatureSpin->setDecimals(2);
    m_temperatureSpin->setValue(0.2);
    m_temperatureSpin->setToolTip(tm.tr("tip_advanced_params_scope"));
    m_temperatureLabel = new QLabel(tm.tr("lbl_temperature"), m_advancedParamsGroup);
    advForm->addRow(m_temperatureLabel, m_temperatureSpin);

    m_topPSpin = new QDoubleSpinBox(m_advancedParamsGroup);
    m_topPSpin->setRange(0.0, 1.0);
    m_topPSpin->setSingleStep(0.05);
    m_topPSpin->setDecimals(2);
    m_topPSpin->setValue(1.0);
    m_topPSpin->setToolTip(tm.tr("tip_advanced_params_scope"));
    m_topPLabel = new QLabel(tm.tr("lbl_top_p"), m_advancedParamsGroup);
    advForm->addRow(m_topPLabel, m_topPSpin);

    m_maxTokensSpin = new QSpinBox(m_advancedParamsGroup);
    m_maxTokensSpin->setRange(1, 200000);
    m_maxTokensSpin->setSingleStep(64);
    m_maxTokensSpin->setValue(1024);
    m_maxTokensSpin->setToolTip(tm.tr("tip_advanced_params_scope"));
    m_maxTokensLabel = new QLabel(tm.tr("lbl_max_tokens"), m_advancedParamsGroup);
    advForm->addRow(m_maxTokensLabel, m_maxTokensSpin);

    m_timeoutMsSpin = new QSpinBox(m_advancedParamsGroup);
    m_timeoutMsSpin->setRange(1000, 600000);
    m_timeoutMsSpin->setSingleStep(1000);
    m_timeoutMsSpin->setValue(30000);
    m_timeoutMsSpin->setSuffix(" ms");
    m_timeoutMsSpin->setToolTip(tm.tr("tip_advanced_params_scope"));
    m_timeoutLabel = new QLabel(tm.tr("lbl_timeout"), m_advancedParamsGroup);
    advForm->addRow(m_timeoutLabel, m_timeoutMsSpin);

    layout->addRow(m_advancedParamsGroup);

    connect(m_temperatureSpin, &QDoubleSpinBox::valueChanged, this, [this](double)
            { applyAdvancedFieldsToTemplate(); });
    connect(m_topPSpin, &QDoubleSpinBox::valueChanged, this, [this](double)
            { applyAdvancedFieldsToTemplate(); });
    connect(m_maxTokensSpin, &QSpinBox::valueChanged, this, [this](int)
            { applyAdvancedFieldsToTemplate(); });
    connect(m_timeoutMsSpin, &QSpinBox::valueChanged, this, [this](int)
            { applyAdvancedFieldsToTemplate(); });

    m_tabWidget->addTab(m_generalTab, "General");

    // Keep the advanced JSON template aligned while regular mode is active.
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, [this](const QString &)
            { syncAdvancedTemplateFromRegular(); });
    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, [this](const QString &)
            { syncAdvancedTemplateFromRegular(); });
    connect(m_endpointPathEdit, &QLineEdit::textChanged, this, [this](const QString &)
            { syncAdvancedTemplateFromRegular(); });
    connect(m_modelNameEdit, &QLineEdit::textChanged, this, [this](const QString &)
            { syncAdvancedTemplateFromRegular(); });
    connect(m_promptEdit, &QTextEdit::textChanged, this, [this]()
            { syncAdvancedTemplateFromRegular(); });
}

void ConfigDialog::setupAdvancedApiTab()
{
    TranslationManager &tm = TranslationManager::instance();
    m_advancedApiTab = new QWidget();
    auto *rootLayout = new QVBoxLayout(m_advancedApiTab);

    auto *topRow = new QHBoxLayout();
    m_enableAdvancedApiCheck = new QCheckBox(tm.tr("chk_enable_advanced_api"), this);
    m_deleteAdvancedApiConfigBtn = new QPushButton(tm.tr("btn_delete_advanced_api_config"), this);
    m_advancedTemplateStatusLabel = new QLabel(this);
    topRow->addWidget(m_enableAdvancedApiCheck, 0);
    topRow->addWidget(m_deleteAdvancedApiConfigBtn, 0);
    topRow->addSpacing(12);
    topRow->addWidget(m_advancedTemplateStatusLabel, 0);
    topRow->addStretch(1);
    rootLayout->addLayout(topRow);

    m_advancedApiTemplateEdit = new QPlainTextEdit(this);
    m_advancedApiTemplateEdit->setPlaceholderText(tm.tr("ph_advanced_api_template"));
    m_advancedApiTemplateEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_advancedApiTemplateEdit->setMinimumHeight(100);
    rootLayout->addWidget(m_advancedApiTemplateEdit, 1);

    auto *testRow = new QHBoxLayout();
    m_testAdvancedApiBtn = new QPushButton(tm.tr("btn_test_advanced_api"), this);
    m_pickAdvancedJsonFieldsBtn = new QPushButton(tm.tr("btn_select_json_fields"), this);
    m_showAdvancedDebugInResultCheck = new QCheckBox(tm.tr("chk_adv_debug_result_short"), this);
    m_showAdvancedDebugInArchiveCheck = new QCheckBox(tm.tr("chk_adv_debug_archive_short"), this);
    m_pickAdvancedJsonFieldsBtn->setEnabled(false);
    testRow->addWidget(m_testAdvancedApiBtn, 0);
    testRow->addWidget(m_pickAdvancedJsonFieldsBtn, 0);
    testRow->addStretch(1);
    rootLayout->addLayout(testRow);

    auto *debugRow = new QHBoxLayout();
    m_advancedDebugDisplayLabel = new QLabel(tm.tr("lbl_adv_debug_display"), this);
    debugRow->addWidget(m_advancedDebugDisplayLabel, 0);
    debugRow->addWidget(m_showAdvancedDebugInResultCheck, 0);
    debugRow->addWidget(m_showAdvancedDebugInArchiveCheck, 0);
    debugRow->addStretch(1);
    rootLayout->addLayout(debugRow);

    m_advancedApiResultEdit = new QPlainTextEdit(this);
    m_advancedApiResultEdit->setReadOnly(true);
    m_advancedApiResultEdit->setPlaceholderText(tm.tr("ph_advanced_api_result"));
    m_advancedApiResultEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_advancedApiResultEdit->setMinimumHeight(110);
    rootLayout->addWidget(m_advancedApiResultEdit, 1);

    connect(m_enableAdvancedApiCheck, &QCheckBox::toggled, this, [this](bool checked)
            {
        if (m_isLoadingConfig)
            return;

        if (!checked)
            syncAdvancedTemplateFromRegular();

        updateAdvancedApiUiState(); });

    connect(m_deleteAdvancedApiConfigBtn, &QPushButton::clicked, this, [this]()
            { resetAdvancedApiToDefault(); });

    connect(m_advancedApiTemplateEdit, &QPlainTextEdit::textChanged, this, [this]()
            {
        if (m_isLoadingConfig || m_isSyncingAdvanced)
            return;
        if (m_enableAdvancedApiCheck && m_enableAdvancedApiCheck->isChecked())
        {
            m_advancedTemplateDetached = true;
            updateAdvancedTemplateStatusLabel();
        }
        // Typing in the template writes back into the General-tab fields (the shared
        // ones as well as the advanced parameter spins).
        syncRegularFieldsFromAdvancedTemplate(); });

    connect(m_testAdvancedApiBtn, &QPushButton::clicked, this, &ConfigDialog::onTestAdvancedApi);
    m_pickAdvancedJsonFieldsBtn->installEventFilter(this);
    connect(m_pickAdvancedJsonFieldsBtn, &QPushButton::clicked, this, &ConfigDialog::onPickAdvancedJsonFields);

    m_tabWidget->addTab(m_advancedApiTab, tm.tr("tab_advanced_api"));
}




