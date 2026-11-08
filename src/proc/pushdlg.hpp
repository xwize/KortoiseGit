// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/PushDlg.h (CPushDlg): asks what to push where.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;

namespace kgt
{

/**
 * What PushDlg asks for, the members of CPushDlg that CAppUtils::DoPush takes.
 */
struct PushOptions {
    QString localBranch; // m_BranchSourceName: the local branch or ref; empty deletes the remote branch
    QString remote; // m_URL: the remote's name
    QString remoteBranch; // m_BranchRemoteName: empty pushes to the configured or same-named branch
    bool forceWithLease = false; // m_bForceWithLease: --force-with-lease
    bool tags = false; // m_bTags: --tags
    bool setUpstream = false; // m_bSetUpstream: --set-upstream
};

/**
 * The "Push" dialog, cut down to the essentials: the local branch (the current one to begin with), the remote and
 * remote branch (the configured push target, or the only remote and the same name), and the options Force with
 * lease, Include Tags and Set upstream/track remote branch. Pushing all branches, arbitrary URLs, Force, the
 * push-remote/push-branch settings, submodule recursion and push options are left out for now.
 */
class PushDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Reads the branches and remotes; this runs git, quickly, on the calling thread.
     * @param workingTree The working tree's root.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     * @param selectLocalBranch m_BranchSourceName: what to show as the local branch, such as refs/tags/v1; empty for
     *                          the current branch.
     * @throws RuntimeError (category Git) if the branches or remotes can't be read.
     */
    explicit PushDlg(const QString& workingTree, QWidget *parent = nullptr, const QString& selectLocalBranch = {});

    /**
     * @returns What the dialog shows now.
     */
    [[nodiscard]] PushOptions options() const;

    /**
     * Checks the choices as TortoiseGit's OnBnClickedOk does, asking about deletions, and closes the dialog if
     * they are fine.
     */
    void accept() override;

private:
    /**
     * Ported from CPushDlg::Refresh: fills the lists and selects the defaults.
     * @param selectLocalBranch What to show as the local branch; empty for the current branch.
     */
    void refresh(const QString& selectLocalBranch);

    /**
     * Ported from CPushDlg::GetRemoteBranch: selects the remote and remote branch to push the local branch to.
     * @param localBranch The local branch chosen.
     */
    void selectRemoteBranch(const QString& localBranch);

    /**
     * Enables the options that exclude each other, as OnBnClickedForceWithLease and OnBnClickedTags do.
     */
    void updateOptionStates();

    /**
     * @param name A configuration variable.
     * @returns Its value, or an empty string if it is not set.
     */
    [[nodiscard]] QString configValue(const QString& name) const;

    /**
     * @param name A remote branch name.
     * @returns Whether git accepts it as a branch name.
     */
    [[nodiscard]] bool isBranchNameValid(const QString& name) const;

    Git m_git;
    QComboBox *m_branchSource;
    QComboBox *m_branchRemote;
    QComboBox *m_remote;
    QCheckBox *m_forceWithLease;
    QCheckBox *m_tags;
    QCheckBox *m_setUpstream;
};

} // namespace kgt
