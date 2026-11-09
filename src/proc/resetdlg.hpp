// SPDX-FileCopyrightText: 2008-2014, 2019, 2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ResetDlg.h (CResetDlg): the Reset dialog.
 */

#pragma once

#include "mergeabortdlg.hpp"

#include <QDialog>
#include <QString>

class QRadioButton;

namespace kgt
{

class ChooseVersion;
class Git;

/**
 * Asks what to reset the active branch to and how: a ChooseVersion and a soft, mixed or hard reset.
 */
class ResetDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog with a mixed reset selected, or a soft one in a bare repository, and starts loading the
     * branches and tags.
     * @param git Runs git in the working tree's root or the bare repository.
     * @param initialRefName What to select once loaded (m_initialRefName): a branch, tag or commit.
     * @param parent Parent widget.
     */
    ResetDlg(const Git& git, const QString& initialRefName, QWidget *parent = nullptr);

    /**
     * @returns The chosen reset: Soft, Mixed or Hard.
     */
    [[nodiscard]] ResetType resetType() const;

    /**
     * @returns What to reset to (m_ResetToVersion). Valid after the dialog was accepted.
     */
    [[nodiscard]] const QString& resetToVersion() const;

    /**
     * @returns The version chooser.
     */
    [[nodiscard]] ChooseVersion *chooseVersion() const;

    /**
     * Ported from CResetDlg::OnOK: collects the chosen version.
     */
    void accept() override;

    /**
     * Saves the window size, whichever way the dialog closes (EnableSaveRestore).
     * @param result The dialog's result.
     */
    void done(int result) override;

private:
    ChooseVersion *m_chooseVersion;
    QRadioButton *m_soft;
    QRadioButton *m_mixed;
    QRadioButton *m_hard;
    QString m_resetToVersion; // m_ResetToVersion: what to reset to
};

} // namespace kgt
