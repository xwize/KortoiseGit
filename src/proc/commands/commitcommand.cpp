// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2018, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CommitCommand.cpp and CAppUtils::Commit. The log message
 * options (--logmsg, --logmsgfile) and the bug ID come with the later commit dialog cards.
 */

#include "commands/commitcommand.hpp"

#include "commitdlg.hpp"

namespace kgt
{

CommitCommand::CommitCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool CommitCommand::execute()
{
    CommitDlg dlg(paths());
    return dlg.exec() == QDialog::Accepted;
}

} // namespace kgt
