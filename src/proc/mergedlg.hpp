// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/MergeDlg.h (CMergeDlg): the Merge dialog.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>

#include <optional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace kgt
{

class ChooseVersion;

/**
 * What the Merge dialog asks for: the arguments of TortoiseGit's DoMerge.
 */
struct MergeOptions {
    QString version; // m_VersionName: the branch, tag or commit to merge
    bool isBranch = false; // m_bIsBranch: the version was chosen as a branch
    bool noFF = false; // m_bNoFF: --no-ff
    bool ffOnly = false; // m_bFFonly: --ff-only (ignored with noFF)
    bool squash = false; // m_bSquash: --squash
    bool noCommit = false; // m_bNoCommit: --no-commit
    std::optional<int> log; // m_bLog and m_nLog: --log=<n>
    bool unrelated = false; // --allow-unrelated-histories, only for the post-command retry
    QString strategy; // m_MergeStrategy: --strategy, empty for git's default
    QString strategyOption; // m_StrategyOption: --strategy-option, used with "recursive" only
    QString strategyParam; // m_StrategyParam: "=<param>" after a rename-threshold or subtree option
    QString message; // m_strLogMesage: the merge commit's message, empty for git's own
};

/**
 * The Merge dialog: a ChooseVersion for what to merge, the merge options and the merge message.
 */
class MergeDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the branches and tags.
     * @param git Runs git in the working tree's root.
     * @param initialRefName What to select first (m_initialRefName); empty for the current branch's push branch.
     * @param parent Parent widget.
     */
    explicit MergeDlg(Git git, const QString& initialRefName = {}, QWidget *parent = nullptr);

    /**
     * @returns What the user chose. Valid after the dialog was accepted.
     */
    [[nodiscard]] const MergeOptions& options() const;

    /**
     * @returns The version chooser.
     */
    [[nodiscard]] ChooseVersion *chooseVersion() const;

    /**
     * Ported from CMergeDlg::OnBnClickedOk: checks the choice and collects the options.
     */
    void accept() override;

private:
    void updateStates();
    [[nodiscard]] QString defaultInitialRefName() const;

    Git m_git;
    MergeOptions m_options;
    QLineEdit *m_currentBranch;
    ChooseVersion *m_chooseVersion;
    QCheckBox *m_squash;
    QCheckBox *m_log;
    QSpinBox *m_logNum;
    QCheckBox *m_noFF;
    QCheckBox *m_ffOnly;
    QCheckBox *m_noCommit;
    QComboBox *m_strategy;
    QComboBox *m_strategyOption;
    QLineEdit *m_strategyParam;
    QPlainTextEdit *m_message;
    QDialogButtonBox *m_buttons;
};

} // namespace kgt
