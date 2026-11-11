// SPDX-FileCopyrightText: 2008-2021, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingGitConfig.cpp, Settings/GitSettings.h and the
 * IDD_SETTINGIT_CONFIG resource. TortoiseGit reads each level with libgit2; here git config does, with --local,
 * --global or --system.
 */

#include "settings/settinggitconfig.hpp"

#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>

#include <tuple>

namespace
{

using namespace kgt;

// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

/**
 * @param source A config source.
 * @returns The level git config reads for it.
 */
ConfigLevel levelOf(SettingGitConfig::ConfigSource source)
{
    switch (source) {
    case SettingGitConfig::ConfigSource::Local:
        return ConfigLevel::Local;
    case SettingGitConfig::ConfigSource::Global:
        return ConfigLevel::Global;
    case SettingGitConfig::ConfigSource::System:
        return ConfigLevel::System;
    case SettingGitConfig::ConfigSource::Effective:
        break;
    }
    return ConfigLevel::Effective;
}

/**
 * Ported from CGit::GetUserName and GetUserEmail: the environment overrides the configuration.
 * @param git Runs git.
 * @param variable GIT_AUTHOR_NAME or GIT_AUTHOR_EMAIL.
 * @param key user.name or user.email.
 * @returns The value git commits with.
 */
QString effectiveUserValue(const Git& git, const char *variable, const QString& key)
{
    const QString fromEnvironment = qEnvironmentVariable(variable);
    return fromEnvironment.isEmpty() ? git.configValue(key) : fromEnvironment;
}

/**
 * CGit::GetGitGlobalConfig.
 * @returns The user's ~/.gitconfig, or the file $GIT_CONFIG_GLOBAL names instead.
 */
QString globalConfigFile()
{
    const QString fromEnvironment = qEnvironmentVariable("GIT_CONFIG_GLOBAL");
    return fromEnvironment.isEmpty() ? QDir::home().filePath(QStringLiteral(".gitconfig")) : fromEnvironment;
}

/**
 * CGit::GetGitGlobalXDGConfig.
 * @returns $XDG_CONFIG_HOME/git/config.
 */
QString globalXdgConfigFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("git/config"));
}

/**
 * CGit::GetGitSystemConfig.
 * @param git Runs git.
 * @returns The machine's configuration file.
 */
