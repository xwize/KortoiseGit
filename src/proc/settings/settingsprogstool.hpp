// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * What the "Diff Viewer" and "Merge Tool" pages share. TortoiseGit's CSettingsProgsDiff and CSettingsProgsMerge each
 * store the command line of an external program, to use in place of TortoiseGitMerge. We have no viewer of our own
 * and run git difftool and git mergetool, so each page chooses the tool git starts: diff.tool or merge.tool.
 */

#pragma once

#include "git/git.hpp"
#include "settings/settingsproppage.hpp"

#include <QString>
#include <QStringList>

class QComboBox;
class QRadioButton;

namespace kgt
{

/**
 * What tells the two tool pages apart.
 */
struct ProgsToolPage {
    QString configKey; // diff.tool or merge.tool
    QString description; // TortoiseGit's heading of the page
    QString defaultHint; // What "Default" means for this tool
    QStringList tools; // The tools we would pick ourselves, as git names them
};

/**
 * A page choosing between our automatic choice of a tool ("Default", which leaves the git setting unset) and a named
 * one ("External"). It reads and writes the user's global configuration, as TortoiseGit's pages do the registry: a
 * repository whose own configuration names a tool still overrides it.
 */
class SettingsProgsTool : public SettingsPropPage
{
    Q_OBJECT

public:
    /**
     * Ported from the pages' OnApply: sets the git setting to the tool chosen, or removes it for "Default".
     * @returns False if git failed, which is shown to the user.
     */
    [[nodiscard]] bool apply() override;

    /**
     * @returns The tool "External" names, as git knows it; empty with "Default" chosen.
     */
    [[nodiscard]] QString tool() const;

    /**
     * Chooses a tool as the user would.
     * @param tool A tool as git names it, or empty for "Default".
     */
    void setTool(const QString& tool);

protected:
    /**
     * @param page What the page chooses.
     * @param parent Parent widget.
     */
    explicit SettingsProgsTool(const ProgsToolPage& page, QWidget *parent = nullptr);

private:
    /**
     * Shows a tool in the fields.
     * @param tool A tool as git names it, or empty for "Default".
     */
    void showTool(const QString& tool);

    /**
     * The handler of every control, the pages' OnBnClickedExtdiffOff, OnBnClickedExtdiffOn and OnEnChangeExtdiff.
     */
    void onChange();

    Git m_git;
    QString m_configKey;
    QRadioButton *m_defaultRadio; // IDC_EXTDIFF_OFF, TortoiseGit's "TortoiseGitMerge"
    QRadioButton *m_externalRadio; // IDC_EXTDIFF_ON
    QComboBox *m_tools; // In place of IDC_EXTDIFF's command line; each item's data is git's name of the tool
};

} // namespace kgt
