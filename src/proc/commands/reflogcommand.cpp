// SPDX-FileCopyrightText: 2008-2009, 2015-2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RefLogCommand.cpp.
 */

#include "commands/reflogcommand.hpp"

#include "git/git.hpp"
#include "reflogdlg.hpp"

namespace kgt
{

RefLogCommand::RefLogCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool RefLogCommand::execute()
{
    RefLogDlg dlg(Git::repositoryRoot(paths().constFirst()));
    dlg.setCurrentBranch(value(QStringLiteral("ref")));
    dlg.refresh();
    dlg.exec();
    return true;
}

} // namespace kgt
