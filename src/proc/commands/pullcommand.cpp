// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PullCommand.cpp, CAppUtils::Pull and DoPull. The post-command
 * buttons (Pulled Diff, Pulled Log, Resolve, Commit, Stash, Reset, Push, Submodule Update) and the PuTTY key are
 * left out for now.
 */

#include "commands/pullcommand.hpp"

#include "apputils.hpp"
#include "git/gitadmindir.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
#include <QFileInfo>

#include <tuple>

namespace kgt
{

PullCommand::PullCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool PullCommand::execute()
{
    if (paths().size() > 1) {
        bool result = true;
        for (const QString& path : paths()) {
            const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
            if (GitAdminDir::projectTopDir(clean) != clean && !GitAdminDir::isBareRepo(clean)) {
                result = false;
                continue;
            }
            result = pull(clean) && result;
        }
        return result;
    }
    return pull(Git::workingTreeRoot(paths().constFirst()));
}

QStringList PullCommand::pullArguments(const PullFetchOptions& options)
{
    // "git pull --rebase" is never used: a rebase is a fetch followed by RebaseDlg (CAppUtils::Pull)
    QStringList arguments{QStringLiteral("pull"), QStringLiteral("--progress"), QStringLiteral("-v"), QStringLiteral("--no-rebase")};
    if (options.fetchTags == Qt::Unchecked) {
        arguments.append(QStringLiteral("--no-tags"));
    } else if (options.fetchTags == Qt::Checked) {
        arguments.append(QStringLiteral("--tags"));
    }
    if (options.noFF) {
        arguments.append(QStringLiteral("--no-ff"));
    }
    if (options.ffOnly) {
        arguments.append(QStringLiteral("--ff-only"));
    }
    if (options.squash) {
        arguments.append(QStringLiteral("--squash"));
    }
    if (options.noCommit) {
        arguments.append(QStringLiteral("--no-commit"));
    }
    if (options.depth) {
        arguments << QStringLiteral("--depth") << QString::number(*options.depth);
    }
    if (options.prune == Qt::Checked) {
        arguments.append(QStringLiteral("--prune"));
    } else if (options.prune == Qt::Unchecked) {
        arguments.append(QStringLiteral("--no-prune"));
    }
    arguments << QStringLiteral("--") << options.remoteUrl;
    if (!options.remoteBranchName.isEmpty()) {
        arguments.append(options.remoteBranchName);
    }
    return arguments;
}

bool PullCommand::pull(const QString& workingTree)
{
    if (AppUtils::isTGitRebaseActive(workingTree)) {
        return false;
    }

    PullFetchDlg dlg(workingTree, true);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    const PullFetchOptions options = dlg.options();
    // options.rebase would fetch and then open RebaseDlg (DoFetch with runRebase, RebaseAfterFetch); the dialog
    // doesn't offer it yet
    Q_ASSERT(!options.rebase);

    const Git git(workingTree);
    try {
        std::ignore = git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("HEAD")}, RunMode::ReadOnly);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(nullptr, i18n("Could not get HEAD hash.") + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
        return false;
    }

    ProgressDlg progress(workingTree);
    progress.addGitCmd(pullArguments(options));
    // DoPull's m_PostCmdCallback: a failed pull may have left conflicts to resolve
    QObject::connect(&progress, &ProgressDlg::commandsFinished, &progress, [&progress, &git] {
        if (progress.gitStatus() == 0) {
            return;
        }
        try {
            if (git.hasWorkingTreeConflicts()) {
                AppUtils::showMergeConflictsHint(&progress);
            }
        } catch (const RuntimeError& e) {
            KMessageBox::error(&progress, i18n("Checking for conflicts failed.") + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
        }
    });
    progress.exec();
    return progress.gitStatus() == 0;
}

} // namespace kgt
