// SPDX-FileCopyrightText: 2011, 2013, 2023 Sven Strickroth <email@cs-ware.de>
// SPDX-FileCopyrightText: 2003-2008 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/StashSave.h (CStashSaveDlg): the Stash dialog.
 */

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;

namespace kgt
{

/**
 * What the Stash dialog asks for, the values CAppUtils::StashSave reads from CStashSaveDlg.
 */
struct StashSaveOptions {
    QString message; // Empty for git's own message
    bool includeUntracked = false; // --include-untracked
    bool all = false; // --all
};

/**
 * Asks for a stash's message and whether it takes untracked or all files.
 */
class StashSaveDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param workingTree The working tree's root, for the title.
     * @param parent Parent widget.
     */
    explicit StashSaveDlg(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * @param message The message the dialog starts with, CStashSaveDlg::m_sMessage.
     */
    void setMessage(const QString& message);

    /**
     * @returns What the user chose.
     */
    [[nodiscard]] StashSaveOptions options() const;

    /**
     * Ported from CStashSaveDlg::OnBnClickedOk: closes the dialog, unless the user aborts at the include untracked
     * warning.
     */
    void accept() override;

private:
    /**
     * Ported from the CMessageBox::ShowCheck call in CStashSaveDlg::OnBnClickedOk.
     * @returns True if the user chose Continue now, or chose it earlier and asked not to be warned again.
     */
    [[nodiscard]] bool confirmIncludeUntracked();

    QLineEdit *m_message;
    QCheckBox *m_includeUntracked;
    QCheckBox *m_all;
};

} // namespace kgt
