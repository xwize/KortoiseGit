// SPDX-FileCopyrightText: 2007-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2016, 2018-2019, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/LogCommand.cpp. Takes --range, --startrev, --endrev and --rev;
 * the limit and find options come with the log dialog.
 */

#include "commands/logcommand.hpp"

#include "git/git.hpp"
#include "logdlg.hpp"

namespace kgt
{

LogCommand::LogCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool LogCommand::execute()
{
    // --startrev and --endrev make a range; --range wins over them
    const Git git(Git::repositoryRoot(paths().constFirst()));
    QString range;
    if (const QString revStart = value(QStringLiteral("startrev")); !revStart.isEmpty()) {
        range = git.fixBranchName(revStart) + QStringLiteral("..");
    }
    if (const QString revEnd = value(QStringLiteral("endrev")); !revEnd.isEmpty()) {
        range += git.fixBranchName(revEnd);
    }
    if (const QString givenRange = value(QStringLiteral("range")); !givenRange.isEmpty()) {
        range = givenRange;
    }
    LogDlg dlg(paths(), range);
    if (const QString rev = value(QStringLiteral("rev")); !rev.isEmpty()) {
        dlg.setHighlightRevision(rev);
    }
    dlg.exec();
    return true;
}

} // namespace kgt
