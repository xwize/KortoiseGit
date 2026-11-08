// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/AboutCommand.cpp.
 */

#include "commands/aboutcommand.hpp"

#include "aboutdlg.hpp"

namespace kgt
{

AboutCommand::AboutCommand()
    : Command(PathRequirement::NoPathRequired)
{
}

bool AboutCommand::execute()
{
    AboutDlg dlg;
    dlg.exec();
    return true;
}

} // namespace kgt
