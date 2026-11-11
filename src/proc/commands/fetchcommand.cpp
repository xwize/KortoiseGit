// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/FetchCommand.cpp, CAppUtils::Fetch and DoFetch. The
 * post-command buttons (Retry, Show Log, Reset, Fetch, Rebase, Switch) and the PuTTY key are left out for now.
 */

#include "commands/fetchcommand.hpp"

#include "apputils.hpp"
#include "git/gitadmindir.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"
#include "rebasedlg.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
#include <QFileInfo>

namespace
{

using namespace kgt;

/**
 * Asks whether to open the rebase dialog although there seems to be nothing to rebase; No is the default.
 * @param parent Parent of the question.
 * @param text The question.
 * @param dontAskAgainName TortoiseGit's name of the "Don't show this message again" setting.
 * @returns Whether to open it.
 */
bool askOpenRebase(QWidget *parent, const QString& text, const QString& dontAskAgainName)
{
    return KMessageBox::questionTwoActions(parent, text, QString(), KGuiItem(i18n("&Yes")), KGuiItem(i18n("&No")), dontAskAgainName, KMessageBox::Dangerous)
        == KMessageBox::PrimaryAction;
}

/**
 * DoFetch's "git merge --ff-only" of the fetched branch, in a ProgressDlg that closes itself if git succeeds.
 * @param git Runs git in the working tree's root.
 * @param upstream The fetched branch.
 * @param parent Parent of the progress dialog.
 */
void mergeFastForward(const Git& git, const QString& upstream, QWidget *parent)
{
    ProgressDlg progress(git.workingDirectory(), parent);
    progress.addGitCmd({QStringLiteral("merge"), QStringLiteral("--ff-only"), QStringLiteral("--"), git.fixBranchName(upstream)});
    // AUTOCLOSE_IF_NO_ERRORS
    QObject::connect(
        &progress,
        &ProgressDlg::commandsFinished,
        &progress,
        [&progress] {
            if (progress.gitStatus() == 0) {
                progress.accept();
            }
        },
        Qt::QueuedConnection);
    progress.exec();
}

} // namespace

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
            result = fetch(clean, value(QStringLiteral("remote"))) && result;
        }
        return result;
    }
    return fetch(Git::repositoryRoot(paths().constFirst()), value(QStringLiteral("remote")));
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

FetchedUpstream FetchCommand::rebaseUpstream(const Git& git, const PullFetchOptions& options)
{
    const QStringList remotes = QString::fromUtf8(git.run({QStringLiteral("remote")}, RunMode::ReadOnly)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (options.allRemotes || !remotes.contains(options.remoteUrl)) {
        return {.upstream = QStringLiteral("FETCH_HEAD"), .oldHash = {}};
    }

    FetchedUpstream fetched;
    if (!options.remoteBranchName.isEmpty()) {
        fetched.upstream = QStringLiteral("remotes/%1/%2").arg(options.remoteUrl, options.remoteBranchName);
    } else if (const QString currentBranch = git.currentBranch(); currentBranch != QLatin1String("(no branch)")) {
        // PullFetchDlg clears the remote branch if it is the default tracked branch (CGit::GetRemoteTrackedBranch)
        const QString pullRemote = git.configValue(QStringLiteral("branch.%1.remote").arg(currentBranch));
        const QString pullBranch = Git::stripRefName(git.configValue(QStringLiteral("branch.%1.merge").arg(currentBranch)));
        // pullRemote == url is just another safety check and should not be needed
        if (!pullBranch.isEmpty() && pullRemote == options.remoteUrl) {
            fetched.upstream = QStringLiteral("remotes/%1/%2").arg(options.remoteUrl, pullBranch);
        }
    }
    fetched.oldHash = git.hash(fetched.upstream);
    return fetched;
}

void FetchCommand::rebaseFetched(const Git& git, const FetchedUpstream& fetched, FetchRebase runRebase, bool preserveMerges, QWidget *parent)
{
    const QString& upstream = fetched.upstream;
    if (runRebase == FetchRebase::Open) {
        const QString remoteBranchHash = git.hash(upstream);
        const QString headHash = git.hash(QStringLiteral("HEAD"));
        if (!headHash.isEmpty() && (remoteBranchHash == headHash || git.isFastForward(upstream, QStringLiteral("HEAD")))
            && !askOpenRebase(parent,
                              i18n("Current branch is up to date or newer than the fetched branch. Open rebase anyway?"),
                              QStringLiteral("OpenRebaseRemoteBranchEqualsHEAD"))) {
            return;
        }
        if (!fetched.oldHash.isEmpty() && remoteBranchHash == fetched.oldHash && remoteBranchHash == headHash
            && !askOpenRebase(parent,
                              i18n("The remote branch has not changed.\n\nOpen the rebase dialog anyway?"),
                              QStringLiteral("OpenRebaseRemoteBranchUnchanged"))) {
            return;
        }
        if (git.isFastForward(QStringLiteral("HEAD"), upstream)) {
            const auto answer = KMessageBox::questionTwoActionsCancel(
                parent,
                i18n("The fetched branch fast-forwards upon the current branch.\n\nMerge or open the rebase dialog anyway?"),
                QString(),
                KGuiItem(i18n("&Merge")),
                KGuiItem(i18n("&Rebase")),
                KGuiItem(i18n("A&bort")),
                QStringLiteral("OpenRebaseRemoteBranchFastForwards"),
                KMessageBox::Options());
            if (answer == KMessageBox::Cancel) {
                return;
            }
            if (answer == KMessageBox::PrimaryAction) {
                mergeFastForward(git, upstream, parent);
                return;
            }
        }
    }

    RebaseOptions rebaseOptions;
    rebaseOptions.upstream = upstream;
    rebaseOptions.autoStart = runRebase == FetchRebase::AutoStart;
    rebaseOptions.preserveMerges = preserveMerges;
    AppUtils::rebaseAfterFetch(git.workingDirectory(), rebaseOptions, parent);
}

bool FetchCommand::doFetch(const QString& repository, const PullFetchOptions& options, FetchRebase runRebase, QWidget *parent)
{
    const Git git(repository);
    const FetchedUpstream fetched = runRebase == FetchRebase::None ? FetchedUpstream{.upstream = {}, .oldHash = {}} : rebaseUpstream(git, options);

    ProgressDlg progress(repository, parent);
    progress.addGitCmd(fetchArguments(options));
    progress.setPostExecCallback([&](const int& exitCode, const QString& /*extraMsg*/) {
        if (exitCode == 0 && runRebase != FetchRebase::None) {
            rebaseFetched(git, fetched, runRebase, options.rebasePreserveMerges, &progress);
        }
    });
    progress.exec();
    return progress.gitStatus() == 0;
}

bool FetchCommand::fetch(const QString& repository, const QString& remoteName)
{
    PullFetchDlg dlg(repository, false, nullptr, remoteName);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    const PullFetchOptions options = dlg.options();
    return doFetch(repository, options, options.rebase ? FetchRebase::Open : FetchRebase::None);
}

} // namespace kgt
