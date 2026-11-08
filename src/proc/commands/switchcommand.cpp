// SPDX-FileCopyrightText: 2008-2013, 2015-2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/SwitchCommand.cpp.
 */

#include "commands/switchcommand.hpp"

#include "apputils.hpp"
#include "git/git.hpp"

namespace kgt
{

SwitchCommand::SwitchCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool SwitchCommand::execute()
{
    return AppUtils::switchTo(Git::workingTreeRoot(paths().constFirst()), value(QStringLiteral("rev")));
}

} // namespace kgt
