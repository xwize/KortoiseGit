// SPDX-FileCopyrightText: 2008-2017, 2019-2021, 2024-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CreateBranchTagDlg.h (CCreateBranchTagDlg): the Create Branch and Create
 * Tag dialog.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;

namespace kgt
{

class ChooseVersion;

/**
 * What the dialog starts with: the parameters of CAppUtils::CreateBranchTag.
 */
struct CreateBranchTagParams {
    QString initialRefName; // m_initialRefName: what to base it on; empty for HEAD
    QString name; // m_BranchTagName: the new branch's or tag's name
    bool switchNewBranch = false; // m_bSwitch: tick "Switch to new branch" whatever was remembered
    bool isTag = false; // m_bIsTag: create a tag rather than a branch
};

/**
 * What the dialog asks for: CCreateBranchTagDlg's members that CAppUtils::CreateBranchTag reads.
 */
struct CreateBranchTagOptions {
    QString branchTagName; // m_BranchTagName: the name, trimmed
    QString versionName; // m_VersionName: what to base it on
    bool force = false; // m_bForce: -f
    Qt::CheckState track = Qt::PartiallyChecked; // m_bTrack: --track, --no-track, or neither
    bool switchToNewBranch = false; // m_bSwitch: switch to it afterwards
    QString message; // m_Message: the branch's description or the tag's message
    bool sign = false; // m_bSign: -s, tags only
    bool push = false; // m_bPush: open the push dialog afterwards, tags only
};

/**
 * The Create Branch and Create Tag dialog: a name, a ChooseVersion for what to base it on, the options of git branch
 * or git tag, and a description or tag message.
 */
class CreateBranchTagDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the branches and tags.
     * @param git Runs git in the working tree's root or the bare repository.
     * @param params What to start with.
     * @param parent Parent widget.
     */
    explicit CreateBranchTagDlg(Git git, const CreateBranchTagParams& params = {}, QWidget *parent = nullptr);

    /**
     * @returns What the user chose. Valid after the dialog was accepted.
     */
    [[nodiscard]] const CreateBranchTagOptions& options() const;

    /**
     * @returns The version chooser.
     */
    [[nodiscard]] ChooseVersion *chooseVersion() const;

    /**
     * Ported from CCreateBranchTagDlg::OnBnClickedOk: checks the message and name, remembers "Switch to new branch"
     * or "Push" and collects the options.
     */
    void accept() override;

private:
    void onVersionChanged();
    void onBranchSelectionChanged();
    void onNameChanged();
    [[nodiscard]] bool isNameAccepted(const QString& name);

    Git m_git;
    CreateBranchTagOptions m_options;
    QLineEdit *m_name;
    ChooseVersion *m_chooseVersion;
    QCheckBox *m_track;
    QCheckBox *m_force;
    QCheckBox *m_switch;
    QCheckBox *m_sign;
    QCheckBox *m_push;
    QPlainTextEdit *m_message;
    QDialogButtonBox *m_buttons;
    QString m_oldSelectBranch; // m_OldSelectBranch: the branch selected before the current one
    bool m_isTag; // m_bIsTag: creates a tag rather than a branch
};

} // namespace kgt
