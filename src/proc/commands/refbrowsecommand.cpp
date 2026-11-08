// SPDX-FileCopyrightText: 2009, 2012-2013, 2016, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/Commands/RefBrowseCommand.h

#include "commands/refbrowsecommand.hpp"

#include "browserefsdlg.hpp"

#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <QFileInfo>

namespace
{

/**
 * @param path The path given on the command line.
 * @returns The working tree root holding it, or, for a bare repository, which has none, the path itself.
 */
QString repositoryRoot(const QString& path)
{
    try {
        return kgt::Git::workingTreeRoot(path);
    } catch (const kgt::RuntimeError& e) {
        if (e.error().category != kgt::ErrorCategory::Git) {
            throw;
        }
        const QFileInfo info(path);
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
}

} // namespace

namespace kgt
{

RefBrowseCommand::RefBrowseCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool RefBrowseCommand::execute()
{
    BrowseRefsDlg dlg(repositoryRoot(paths().constFirst()));
    dlg.exec();
    return true;
}

} // namespace kgt
