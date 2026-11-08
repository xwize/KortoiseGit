// SPDX-FileCopyrightText: 2009, 2012-2013, 2016, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/Commands/RefBrowseCommand.h

#include "commands/refbrowsecommand.hpp"

#include "browserefsdlg.hpp"
#include "git/git.hpp"

namespace kgt
{

RefBrowseCommand::RefBrowseCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool RefBrowseCommand::execute()
{
    BrowseRefsDlg dlg(Git::repositoryRoot(paths().constFirst()));
    dlg.exec();
    return true;
}

} // namespace kgt
