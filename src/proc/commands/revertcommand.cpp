// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2014, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RevertCommand.cpp. TortoiseGit reverts in its progress dialog
 * (CGitProgressDlg with RevertProgressCommand), which is not ported: the Revert dialog reverts and shows only an
 * error.
 */

#include "commands/revertcommand.hpp"

#include "git/git.hpp"
#include "revertdlg.hpp"

namespace kgt
{

RevertCommand::RevertCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool RevertCommand::execute()
{
    RevertDlg dlg(Git::workingTreeRoot(paths().constFirst()), paths());
    return dlg.exec() == QDialog::Accepted;
}

} // namespace kgt
