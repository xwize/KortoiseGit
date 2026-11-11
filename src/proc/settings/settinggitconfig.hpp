// SPDX-FileCopyrightText: 2008-2021, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingGitConfig.h (CSettingGitConfig) and its base
 * Settings/GitSettings.h (CGitSettings), which is folded in here while this is the only page built on it.
 */

#pragma once

#include "git/git.hpp"
#include "settings/settingsproppage.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace kgt
{

/**
 * The Settings dialog's "Git" page: the user's name, e-mail and signing key and fetch.prune, shown for one
 * configuration level at a time (CGitSettings' config source) and saved to the repository's or the user's
 * configuration, and buttons that open the configuration files in an editor.
 *
 * Left out: AutoCrLf, SafeCrLf and QuotePath, which nothing here depends on, and the project level (.tgitconfig).
 */
class SettingGitConfig : public SettingsPropPage
{
    Q_OBJECT

public:
    /**
     * CGitSettings' CFG_SRC_*, without the project level.
     */
    enum class ConfigSource {
        Effective, // CFG_SRC_EFFECTIVE: what git uses, read-only
        Local, // CFG_SRC_LOCAL
        Global, // CFG_SRC_GLOBAL
        System, // CFG_SRC_SYSTEM
    };

    /**
     * Reads the settings; this runs git, quickly, on the calling thread.
     * @param repository The working tree's root or bare repository the dialog is opened for; empty for none, which
     *                   leaves only the user's and the machine's configuration.
     * @param parent Parent widget.
     */
    explicit SettingGitConfig(const QString& repository, QWidget *parent = nullptr);

    /**
     * Ported from CSettingGitConfig::OnApply and CGitSettings::SafeData.
     * @returns False if the user chose not to save to another level than the one shown, or git failed.
     */
    [[nodiscard]] bool apply() override;

    /**
     * The config source radio buttons' handler, GITSETTINGS_RADIO_EVENT_HANDLE.
     * @param source The level to show.
     */
    void setConfigSource(ConfigSource source);

    /**
     * @returns The level shown.
     */
    [[nodiscard]] ConfigSource configSource() const;

protected:
    /**
     * Shows IDS_HIERARCHICALCONFIG the first time the page is shown, as InitGitSettings does when MFC creates it.
     * @param event The event.
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * Ported from CGitSettings::LoadData and CSettingGitConfig::LoadDataImpl.
     */
    void loadData();

    /**
     * Ported from CSettingGitConfig::EnDisableControls.
     */
    void enDisableControls();

    /**
     * Ported from CSettingGitConfig::OnChange.
     */
    void onChange();

    /**
     * Ported from CGitSettings::Save.
     * @param level The file to write to.
     * @param key A configuration key.
     * @param value Its new value.
     * @param deleteKey Whether to remove the key instead.
     * @returns False if git failed, which is shown to the user.
     */
    [[nodiscard]] bool save(ConfigLevel level, const QString& key, const QString& value, bool deleteKey);

    /**
     * Ported from CGitSettings::WarnUserSafeToDifferentDestination.
     * @param destination The level about to be written.
     * @returns False if it isn't the level shown and the user chose not to save there.
     */
    [[nodiscard]] bool warnUserSafeToDifferentDestination(ConfigLevel destination);

    /**
     * Ported from CSettingGitConfig::OnBnClickedVieweffectivegitconfig.
     */
    void viewEffectiveConfig();

    /**
     * Opens a file in the user's text editor with AppUtils::launchAlternativeEditor.
     * @param file The file; created empty if it doesn't exist yet and @p create is set.
     * @param create Whether a missing file is created, for a configuration file git would create itself.
     */
    void launchEditor(const QString& file, bool create);

    Git m_git;
    bool m_global; // m_bGlobal: not in a repository
    bool m_needSave = false; // m_bNeedSave
    bool m_hintShown = false;
    ConfigSource m_configSource = ConfigSource::Effective; // m_iConfigSource
    QRadioButton *m_effectiveRadio;
    QRadioButton *m_localRadio;
    QRadioButton *m_globalRadio;
    QRadioButton *m_systemRadio;
    QLineEdit *m_userName;
    QCheckBox *m_inheritUserName;
    QLineEdit *m_userEmail;
    QCheckBox *m_inheritEmail;
    QLineEdit *m_userSigningKey;
    QCheckBox *m_inheritSigningKey;
    QCheckBox *m_prune;
    QComboBox *m_saveTo; // m_cSaveTo; each item's data is its ConfigLevel
};

} // namespace kgt
