// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/FetchCommand.cpp, CAppUtils::Fetch and DoFetch. The
 * post-command buttons (Retry, Show Log, Reset, Fetch, Rebase, Switch), the /remote: preselection and the PuTTY key
 * are left out for now; "Launch Rebase After Fetch" waits for RebaseDlg (KORTOISEGIT-0018).
 */

#include "commands/fetchcommand.hpp"

#include "git/gitadmindir.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"

#include <QDir>
#include <QFileInfo>

namespace kgt
{

FetchCommand::FetchCommand()
    : Command(PathRequirement::WorkingTreeOrBareRepoRequired)
{
}

bool FetchCommand::execute()
{
    if (paths().size() > 1) {
        bool result = true;
        for (const QString& path : paths()) {
            const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
            if (GitAdminDir::projectTopDir(clean) != clean && !GitAdminDir::isBareRepo(clean)) {
                result = false;
                continue;
            }
            result = fetch(clean) && result;
        }
        return result;
    }
    return fetch(repositoryRoot(paths().constFirst()));
}

QStringList FetchCommand::fetchArguments(const PullFetchOptions& options)
{
    QStringList arguments{QStringLiteral("fetch")};
    if (options.allRemotes) {
        arguments.append(QStringLiteral("--all"));
    }
    // TortoiseGit's FetchVerbose setting, on by default
    arguments << QStringLiteral("--progress") << QStringLiteral("-v");
    if (options.depth) {
        arguments << QStringLiteral("--depth") << QString::number(*options.depth);
    }
    if (options.prune == Qt::Checked) {
        arguments.append(QStringLiteral("--prune"));
    } else if (options.prune == Qt::Unchecked) {
        arguments.append(QStringLiteral("--no-prune"));
    }
    if (options.fetchTags == Qt::Checked) {
        arguments.append(QStringLiteral("--tags"));
    } else if (options.fetchTags == Qt::Unchecked) {
        arguments.append(QStringLiteral("--no-tags"));
    }
    if (!options.allRemotes) {
        arguments << QStringLiteral("--") << options.remoteUrl;
        if (!options.remoteBranchName.isEmpty()) {
            arguments.append(options.remoteBranchName);
        }
    }
    return arguments;
}

bool FetchCommand::fetch(const QString& repository)
{
    PullFetchDlg dlg(repository, false);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    ProgressDlg progress(repository);
    progress.addGitCmd(fetchArguments(dlg.options()));
    progress.exec();
    return progress.gitStatus() == 0;
}

} // namespace kgt
