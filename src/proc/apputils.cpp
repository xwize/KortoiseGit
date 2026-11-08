// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AppUtils.cpp

#include "apputils.hpp"

#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "gitswitchdlg.hpp"
#include "mergeabortdlg.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>

#include <tuple>

namespace kgt
{

QString AppUtils::getClipboardLink(const QString& skipGitPrefix, int paramsCount)
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return {};
    }
    return clipboardLink(clipboard->text(), skipGitPrefix, paramsCount);
}

QString AppUtils::clipboardLink(QString clipboardText, const QString& skipGitPrefix, int paramsCount)
{
    if (clipboardText.isEmpty()) {
        return {};
    }
    if (clipboardText.size() >= 2 && clipboardText.startsWith(QLatin1Char('"')) && clipboardText.endsWith(QLatin1Char('"'))) {
        clipboardText = clipboardText.mid(1, clipboardText.size() - 2);
    }
    if (const qsizetype newline = clipboardText.indexOf(QLatin1Char('\n')); newline >= 0) {
        clipboardText.truncate(newline);
        while (!clipboardText.isEmpty() && clipboardText.back().isSpace()) {
            clipboardText.chop(1);
        }
    }
    if (clipboardText.isEmpty()) {
        return {};
    }

    for (const QLatin1String prefix :
         {QLatin1String("http://"), QLatin1String("https://"), QLatin1String("git://"), QLatin1String("ssh://"), QLatin1String("git@")}) {
        if (clipboardText.startsWith(prefix) && clipboardText.size() != prefix.size()) {
            return clipboardText;
        }
    }
    // TortoiseGit takes a local path starting with a drive letter; here, an absolute path
    if (clipboardText.size() >= 2 && clipboardText.startsWith(QLatin1Char('/'))) {
        return clipboardText;
    }

    // Skip prefixes like "git clone "
    if (!skipGitPrefix.isEmpty() && clipboardText.startsWith(skipGitPrefix)) {
        clipboardText = clipboardText.mid(skipGitPrefix.size()).trimmed();
        qsizetype spacePos = -1;
        while (paramsCount >= 0) {
            --paramsCount;
            spacePos = clipboardText.indexOf(QLatin1Char(' '), spacePos + 1);
            if (spacePos == -1) {
                break;
            }
        }
        if (spacePos > 0 && paramsCount < 0) {
            clipboardText.truncate(spacePos);
        }
        return clipboardText;
    }
    return {};
}

