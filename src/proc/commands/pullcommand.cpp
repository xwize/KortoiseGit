// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PullCommand.cpp, CAppUtils::Pull and DoPull. The PuTTY key,
 * the Submodule Update follow-up and the "see changes" question after a conflict (repostatus isn't ported) are left
 * out for now.
 */

#include "commands/pullcommand.hpp"

#include "apputils.hpp"
#include "commands/fetchcommand.hpp"
#include "git/gitadmindir.hpp"
#include "gitdiff.hpp"
#include "mergeabortdlg.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
#include <QFileInfo>
#include <QIcon>

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
    if (options.unrelated) {
        arguments.append(QStringLiteral("--allow-unrelated-histories"));
    }
    arguments << QStringLiteral("--") << options.remoteUrl;
    if (!options.remoteBranchName.isEmpty()) {
        arguments.append(options.remoteBranchName);
    }
    return arguments;
}

bool PullCommand::pull(const QString& workingTree, const PullFollowUps& followUps, QWidget *parent)
{
    if (AppUtils::isTGitRebaseActive(workingTree, parent)) {
        return false;
    }

    PullFetchDlg dlg(workingTree, true, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    const PullFetchOptions options = dlg.options();
    // "git pull --rebase" is not supported, never and ever. So, adapting it to Fetch & Rebase.
    if (options.rebase) {
        return FetchCommand::doFetch(workingTree, options, options.rebaseActivatedInConfigForPull ? FetchRebase::AutoStart : FetchRebase::Open);
    }
    return doPull(workingTree, options, followUps, parent);
}

bool PullCommand::doPull(const QString& workingTree, const PullFetchOptions& options, const PullFollowUps& followUps, QWidget *parent)
{
    const Git git(workingTree);
    const QString hashOld = git.hash(QStringLiteral("HEAD"));
    if (hashOld.isEmpty()) {
        KMessageBox::error(parent, i18n("Could not get HEAD hash."), QString(), KMessageBox::Options());
        return false;
    }

    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(pullArguments(options));
    // DoPull's m_PostCmdCallback
    progress.setPostCmdCallback([&](int status, PostCmdList& postCmdList) {
        if (status != 0) {
            addFailurePostCmds(git, options, followUps, &progress, postCmdList);
            return;
        }

        if (followUps.showStashPop) {
            postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-stash-pop")), i18n("Stash Pop"), [&workingTree, parent] {
                                    std::ignore = AppUtils::stashPop(workingTree, parent);
                                }});
        }

        const QString hashNew = git.hash(QStringLiteral("HEAD"));
        if (hashNew.isEmpty()) {
            KMessageBox::error(&progress, i18n("Could not get HEAD hash after pulling."), QString(), KMessageBox::Options());
        } else {
            // /command:showcompare opens CFileDiffDlg, which isn't ported: git difftool --dir-diff shows the same
            postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Pulled Diff"), [&workingTree, hashOld, hashNew, parent] {
                                    std::ignore =
                                        GitDiff::start(parent, {.workingDirectory = workingTree, .arguments = GitDiff::dirArguments({}, hashNew, hashOld)});
                                }});
            postCmdList.append(
                {QIcon::fromTheme(QStringLiteral("view-history")), i18n("Pulled Log"), [&workingTree, hashOld, hashNew] {
                     const QString range = hashOld + QStringLiteral("..") + hashNew;
                     std::ignore = AppUtils::runTortoiseGitProc(
                         {QStringLiteral("--command"), QStringLiteral("log"), QStringLiteral("--path"), workingTree, QStringLiteral("--range"), range});
                 }});
        }

        if (followUps.showPush) {
            postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-push")), i18n("Pus&h..."), [&workingTree, parent] {
                                    std::ignore = AppUtils::push(workingTree, QString(), parent);
                                }});
        }
    });
    return progress.exec() == QDialog::Accepted && progress.gitStatus() == 0;
}

void PullCommand::addFailurePostCmds(const Git& git,
                                     const PullFetchOptions& options,
                                     const PullFollowUps& followUps,
                                     QWidget *progress,
                                     PostCmdList& postCmdList)
{
    // The failure half of DoPull's m_PostCmdCallback
    const QString workingTree = git.workingDirectory();
    QWidget *parent = progress->parentWidget();
    try {
        if (git.hasWorkingTreeConflicts()) {
            AppUtils::showMergeConflictsHint(progress);
            AppUtils::addConflictPostCmds(workingTree, postCmdList);
            return;
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(progress, i18n("Checking for conflicts failed.") + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
    }

    // A pull from a configured remote that has nothing in common with HEAD can be retried as unrelated histories
    const QStringList remotes = QString::fromUtf8(git.run({QStringLiteral("remote")}, RunMode::ReadOnly)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (remotes.contains(options.remoteUrl)) {
        QString currentBranch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(workingTree));
        if (currentBranch == QLatin1String("HEAD")) {
            currentBranch.clear();
        }
        QString remoteRef = QStringLiteral("remotes/") + options.remoteUrl + QLatin1Char('/') + options.remoteBranchName;
        if (!currentBranch.isEmpty() && options.remoteBranchName.isEmpty()) {
            // CGit::GetRemoteTrackedBranch
            const QString pullRemote = git.configValue(QStringLiteral("branch.%1.remote").arg(currentBranch));
            const QString pullBranch = Git::stripRefName(git.configValue(QStringLiteral("branch.%1.merge").arg(currentBranch)));
            if (!pullRemote.isEmpty() && !pullBranch.isEmpty()) {
                remoteRef = QStringLiteral("remotes/") + pullRemote + QLatin1Char('/') + pullBranch;
            }
        }
        if (git.commonAncestor(QStringLiteral("HEAD"), remoteRef).isEmpty()) {
            postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-merge")), i18n("&Merge unrelated history"), [workingTree, options, followUps, parent] {
                                    PullFetchOptions unrelated = options;
                                    unrelated.unrelated = true;
                                    std::ignore = doPull(workingTree, unrelated, followUps, parent);
                                }});
        }
    }

    postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-pull")), i18n("&Pull..."), [workingTree, parent] {
                            std::ignore = pull(workingTree, {}, parent);
                        }});
    postCmdList.append(
        {QIcon::fromTheme(QStringLiteral("vcs-stash")), i18n("Stash changes"), [workingTree, parent] {
             std::ignore =
                 AppUtils::stashSave(Git(workingTree), QString(), parent, {.showPull = true, .pullShowPush = false, .showMerge = false, .mergeRev = {}});
         }});
    postCmdList.append({QIcon::fromTheme(QStringLiteral("edit-reset")), i18n("Reset"), [workingTree, parent] {
                            // GetRemoteTrackedBranchForHEAD
                            const Git repo(workingTree);
                            const QString current = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(workingTree));
                            const QString pullRemote = repo.configValue(QStringLiteral("branch.%1.remote").arg(current));
                            const QString pullBranch = Git::stripRefName(repo.configValue(QStringLiteral("branch.%1.merge").arg(current)));
                            QString defaultUpstream;
                            if (!pullRemote.isEmpty() && !pullBranch.isEmpty()) {
                                defaultUpstream = QStringLiteral("remotes/%1/%2").arg(pullRemote, pullBranch);
                            }
                            std::ignore = AppUtils::gitReset(workingTree, defaultUpstream, parent, ResetType::Hard);
                        }});
}

} // namespace kgt
