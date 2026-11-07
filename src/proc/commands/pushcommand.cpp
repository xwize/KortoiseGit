// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PushCommand.cpp, CAppUtils::Push and CAppUtils::DoPush. The
 * --branch option, several paths at once, hooks and the post-push buttons are left out for now.
 */

#include "commands/pushcommand.hpp"

#include "git/git.hpp"
#include "progressdlg.hpp"
#include "pushdlg.hpp"

namespace kgt
{

PushCommand::PushCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool PushCommand::execute()
{
    const QString workingTree = Git::workingTreeRoot(paths().constFirst());
    PushDlg dlg(workingTree);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    ProgressDlg progress(workingTree);
    progress.addGitCmd(pushArguments(dlg.options()));
    progress.exec();
    return progress.gitStatus() == 0;
}

QStringList PushCommand::pushArguments(const PushOptions& options)
{
    QStringList arguments{QStringLiteral("push")};
    if (options.tags) {
        arguments.append(QStringLiteral("--tags"));
    }
    if (options.forceWithLease) {
        arguments.append(QStringLiteral("--force-with-lease"));
    }
    if (options.setUpstream) {
        arguments.append(QStringLiteral("--set-upstream"));
    }
    arguments << QStringLiteral("--progress") << QStringLiteral("--") << options.remote;
    QString refspec = options.localBranch;
    if (!options.remoteBranch.isEmpty()) {
        refspec += QLatin1Char(':') + options.remoteBranch;
    }
    if (!refspec.isEmpty()) {
        arguments.append(refspec);
    }
    return arguments;
}

} // namespace kgt