QString systemConfigFile(const Git& git)
{
    try {
        // Since git 2.40; older ones only have the compiled-in default
        const QString file = QString::fromUtf8(git.run({QStringLiteral("var"), QStringLiteral("GIT_CONFIG_SYSTEM")}, RunMode::ReadOnly)).trimmed();
        if (!file.isEmpty()) {
            return file;
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    return QStringLiteral("/etc/gitconfig");
}

/**
 * CGit::GetGitLocalConfig.
 * @param repository A working tree's root or a bare repository.
 * @returns The repository's own config file, which a linked worktree shares with the main one.
 */
QString localConfigFile(const QString& repository)
{
    if (GitAdminDir::isBareRepo(repository)) {
        return QDir(repository).filePath(QStringLiteral("config"));
    }
    const QString adminDir = GitAdminDir::commonAdminDirPath(GitAdminDir::worktreeAdminDirPath(repository));
    return QDir(adminDir).filePath(QStringLiteral("config"));
}

/**
 * @param parent Its parent.
 * @returns The checkbox, with IDS_SETTINGS_GITCONFIG_INHERIT_TT.
 */
QCheckBox *inheritCheckBox(QWidget *parent)
{
    auto *box = new QCheckBox(i18n("inherit"), parent);
    box->setToolTip(i18n("If this is enabled, no value is stored or overwritten at this config level."));
    return box;
}

} // namespace

namespace kgt
{

SettingGitConfig::SettingGitConfig(const QString& repository, QWidget *parent)
    : SettingsPropPage(parent)
    , m_git(repository.isEmpty() ? QDir::homePath() : repository)
    , m_global(repository.isEmpty())
    , m_effectiveRadio(new QRadioButton(i18n("Effective"), this))
    , m_localRadio(new QRadioButton(i18n("Local"), this))
    , m_globalRadio(new QRadioButton(i18n("Global"), this))
    , m_systemRadio(new QRadioButton(i18n("System"), this))
    , m_userName(new QLineEdit(this))
    , m_inheritUserName(inheritCheckBox(this))
    , m_userEmail(new QLineEdit(this))
    , m_inheritEmail(inheritCheckBox(this))
    , m_userSigningKey(new QLineEdit(this))
    , m_inheritSigningKey(inheritCheckBox(this))
    , m_prune(new QCheckBox(i18n("Prune"), this))
    , m_saveTo(new QComboBox(this))
{
    auto *layout = new QVBoxLayout(this);

    // Effective | Local << Global << System: the levels, the more specific overriding the ones to its right
    auto *sourceGroup = new QGroupBox(i18n("Config source"), this);
    auto *sourceLayout = new QHBoxLayout(sourceGroup);
    sourceLayout->addWidget(m_effectiveRadio);
    sourceLayout->addWidget(new QLabel(QStringLiteral("|"), sourceGroup));
    sourceLayout->addWidget(m_localRadio);
    sourceLayout->addWidget(new QLabel(QStringLiteral("<<"), sourceGroup));
    sourceLayout->addWidget(m_globalRadio);
    sourceLayout->addWidget(new QLabel(QStringLiteral("<<"), sourceGroup));
    sourceLayout->addWidget(m_systemRadio);
    sourceLayout->addStretch();
    m_localRadio->setToolTip(i18n(".git/config in the repository root"));
    m_globalRadio->setToolTip(i18n("~/.gitconfig or $XDG_CONFIG_HOME/git/config"));
    layout->addWidget(sourceGroup);

    auto *userGroup = new QGroupBox(i18n("User Info"), this);
    auto *userLayout = new QGridLayout(userGroup);
    const auto addRow = [userGroup, userLayout](int row, const QString& label, QLineEdit *edit, QCheckBox *inherit) {
        auto *text = new QLabel(label, userGroup);
        text->setBuddy(edit);
        userLayout->addWidget(text, row, 0);
        userLayout->addWidget(edit, row, 1);
        userLayout->addWidget(inherit, row, 2);
    };
    addRow(0, i18n("&Name:"), m_userName, m_inheritUserName);
    addRow(1, i18n("&Email:"), m_userEmail, m_inheritEmail);
    addRow(2, i18n("&Signing key ID:"), m_userSigningKey, m_inheritSigningKey);
    userLayout->setColumnStretch(1, 1);
    layout->addWidget(userGroup);

    m_prune->setTristate(true);
    m_prune->setToolTip(i18n("This option represents \"%1\" git config setting.", QStringLiteral("fetch.prune")));
    layout->addWidget(m_prune);

    auto *saveToLayout = new QHBoxLayout;
    auto *saveToLabel = new QLabel(i18n("Save to:"), this);
    saveToLabel->setBuddy(m_saveTo);
    saveToLayout->addWidget(saveToLabel);
    saveToLayout->addWidget(m_saveTo);
    saveToLayout->addStretch();
    layout->addLayout(saveToLayout);

    // TortoiseGit also has "Edit .tgitconfig", a file we don't read, and a separate "View systemwide gitconfig" for
    // a user who may not write it; the editor deals with that here
    auto *editLocal = new QPushButton(GitAdminDir::isBareRepo(repository) ? i18n("Edit local git config") : i18n("Edit &local .git/config"), this);
    auto *viewEffective = new QPushButton(i18n("&View effective config"), this);
    auto *editGlobal = new QPushButton(i18n("Edit gl&obal .gitconfig"), this);
    auto *editGlobalXdg = new QPushButton(i18n("Edit global &XDG git/config"), this);
    auto *editSystem = new QPushButton(i18n("Edit s&ystemwide gitconfig"), this);
    auto *buttonLayout = new QGridLayout;
    buttonLayout->addWidget(editLocal, 0, 0);
    buttonLayout->addWidget(viewEffective, 1, 0);
    buttonLayout->addWidget(editGlobal, 0, 1);
    buttonLayout->addWidget(editGlobalXdg, 1, 1);
    buttonLayout->addWidget(editSystem, 0, 2);
    buttonLayout->setColumnStretch(3, 1);
    layout->addLayout(buttonLayout);
    layout->addStretch();

    const QString systemConfig = systemConfigFile(m_git);
    editLocal->setEnabled(!m_global);
    editGlobalXdg->setVisible(QFileInfo(globalXdgConfigFile()).dir().exists());
    editSystem->setEnabled(QFile::exists(systemConfig));
    connect(editLocal, &QPushButton::clicked, this, [this, repository] {
        launchEditor(localConfigFile(repository), false);
    });
    connect(viewEffective, &QPushButton::clicked, this, &SettingGitConfig::viewEffectiveConfig);
    connect(editGlobal, &QPushButton::clicked, this, [this] {
        launchEditor(globalConfigFile(), true);
    });
    connect(editGlobalXdg, &QPushButton::clicked, this, [this] {
        launchEditor(globalXdgConfigFile(), true);
    });
    connect(editSystem, &QPushButton::clicked, this, [this, systemConfig] {
        launchEditor(systemConfig, false);
    });

    // CGitSettings::InitGitSettings
    m_localRadio->setEnabled(!m_global);
    if (!m_global) {
        m_saveTo->addItem(i18n("Local"), QVariant::fromValue(ConfigLevel::Local));
    }
    m_saveTo->addItem(i18n("Global"), QVariant::fromValue(ConfigLevel::Global));
    m_effectiveRadio->setChecked(true);

    // Only what the user does counts as a change, not loadData() filling the fields in
    for (const QLineEdit *edit : {m_userName, m_userEmail, m_userSigningKey}) {
        connect(edit, &QLineEdit::textEdited, this, &SettingGitConfig::onChange);
    }
    for (const QCheckBox *box : {m_inheritUserName, m_inheritEmail, m_inheritSigningKey, m_prune}) {
        connect(box, &QCheckBox::clicked, this, &SettingGitConfig::onChange);
    }
    connect(m_saveTo, &QComboBox::activated, this, &SettingGitConfig::onChange);
    const auto connectSource = [this](QRadioButton *radio, ConfigSource source) {
        connect(radio, &QRadioButton::clicked, this, [this, source] {
            setConfigSource(source);
        });
    };
    connectSource(m_effectiveRadio, ConfigSource::Effective);
    connectSource(m_localRadio, ConfigSource::Local);
    connectSource(m_globalRadio, ConfigSource::Global);
    connectSource(m_systemRadio, ConfigSource::System);

    loadData();

    // CSettingGitConfig::OnInitDialog: with no name or e-mail yet, start on "Global", ready to type them in
    if (m_userName->text().isEmpty() && m_userEmail->text().isEmpty()) {
        m_globalRadio->setChecked(true);
        setConfigSource(ConfigSource::Global);
        m_inheritUserName->setChecked(false);
        m_inheritEmail->setChecked(false);
        enDisableControls();
    }
}

bool SettingGitConfig::apply()
{
    if (!m_needSave) {
        return true;
    }
    // CGitSettings::SafeData
    const ConfigLevel destination = m_global ? ConfigLevel::Global : m_saveTo->currentData().value<ConfigLevel>();
    if (!warnUserSafeToDifferentDestination(destination)) {
        return false;
    }
    // CSettingGitConfig::SafeDataImpl
    const Qt::CheckState prune = m_prune->checkState();
    const QString pruneValue = prune == Qt::Checked ? QStringLiteral("true") : QStringLiteral("false");
    if (!save(destination, QStringLiteral("user.name"), m_userName->text(), m_inheritUserName->isChecked())
        || !save(destination, QStringLiteral("user.email"), m_userEmail->text(), m_inheritEmail->isChecked())
        || !save(destination, QStringLiteral("user.signingkey"), m_userSigningKey->text(), m_inheritSigningKey->isChecked())
        || !save(destination, QStringLiteral("fetch.prune"), pruneValue, prune == Qt::PartiallyChecked)) {
        return false;
    }
    m_needSave = false;
    setModified(false);
    return true;
}

void SettingGitConfig::setConfigSource(ConfigSource source)
{
    m_configSource = source;
    if (source == ConfigSource::Local || source == ConfigSource::Global) {
        const int index = m_saveTo->findData(QVariant::fromValue(levelOf(source)));
        if (index >= 0) {
            m_saveTo->setCurrentIndex(index);
        }
    }
    loadData();
}

SettingGitConfig::ConfigSource SettingGitConfig::configSource() const
{
    return m_configSource;
}

void SettingGitConfig::showEvent(QShowEvent *event)
{
    SettingsPropPage::showEvent(event);
    if (m_hintShown) {
        return;
    }
    m_hintShown = true;
    // Queued: a message box opened from inside a show event would come up before the page itself
    QMetaObject::invokeMethod(
        this,
        [this] {
            KMessageBox::information(
                this,
                i18n("Git uses the concept of a hierarchical configuration.\nI.e. there are multiple levels; settings in higher levels override "
                     "values in lower levels.\nThe \"Effective\" tab shows you the effective values for the current scope (read-only).\nSelect any "
                     "level to see the values stored there.\nIn order to change settings select a level, enter the values and select where to "
                     "store to."),
                QString(),
                QStringLiteral("HintHierarchicalConfig"),
                noNotify);
        },
        Qt::QueuedConnection);
}

void SettingGitConfig::loadData()
{
    const ConfigLevel level = levelOf(m_configSource);
    const auto load = [this, level](const QString& key, QLineEdit *edit, QCheckBox *inherit) {
        const std::optional<QString> value = m_git.configValueAt(key, level);
        edit->setText(value.value_or(QString()));
        inherit->setChecked(!value);
    };
    load(QStringLiteral("user.signingkey"), m_userSigningKey, m_inheritSigningKey);
    // The name and e-mail can also come from environment variables, for the effective settings
    if (m_configSource == ConfigSource::Effective) {
        m_userName->setText(effectiveUserValue(m_git, "GIT_AUTHOR_NAME", QStringLiteral("user.name")));
        m_userEmail->setText(effectiveUserValue(m_git, "GIT_AUTHOR_EMAIL", QStringLiteral("user.email")));
        m_inheritUserName->setChecked(false);
        m_inheritEmail->setChecked(false);
        m_inheritSigningKey->setChecked(false);
    } else {
        load(QStringLiteral("user.name"), m_userName, m_inheritUserName);
        load(QStringLiteral("user.email"), m_userEmail, m_inheritEmail);
    }

    if (const std::optional<QString> prune = m_git.configValueAt(QStringLiteral("fetch.prune"), level)) {
        m_prune->setCheckState(Git::isConfigTrue(*prune) ? Qt::Checked : Qt::Unchecked);
    } else {
        // Not set: git's default for the effective value, "not stored here" for a level
        m_prune->setCheckState(m_configSource == ConfigSource::Effective ? Qt::Unchecked : Qt::PartiallyChecked);
    }

    m_needSave = false;
    setModified(false);
    enDisableControls();
}

void SettingGitConfig::enDisableControls()
{
    const bool effective = m_configSource == ConfigSource::Effective;
    m_userName->setReadOnly(effective);
    m_userEmail->setReadOnly(effective);
    m_userSigningKey->setReadOnly(effective);
    m_prune->setEnabled(!effective);
    m_saveTo->setEnabled(!effective);
    m_inheritUserName->setEnabled(!effective);
    m_inheritEmail->setEnabled(!effective);
    m_inheritSigningKey->setEnabled(!effective);

    m_userName->setEnabled(!m_inheritUserName->isChecked());
    m_userEmail->setEnabled(!m_inheritEmail->isChecked());
    m_userSigningKey->setEnabled(!m_inheritSigningKey->isChecked());
}

void SettingGitConfig::onChange()
{
    enDisableControls();
    m_needSave = true;
    setModified();
}

bool SettingGitConfig::save(ConfigLevel level, const QString& key, const QString& value, bool deleteKey)
{
    try {
        if (!deleteKey) {
            m_git.setConfigValue(key, value, level);
        } else if (m_git.configValueAt(key, level)) {
            m_git.unsetConfigValue(key, level);
        }
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        const QString message = i18n("Saving config failed (key: \"%1\", value: \"%2\").", key, value);
        KMessageBox::error(this, message + QLatin1Char('\n') + e.error().message, QString(), noNotify);
        return false;
    }
}

bool SettingGitConfig::warnUserSafeToDifferentDestination(ConfigLevel destination)
{
    if (destination == levelOf(m_configSource)) {
        return true;
    }
    const QString name = destination == ConfigLevel::Global ? i18n("Global") : i18n("Local");
    const auto answer = KMessageBox::questionTwoActions(this,
                                                        i18n("Are you sure you want to save to \"%1\"?", name),
                                                        QString(),
                                                        KGuiItem(i18n("&Save")),
                                                        KGuiItem(i18n("A&bort")),
                                                        QString(),
                                                        noNotify);
    return answer == KMessageBox::PrimaryAction;
}

void SettingGitConfig::viewEffectiveConfig()
{
    QByteArray config;
    try {
        config = m_git.run({QStringLiteral("config"), QStringLiteral("--show-origin"), QStringLiteral("-l")}, RunMode::ReadOnly);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, i18n("Could not get effective git config:\n%1", e.error().message), QString(), noNotify);
        return;
    }
    // Kept after we exit, for the editor to read: it is started without waiting for it
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("kortoisegit-effective-config-XXXXXX.txt")));
    file.setAutoRemove(false);
    if (!file.open() || file.write(config) != config.size()) {
        KMessageBox::error(this, i18n("Could not create temp file."), QString(), noNotify);
        return;
    }
    file.close();
    file.setPermissions(QFile::ReadOwner);
    launchEditor(file.fileName(), false);
}

void SettingGitConfig::launchEditor(const QString& file, bool create)
{
    if (create && !QFile::exists(file)) {
        // git creates it on the first "git config --global"; an empty one changes nothing
        QDir().mkpath(QFileInfo(file).absolutePath());
        std::ignore = QFile(file).open(QIODevice::WriteOnly | QIODevice::Append);
    }
    // KIO jobs delete themselves when they finish. Opened as plain text: a config file has no extension to tell by
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(file), QStringLiteral("text/plain"));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

} // namespace kgt
