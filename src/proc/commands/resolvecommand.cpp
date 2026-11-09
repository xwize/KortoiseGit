// SPDX-FileCopyrightText: 2007-2008, 2012 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2016, 2018-2019, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/ResolveCommand.cpp. The --noquestion and --silent switches are
 * left out: only TortoiseMerge, which is not ported, passes them.
 */

#include "commands/resolvecommand.hpp"

#include "git/git.hpp"
#include "progresscommands/resolveprogresscommand.hpp"
#include "progressdlg.hpp"
#include "resolvedlg.hpp"

namespace kgt
{

ResolveCommand::ResolveCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool ResolveCommand::execute()
{
    const QString workingTree = Git::workingTreeRoot(paths().constFirst());
    ResolveDlg dlg(workingTree, paths());
    if (dlg.exec() != QDialog::Accepted || dlg.pathList().isEmpty()) {
        return false;
    }
    ProgressDlg progress(workingTree);
    ResolveProgressCommand command;
    command.setPathList(dlg.pathList());
    command.setUp(workingTree, progress);
    progress.exec();
    // !CGitProgressDlg::DidErrorsOccur
    return progress.gitStatus() == 0;
}

} // namespace kgt
