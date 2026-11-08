// SPDX-FileCopyrightText: 2008-2022, 2024-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/PullFetchDlg.h (CPullFetchDlg): asks where to pull or fetch from.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace kgt
{

/**
 * What PullFetchDlg asks for, the members of CPullFetchDlg that CAppUtils::DoPull and DoFetch take.
 */
struct PullFetchOptions {
    QString remoteUrl; // m_RemoteURL: a remote's name or a URL
    QString remoteBranchName; // m_RemoteBranchName: empty for the remote's configured refspecs
    bool allRemotes = false; // m_bAllRemotes: fetch from every remote ("- all -")
    bool rebase = false; // m_bRebase: fetch, then rebase; not offered yet
    bool noFF = false; // m_bNoFF: --no-ff
    bool ffOnly = false; // m_bFFonly: --ff-only
    bool squash = false; // m_bSquash: --squash
    bool noCommit = false; // m_bNoCommit: --no-commit
    Qt::CheckState fetchTags = Qt::PartiallyChecked; // m_bFetchTags: --tags, --no-tags or git's default
    Qt::CheckState prune = Qt::PartiallyChecked; // m_bPrune: --prune, --no-prune or git's default
    std::optional<int> depth; // m_bDepth and m_nDepth: --depth, offered in a shallow repository only
};

/**
 * The "Pull" and "Fetch" dialog: one class with two modes, as TortoiseGit's m_IsPull. Fetch disables the merge
 * options (Squash, No Commit, No Fast Forward, Fast Forward Only), adds "- all -" when there are several remotes
 * and leaves the remote branch to the remote's refspecs.
 *
 * Left out for now: the "..." remote ref browser (SelectRemoteRefDlg), Manage Remotes, the PuTTY key, Help and a
 * submodule's default branch. "Launch Rebase After Fetch" is not offered yet.
 */
class PullFetchDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Reads the remotes and the current branch's upstream; this runs git, quickly, on the calling thread.
     * @param workingDirectory The working tree's root, or a bare repository.
     * @param isPull True for Pull, false for Fetch: m_IsPull.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     * @throws RuntimeError (category Git) if the remotes can't be read.
     */
    PullFetchDlg(const QString& workingDirectory, bool isPull, QWidget *parent = nullptr);

    /**
     * @returns The choices as OK would take them, without the checks or remembering them.
     */
    [[nodiscard]] PullFetchOptions options() const;

    /**
     * Ported from CPullFetchDlg::OnBnClickedOk: checks the choices, remembers them and closes the dialog.
     */
    void accept() override;

private:
    /**
     * Ported from CPullFetchDlg::Refresh: selects the current branch's upstream, or the remote last used.
     */
    void refresh();

    /**
     * Ported from CPullFetchDlg::OnCbnSelchangeRemote: shows the remote's default for tags and prune.
     */
    void onRemoteChanged();

    /**
     * Ported from CPullFetchDlg::OnBnClickedRd: switches between a named remote and an arbitrary URL.
     */
    void onSourceChanged();

    /**
     * Ported from CPullFetchDlg::OnBnClickedCheckFfonly: No Fast Forward and Fast Forward Only exclude each other.
     */
    void updateFastForwardStates();

    /**
     * @param name A configuration variable.
     * @returns Its value, or an empty string if it is not set.
     */
    [[nodiscard]] QString configValue(const QString& name) const;

    /**
     * @returns The settings group of this repository and mode, TortoiseGit's PullFetch\<dir>_<mode>.
     */
    [[nodiscard]] QString settingsGroup() const;

    Git m_git;
    bool m_isPull;
    QString m_configPullRemote; // m_configPullRemote: branch.<current>.remote
    QString m_configPullBranch; // m_configPullBranch: branch.<current>.merge, stripped
    QRadioButton *m_remoteRadio;
    QRadioButton *m_otherRadio;
    QComboBox *m_remote;
    QComboBox *m_other;
    QComboBox *m_remoteBranch;
    QCheckBox *m_squash;
    QCheckBox *m_noCommit;
    QCheckBox *m_depthCheck;
    QSpinBox *m_depth;
    QCheckBox *m_noFF;
    QCheckBox *m_ffOnly;
    QCheckBox *m_fetchTags;
    QLabel *m_tagOpt;
    QCheckBox *m_prune;
    QLabel *m_pruneDefault;
    QCheckBox *m_rebase;
};

} // namespace kgt
