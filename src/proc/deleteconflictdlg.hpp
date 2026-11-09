// SPDX-FileCopyrightText: 2009-2011, 2017, 2019-2020, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/DeleteConflictDlg.h (CDeleteConflictDlg): the Conflict dialog, which
 * CAppUtils::ConflictEdit opens for a delete/modify conflict.
 */

#pragma once

#include <QDialog>
#include <QString>

namespace kgt
{

/**
 * What CDeleteConflictDlg's public members hold, as CAppUtils::ConflictEdit fills them in.
 */
struct DeleteConflictParams {
    QString workingTree; // The working tree's root
    QString file; // The conflicted file, relative to the root (m_File)
    QString fileBaseVersion; // A copy of the common ancestor's version; empty if it has none (m_FileBaseVersion)
    QString localStatus; // What the first side did to the file (m_LocalStatus)
    QString remoteStatus; // What the second side did to the file (m_RemoteStatus)
    QString localHash; // The first side's commit; empty if unknown (m_LocalHash)
    QString remoteHash; // The second side's commit; empty if unknown (m_RemoteHash)
    QString localRef; // The first side's title (m_LocalRef)
    QString remoteRef; // The second side's title (m_RemoteRef)
    bool showModifiedButton = false; // The common ancestor has the file (m_bShowModifiedButton)
    bool diffMine = true; // The first side is the one that still has the file (m_bDiffMine)
};

/**
 * Asks whether to keep or delete a file that one side of a merge deleted and the other kept.
 */
class DeleteConflictDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param params What to show.
     * @param parent Parent widget.
     */
    explicit DeleteConflictDlg(const DeleteConflictParams& params, QWidget *parent = nullptr);

    /**
     * @returns Whether the user chose Delete; meaningful once the dialog has been accepted (m_bIsDelete).
     */
    [[nodiscard]] bool isDelete() const;

private:
    void showLog(const QString& hash) const;
    void showDiff();

    DeleteConflictParams m_params;
    bool m_isDelete = false;
};

} // namespace kgt
