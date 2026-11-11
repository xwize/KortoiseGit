// SPDX-FileCopyrightText: 2009-2013, 2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/MergeCommand.cpp, CAppUtils::Merge and DoMerge. The git-svn
 * dcommit follow-up is left out.
 */

#include "commands/mergecommand.hpp"

#include "apputils.hpp"
#include "mergedlg.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
#include <QIcon>
#include <QTemporaryFile>

#include <tuple>

namespace kgt
{

MergeCommand::MergeCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool MergeCommand::execute()
{
    const QString workingTree = Git::workingTreeRoot(paths().constFirst());
    if (hasKey(QStringLiteral("abort"))) {
        return AppUtils::mergeAbort(workingTree);
    }
    return merge(workingTree);
}

QStringList MergeCommand::mergeArguments(const MergeOptions& options, const QString& messageFile)
{
    QStringList arguments{QStringLiteral("merge")};
    if (options.noFF) {
        arguments.append(QStringLiteral("--no-ff"));
    } else if (options.ffOnly) {
        arguments.append(QStringLiteral("--ff-only"));
    }
    if (options.squash) {
        arguments.append(QStringLiteral("--squash"));
    }
    if (options.noCommit) {
        arguments.append(QStringLiteral("--no-commit"));
    }
    if (options.unrelated) {
        arguments.append(QStringLiteral("--allow-unrelated-histories"));
    }
    if (options.log) {
        arguments.append(QStringLiteral("--log=%1").arg(*options.log));
    }
    if (!options.strategy.isEmpty()) {
        arguments.append(QStringLiteral("--strategy=") + options.strategy);
        if (!options.strategyOption.isEmpty()) {
            QString strategyOption = QStringLiteral("--strategy-option=") + options.strategyOption;
            if (!options.strategyParam.isEmpty()) {
                strategyOption += QLatin1Char('=') + options.strategyParam;
            }
            arguments.append(strategyOption);
        }
    }
    if (!options.message.isEmpty()) {
        arguments << QStringLiteral("-F") << messageFile;
    }
    arguments << QStringLiteral("--") << options.version;
    return arguments;
}

bool MergeCommand::merge(const QString& workingTree, const QString& initialRefName, QWidget *parent, bool showStashPop)
{
    if (AppUtils::isTGitRebaseActive(workingTree, parent)) {
        return false;
    }

    MergeDlg dlg(Git(workingTree), initialRefName, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    return doMerge(workingTree, dlg.options(), showStashPop, parent);
}

bool MergeCommand::doMerge(const QString& workingTree, const MergeOptions& options, bool showStashPop, QWidget *parent)
{
    const Git git(workingTree);
    MergeOptions fixed = options;
    fixed.version = git.fixBranchName(options.version);

    // The message goes to git in a file, as SaveCommitUnicodeFile writes it; it must outlive the merge
    QTemporaryFile messageFile(QDir::temp().absoluteFilePath(QStringLiteral("kortoisegit-merge-XXXXXX.txt")));
    if (!options.message.isEmpty()) {
        if (!messageFile.open() || messageFile.write(options.message.toUtf8()) < 0 || !messageFile.flush()) {
            KMessageBox::error(parent, i18n("Could not save merge message"), QString(), KMessageBox::Options());
            return false;
        }
    }

    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(mergeArguments(fixed, messageFile.fileName()));
    // DoMerge's m_PostCmdCallback
    progress.setPostCmdCallback([&](int status, PostCmdList& postCmdList) {
        if (status == 0) {
            addSuccessPostCmds(workingTree, options, showStashPop, parent, postCmdList);
            return;
        }
        try {
            if (git.hasWorkingTreeConflicts()) {
                AppUtils::showMergeConflictsHint(&progress);
                AppUtils::addConflictPostCmds(workingTree, postCmdList);
            }
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            KMessageBox::error(&progress, i18n("Checking for conflicts failed.") + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
        }

        if (git.commonAncestor(QStringLiteral("HEAD"), fixed.version).isEmpty()) {
            postCmdList.append(
                {QIcon::fromTheme(QStringLiteral("vcs-merge")), i18n("&Merge unrelated history"), [&workingTree, &options, showStashPop, parent] {
                     MergeOptions unrelated = options;
                     unrelated.unrelated = true;
                     std::ignore = doMerge(workingTree, unrelated, showStashPop, parent);
                 }});
        }
        const StashSaveFollowUps stashFollowUps{.showPull = false, .pullShowPush = false, .showMerge = true, .mergeRev = fixed.version};
        postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-stash")), i18n("Stash changes"), [&workingTree, stashFollowUps, parent] {
                                std::ignore = AppUtils::stashSave(Git(workingTree), QString(), parent, stashFollowUps);
                            }});
    });

    // Leaving the progress dialog with Abort while files are in conflict offers to abort the merge
    if (progress.exec() != QDialog::Accepted && git.hasWorkingTreeConflicts()) {
        std::ignore = AppUtils::mergeAbort(workingTree, parent);
        return false;
    }
    return progress.gitStatus() == 0;
}

void MergeCommand::removeBranch(const QString& workingTree, const QString& branch, QWidget *parent)
{
    // DoMerge's Remove branch: asks, then git branch -D
    const QString message = i18n("Do you really want to delete \"%1\"?", branch);
    const KGuiItem erase(i18n("&Delete"));
    const KGuiItem stop(i18n("A&bort"));
    if (KMessageBox::questionTwoActions(parent, message, QString(), erase, stop) != KMessageBox::PrimaryAction) {
        return;
    }
    try {
        std::ignore = Git(workingTree).run({QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("--"), branch});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(parent, e.error().message, QString(), KMessageBox::Options());
    }
}

void MergeCommand::addSuccessPostCmds(const QString& workingTree, const MergeOptions& options, bool showStashPop, QWidget *parent, PostCmdList& postCmdList)
{
    if (showStashPop) {
        postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-stash-pop")), i18n("Stash Pop"), [workingTree, parent] {
                                std::ignore = AppUtils::stashPop(workingTree, parent);
                            }});
    }

    if (options.noCommit || options.squash) {
        postCmdList.append(AppUtils::commitPostCmd(workingTree));
        return;
    }

    // Do not ask to remove remote branches
    if (options.isBranch && !options.version.startsWith(QLatin1String("remotes/"))) {
        const QString version = options.version;
        postCmdList.append({QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove &branch"), [workingTree, version, parent] {
                                removeBranch(workingTree, version, parent);
                            }});
    }
    if (options.isBranch) {
        postCmdList.append({QIcon::fromTheme(QStringLiteral("vcs-push")), i18n("Pus&h..."), [workingTree, parent] {
                                std::ignore = AppUtils::push(workingTree, QString(), parent);
                            }});
    }
}

} // namespace kgt
