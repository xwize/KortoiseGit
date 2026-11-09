// SPDX-FileCopyrightText: 2003-2007 TortoiseSVN
// SPDX-FileCopyrightText: 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ResolveDlg.h (CResolveDlg) and its IDD_RESOLVE dialog resource.
 */

#pragma once

#include "gitstatuslistmodel.hpp"

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QPushButton;

namespace kgt
{

class GitStatusListCtrl;

/**
 * The Resolve dialog: lists the conflicted files below the paths it was opened for, all checked. Pressing OK leaves
 * the checked ones in pathList(), for the caller to resolve.
 */
class ResolveDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Starts loading the list straight away.
     * @param workingTree The top of the working tree, as git rev-parse --show-toplevel reports it.
     * @param paths The absolute paths the dialog was opened for, inside @p workingTree.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    ResolveDlg(const QString& workingTree, const QStringList& paths, QWidget *parent = nullptr);
    ~ResolveDlg() override;

    ResolveDlg(const ResolveDlg&) = delete;
    ResolveDlg& operator=(const ResolveDlg&) = delete;
    ResolveDlg(ResolveDlg&&) = delete;
    ResolveDlg& operator=(ResolveDlg&&) = delete;

    /**
     * @returns The files checked when OK was pressed (m_pathList).
     */
    [[nodiscard]] const QList<GitStatusListEntry>& pathList() const;

    /**
     * @returns The file list, for tests.
     */
    [[nodiscard]] GitStatusListCtrl *listCtrl() const;

    /**
     * Keeps the checked files and closes the dialog (CResolveDlg::OnOK). Does nothing while the list is loading.
     */
    void accept() override;

    /**
     * Saves the window size and closes the dialog, unless one of the list's own actions is changing the repository.
     * @param result The dialog's result.
     */
    void done(int result) override;

private:
    void refresh();
    void updateOkButton();

    GitStatusListCtrl *m_resolveListCtrl;
    QCheckBox *m_selectAll;
    QPushButton *m_okButton = nullptr; // The button box's, set in the constructor
    QList<GitStatusListEntry> m_pathList;
};

} // namespace kgt
