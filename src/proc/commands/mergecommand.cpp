// SPDX-FileCopyrightText: 2009-2013, 2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/MergeCommand.cpp, CAppUtils::Merge and DoMerge. The
 * post-command buttons (Resolve, Commit, Merge unrelated histories, Stash, Stash Pop, Remove branch, Push) are left
 * out for now.
 */

#include "commands/mergecommand.hpp"

#include "apputils.hpp"
#include "mergedlg.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
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

bool MergeCommand::merge(const QString& workingTree, const QString& initialRefName, QWidget *parent)
{
    if (AppUtils::isTGitRebaseActive(workingTree, parent)) {
        return false;
    }

    const Git git(workingTree);
    MergeDlg dlg(Git(workingTree), initialRefName, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    MergeOptions options = dlg.options();
    options.version = git.fixBranchName(options.version);

    // The message goes to git in a file, as SaveCommitUnicodeFile writes it; it must outlive the merge
    QTemporaryFile messageFile(QDir::temp().absoluteFilePath(QStringLiteral("kortoisegit-merge-XXXXXX.txt")));
    if (!options.message.isEmpty()) {
        if (!messageFile.open() || messageFile.write(options.message.toUtf8()) < 0 || !messageFile.flush()) {
            KMessageBox::error(parent, i18n("Could not save merge message"), QString(), KMessageBox::Options());
            return false;
        }
    }

    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(mergeArguments(options, messageFile.fileName()));
    // DoMerge's m_PostCmdCallback: a failed merge may have left conflicts to resolve
    QObject::connect(&progress, &ProgressDlg::commandsFinished, &progress, [&progress, &git] {
        if (progress.gitStatus() == 0) {
            return;
        }
        try {
            if (git.hasWorkingTreeConflicts()) {
                AppUtils::showMergeConflictsHint(&progress);
            }
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            KMessageBox::error(&progress, i18n("Checking for conflicts failed.") + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
        }
    });

    // Leaving the progress dialog with Abort while files are in conflict offers to abort the merge
    if (progress.exec() != QDialog::Accepted && git.hasWorkingTreeConflicts()) {
        std::ignore = AppUtils::mergeAbort(workingTree, parent);
        return false;
    }
    return progress.gitStatus() == 0;
}

} // namespace kgt
