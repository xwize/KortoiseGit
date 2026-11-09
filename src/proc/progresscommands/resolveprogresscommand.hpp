// SPDX-FileCopyrightText: 2014, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressCommands/ResolveProgressCommand.h: the ResolveWith::Current branch,
 * the only one the Resolve dialog uses. TortoiseGit's CGitProgressDlg is not ported, so this runs in ProgressDlg; its
 * post-command buttons aren't either.
 */

#pragma once

#include "gitstatuslistmodel.hpp"

#include <QList>
#include <QString>

namespace kgt
{

class ProgressDlg;

/**
 * Marks files as resolved with git add -f. Files that aren't unmerged are left alone and reported as skipped.
 */
class ResolveProgressCommand
{
public:
    /**
     * @param entries The files to resolve (m_targetPathList).
     */
    void setPathList(const QList<GitStatusListEntry>& entries);

    /**
     * Gives the dialog its title, git command and report (ResolveProgressCommand::Run). They run when it is shown.
     * @param workingTree The top of the working tree, where @p dialog runs git.
     * @param dialog The progress dialog; nothing may have been added to it.
     */
    void setUp(const QString& workingTree, ProgressDlg& dialog) const;

private:
    QList<GitStatusListEntry> m_targetPathList;
};

} // namespace kgt
