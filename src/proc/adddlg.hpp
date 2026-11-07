// SPDX-FileCopyrightText: 2003-2007 TortoiseSVN
// SPDX-FileCopyrightText: 2011, 2013, 2016, 2017, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/AddDlg.h (CAddDlg) and its IDD_ADD dialog resource.
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

class GitCall;
class GitStatusListCtrl;

/**
 * The Add dialog: lists the unversioned files below the paths it was opened for, all checked, and adds the checked
 * ones to the index with git add -f when the user presses OK. "Include ignored files" lists ignored files too,
 * unchecked. TortoiseGit closes the dialog and adds in its progress dialog, which is not ported: here the dialog
 * stays open while git runs, shows git's error if it fails, and closes once the files are added.
 */
class AddDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Starts loading the list straight away.
     * @param workingTree The top of the working tree, as git rev-parse --show-toplevel reports it.
     * @param paths The absolute paths the dialog was opened for, inside @p workingTree.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    AddDlg(const QString& workingTree, const QStringList& paths, QWidget *parent = nullptr);
    ~AddDlg() override;

    AddDlg(const AddDlg&) = delete;
    AddDlg& operator=(const AddDlg&) = delete;
    AddDlg(AddDlg&&) = delete;
    AddDlg& operator=(AddDlg&&) = delete;

    /**
     * @param workingTree The top of the working tree, with symbolic links resolved.
     * @param paths Absolute paths inside it, possibly reached through symbolic links.
     * @returns The paths relative to @p workingTree, "." for the working tree itself.
     */
    [[nodiscard]] static QStringList relativePaths(const QString& workingTree, const QStringList& paths);

    /**
     * @param paths Absolute paths.
     * @returns Their deepest common folder, or the path itself if there is one (CTGitPathList::GetCommonRoot).
     */
    [[nodiscard]] static QString commonRoot(const QStringList& paths);

    /**
     * @returns The file list, for tests.
     */
    [[nodiscard]] GitStatusListCtrl *listCtrl() const;

    /**
     * @returns Whether git add is running.
     */
    [[nodiscard]] bool isAdding() const;

    /**
     * Adds the checked files (CAddDlg::OnOK): starts git add and closes the dialog once it succeeds. Does nothing
     * while the list is loading or git add runs.
     */
    void accept() override;

    /**
     * Closes the dialog, unless git add is running (CAddDlg::OnCancel).
     */
    void reject() override;

Q_SIGNALS:
    /**
     * git add failed; the dialog has shown the error and stays open.
     * @param message git's error.
     */
    void addFailed(const QString& message);

private:
    void refresh();
    void updateOkButton();
    void onAddFinished();

    GitStatusListCtrl *m_addListCtrl;
    QCheckBox *m_selectAll;
    QCheckBox *m_includeIgnored;
    QPushButton *m_okButton = nullptr; // The button box's, set in the constructor
    std::unique_ptr<GitCall> m_addCall;
};

} // namespace kgt
