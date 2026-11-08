// SPDX-FileCopyrightText: 2009-2012, 2015-2016, 2018-2019, 2021, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RebaseCommand.cpp. TortoiseGit's post-rebase buttons ("Show
 * log", "Restart rebase") are not ported yet: Done just closes the dialog.
 */

#include "commands/rebasecommand.hpp"

#include "git/git.hpp"
#include "rebasedlg.hpp"

namespace kgt
{

RebaseCommand::RebaseCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool RebaseCommand::execute()
{
    RebaseDlg dlg(Git::workingTreeRoot(paths().constFirst()), {.upstream = value(QStringLiteral("upstream")), .force = hasKey(QStringLiteral("force"))});
    return dlg.exec() == QDialog::Accepted;
}

} // namespace kgt
