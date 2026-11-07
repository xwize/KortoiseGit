// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RevertDlg.h (CRevertDlg) and its IDD_REVERT dialog resource.
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <memory>

class QCheckBox;
class QPushButton;

namespace kgt
{

class GitStatusListCtrl;
class RevertProgressCommand;

/**
 * The Revert dialog: lists the changes below the paths it was opened for. The files the user picked directly and
 * the added files start checked; the files found inside a picked folder don't, as in TortoiseGit. OK reverts the
 * checked files. TortoiseGit closes the dialog and reverts in its progress dialog, which is not ported: here the
 * dialog stays open while git runs, shows git's error if it fails, and closes once the files are reverted.
 *
 * TortoiseGit's "Note: the folder contains unversioned items" appears only with its UnversionedAsModified
 * setting, which is not ported, so it is left out.
 */
class RevertDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Starts loading the list straight away.
     * @param workingTree The top of the working tree, as git rev-parse --show-toplevel reports it.
     * @param paths The absolute paths the dialog was opened for, inside @p workingTree.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    RevertDlg(const QString& workingTree, const QStringList& paths, QWidget *parent = nullptr);
    ~RevertDlg() override;

    RevertDlg(const RevertDlg&) = delete;
    RevertDlg& operator=(const RevertDlg&) = delete;
    RevertDlg(RevertDlg&&) = delete;
    RevertDlg& operator=(RevertDlg&&) = delete;

    /**
     * @returns The file list, for tests.
     */
    [[nodiscard]] GitStatusListCtrl *listCtrl() const;

    /**
     * @returns Whether the revert is running.
     */
    [[nodiscard]] bool isReverting() const;

    /**
     * Reverts the checked files (CRevertDlg::OnOK) and closes the dialog once that succeeds. Does nothing while
     * the list is loading or the revert runs.
     */
    void accept() override;

    /**
     * Closes the dialog, unless the revert is running (CRevertDlg::OnCancel).
     */
    void reject() override;

Q_SIGNALS:
    /**
     * The revert failed; the dialog has shown the error and stays open.
     * @param message git's error.
     */
    void revertFailed(const QString& message);

private:
    void updateOkButton();
    void onRevertFinished();

    GitStatusListCtrl *m_revertList;
    QCheckBox *m_selectAll;
    QPushButton *m_okButton = nullptr; // The button box's, set in the constructor
    std::unique_ptr<RevertProgressCommand> m_revertCommand;
};

} // namespace kgt
