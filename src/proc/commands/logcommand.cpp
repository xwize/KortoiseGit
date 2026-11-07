// SPDX-FileCopyrightText: 2007-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2016, 2018-2019, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/LogCommand.cpp. The range, limit and revision options come
 * with the log dialog.
 */

#include "commands/logcommand.hpp"

#include "logdlg.hpp"

namespace kgt
{

LogCommand::LogCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool LogCommand::execute()
{
    LogDlg dlg(paths());
    dlg.exec();
    return true;
}

} // namespace kgt
