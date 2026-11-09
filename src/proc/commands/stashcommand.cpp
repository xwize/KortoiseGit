// SPDX-FileCopyrightText: 2008-2012, 2014-2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/StashCommand.cpp.
 */

#include "commands/stashcommand.hpp"

#include "apputils.hpp"
#include "git/git.hpp"

namespace kgt
{

StashSaveCommand::StashSaveCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool StashSaveCommand::execute()
{
    return AppUtils::stashSave(Git(Git::workingTreeRoot(paths().constFirst())), value(QStringLiteral("msg")));
}

StashApplyCommand::StashApplyCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool StashApplyCommand::execute()
{
    return AppUtils::stashApply(Git(Git::workingTreeRoot(paths().constFirst())));
}

StashPopCommand::StashPopCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool StashPopCommand::execute()
{
    // TortoiseGit returns !StashPop() here, which reports a successful pop as a failed command
    return AppUtils::stashPop(Git::workingTreeRoot(paths().constFirst()));
}

} // namespace kgt