bool AppUtils::runTortoiseGitProc(const QStringList& arguments)
{
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

bool AppUtils::isTGitRebaseActive(const QString& workingTree, QWidget *parent)
{
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(workingTree);
    if (adminDir.isEmpty()) {
        return false;
    }
    const QDir lock(QDir(adminDir).filePath(QStringLiteral("tgitrebase.active")));
    if (!lock.exists()) {
        return false;
    }
    const auto answer = KMessageBox::warningTwoActions(parent,
                                                       i18n("Found a lock file indicating that a TortoiseGit rebase process is running. This blocks "
                                                            "this operation.\n\nIf you are not running a TortoiseGit rebase process right now, you "
                                                            "can remove the stale lock file in order to continue."),
                                                       QString(),
                                                       KGuiItem(i18n("Remove stale lock file")),
                                                       KGuiItem(i18n("A&bort")),
                                                       QString(),
                                                       KMessageBox::Options());
    if (answer != KMessageBox::PrimaryAction) {
        return true;
    }
    // RemoveDirectory: only an empty directory goes
    std::ignore = lock.rmdir(lock.absolutePath());
    return false;
}

void AppUtils::showMergeConflictsHint(QWidget *parent)
{
    KMessageBox::information(
        parent,
        i18n("While merging, i.e. integrating changes of another (remote) branch into your local branch, a conflict in at least one file "
             "occurred. This means that you need to resolve this manually (i.e., you need to integrate your changes into a file which was "
             "also modified on another branch).\n\nAfter resolving all files, you need to perform a commit in order to complete the "
             "merge.\n\nIf you want to abort the merge, do a hard reset on HEAD or select abort merge on the context menu.\n\nSee help for "
             "more information."),
        QString(),
        QStringLiteral("MergeConflictsNeedsCommit"));
}

QStringList AppUtils::resetArguments(ResetType type, const QString& resetTo)
{
    QString typeOption;
    switch (type) {
    case ResetType::Soft:
        typeOption = QStringLiteral("--soft");
        break;
    case ResetType::Mixed:
        typeOption = QStringLiteral("--mixed");
        break;
    case ResetType::Hard:
        typeOption = QStringLiteral("--hard");
        break;
    case ResetType::Merge:
        return {QStringLiteral("reset"), QStringLiteral("--merge")};
    }
    return {QStringLiteral("reset"), typeOption, QStringLiteral("--end-of-options"), resetTo, QStringLiteral("--")};
}

bool AppUtils::reset(const QString& workingTree, ResetType type, const QString& resetTo, QWidget *parent)
{
    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(resetArguments(type, resetTo));
    return progress.exec() == QDialog::Accepted && progress.gitStatus() == 0;
}

bool AppUtils::mergeAbort(const QString& workingTree, QWidget *parent)
{
    MergeAbortDlg dlg(workingTree, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    return reset(workingTree, dlg.resetType(), QStringLiteral("HEAD"), parent);
}

bool AppUtils::switchTo(const QString& workingTree, const QString& initialRefName, QWidget *parent)
{
    GitSwitchDlg dlg(Git(workingTree), initialRefName, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    return performSwitch(workingTree, switchOptions(dlg.options()), parent);
}

SwitchOptions AppUtils::switchOptions(const GitSwitchOptions& chosen)
{
    // If refs/heads/ is not stripped, checkout will detach HEAD; checkout prefers branches on name clashes (with tags)
    QString ref = chosen.versionName;
    constexpr QLatin1StringView headsPrefix("refs/heads/");
    if (ref.startsWith(headsPrefix) && !chosen.branchOverride) {
        ref = ref.mid(headsPrefix.size());
    }
    return SwitchOptions{
        .ref = ref,
        .force = chosen.force,
        .newBranch = chosen.isNewBranch ? chosen.newBranch : QString(),
        .branchOverride = chosen.branchOverride,
        .track = chosen.track,
        .merge = chosen.merge,
    };
}

QStringList AppUtils::switchArguments(const SwitchOptions& options)
{
    QStringList arguments{QStringLiteral("checkout")};
    if (options.force) {
        arguments.append(QStringLiteral("-f"));
    }
    if (!options.newBranch.isEmpty()) {
        if (options.track == Qt::Checked) {
            arguments.append(QStringLiteral("--track"));
        } else if (options.track == Qt::Unchecked) {
            arguments.append(QStringLiteral("--no-track"));
        }
        arguments << (options.branchOverride ? QStringLiteral("-B") : QStringLiteral("-b")) << options.newBranch;
    }
    if (options.merge) {
        arguments.append(QStringLiteral("--merge"));
    }
    arguments << QStringLiteral("--end-of-options") << options.ref << QStringLiteral("--");
    return arguments;
}

bool AppUtils::performSwitch(const QString& workingTree, const SwitchOptions& options, QWidget *parent)
{
    const Git git(workingTree);
    SwitchOptions fixed = options;
    fixed.ref = git.fixBranchName(options.ref);

    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(switchArguments(fixed));
    // m_PostExecCallback: git checkout --merge succeeds even when it leaves conflicts
    progress.setPostExecCallback([&git, merge = options.merge](int& exitCode, QString& extraMsg) {
        if (!merge || exitCode != 0) {
            return;
        }
        try {
            if (git.hasWorkingTreeConflicts()) {
                exitCode = 1; // Treat it as failure
                extraMsg = i18n("Has merge conflict");
            }
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            // CGit::HasWorkingTreeConflicts returns -1 on an error, which the check takes as no conflicts
        }
    });
    return progress.exec() == QDialog::Accepted && progress.gitStatus() == 0;
}

} // namespace kgt
