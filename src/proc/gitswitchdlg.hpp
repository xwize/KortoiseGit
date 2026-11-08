// SPDX-FileCopyrightText: 2008-2013, 2015-2017, 2019-2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitSwitchDlg.h (CGitSwitchDlg): the Switch/Checkout dialog.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QDialogButtonBox;
class QLineEdit;

namespace kgt
{

class ChooseVersion;

/**
 * What the Switch/Checkout dialog asks for: CGitSwitchDlg's members that CAppUtils::Switch reads.
 */
struct GitSwitchOptions {
    QString versionName; // m_VersionName: the branch, tag or commit to switch to
    bool isNewBranch = false; // m_bBranch: "Create New Branch"
    QString newBranch; // m_NewBranch: its name, trimmed
    bool force = false; // m_bForce: -f
    bool merge = false; // m_bMerge: --merge
    Qt::CheckState track = Qt::PartiallyChecked; // m_bTrack: --track, --no-track, or neither
    bool branchOverride = false; // m_bBranchOverride: -B in place of -b
};

/**
 * The Switch/Checkout dialog: a ChooseVersion for what to switch to and the options of git checkout.
 */
class GitSwitchDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the branches and tags.
     * @param git Runs git in the working tree's root.
     * @param initialRefName What to select first (m_initialRefName); empty for the current branch.
     * @param parent Parent widget.
     */
    explicit GitSwitchDlg(Git git, const QString& initialRefName = {}, QWidget *parent = nullptr);

    /**
     * @returns What the user chose. Valid after the dialog was accepted.
     */
    [[nodiscard]] const GitSwitchOptions& options() const;

    /**
     * @returns The version chooser.
     */
    [[nodiscard]] ChooseVersion *chooseVersion() const;

    /**
     * Ported from CGitSwitchDlg::OnBnClickedOk: checks the new branch's name, remembers whether a tag or commit
     * was switched to with a new branch, and collects the options.
     */
    void accept() override;

private:
    void setDefaultName();
    void onNewBranchChanged();

    Git m_git;
    GitSwitchOptions m_options;
    ChooseVersion *m_chooseVersion;
    QCheckBox *m_branch;
    QLineEdit *m_newBranch;
    QCheckBox *m_force;
    QCheckBox *m_merge;
    QCheckBox *m_track;
    QCheckBox *m_branchOverride;
    QDialogButtonBox *m_buttons;
    bool m_newBranchForTag = true; // SwitchToTagNewBranch
    bool m_newBranchForCommit = true; // SwitchToCommitNewBranch
};

} // namespace kgt
