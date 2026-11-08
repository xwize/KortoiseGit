// SPDX-FileCopyrightText: 2008-2009, 2015-2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/TagCommand.cpp.
 */

#include "commands/tagcommand.hpp"

#include "apputils.hpp"
#include "createbranchtagdlg.hpp"

namespace kgt
{

TagCommand::TagCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool TagCommand::execute()
{
    return AppUtils::createBranchTag(repositoryRoot(paths().constFirst()),
                                     CreateBranchTagParams{
                                         .initialRefName = value(QStringLiteral("rev")),
                                         .name = value(QStringLiteral("name")),
                                         .switchNewBranch = false,
                                         .isTag = true,
                                     });
}

} // namespace kgt
