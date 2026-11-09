// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AppUtils.cpp

#include "apputils.hpp"

#include "browserefsdlg.hpp"
#include "commands/pushcommand.hpp"
#include "createbranchtagdlg.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "gitswitchdlg.hpp"
#include "kortoisegitproc_debug.hpp"
#include "mergeabortdlg.hpp"
#include "progressdlg.hpp"
#include "pushdlg.hpp"
#include "resetdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryFile>

#include <array>
#include <tuple>

/**
 * Registers icons.qrc, which a static library has to do itself. Q_INIT_RESOURCE must be outside any namespace.
 */
static void registerIconResources()
{
    Q_INIT_RESOURCE(icons);
}

namespace
{

using namespace kgt;

constexpr KMessageBox::Options noNotify = KMessageBox::Options();

/**
 * Asks a question with two choices and Abort.
 * @param parent Parent of the question.
 * @param text The question.
 * @param first The first choice.
 * @param second The second choice.
 * @returns PrimaryAction, SecondaryAction or Cancel.
 */
KMessageBox::ButtonCode askOrAbort(QWidget *parent, const QString& text, const KGuiItem& first, const KGuiItem& second)
{
    const KGuiItem stop(i18n("A&bort"));
    return KMessageBox::questionTwoActionsCancel(parent, text, QString(), first, second, stop, QString(), noNotify);
}

/**
 * Deletes a ref, showing git's error if it can't (the failure branches of CAppUtils::DeleteRef).
 * @param git Runs git.
 * @param ref The full ref name.
 * @param parent Parent of the message.
 * @returns Whether it was deleted.
 */
bool deleteRefReportingFailure(const Git& git, const QString& ref, QWidget *parent)
{
    try {
        git.deleteRef(ref);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        const QString message = i18n("Could not delete reference.") + QStringLiteral("\n") + e.error().message;
        KMessageBox::error(parent, message, QString(), noNotify);
        return false;
    }
    return true;
}

/**
 * The refs/remotes/ part of CAppUtils::DeleteRef: on the remote as well, or only the local remote-tracking branch.
 * @param git Runs git.
 * @param ref The full ref name.
 * @param parent Parent of the dialogs.
 * @returns False if the user aborted or the local delete failed.
 */
bool deleteRemoteTrackingBranch(const Git& git, const QString& ref, QWidget *parent)
{
    const QString question = i18n(
        "The branch \"%1\" is a remote-tracking branch which locally represents a remote branch.\n\n"
        "Do you really want to delete it?",
        ref);
    const KGuiItem bothItem(i18n("&Delete branch on remote && local remote-tracking branch"));
    const KGuiItem localItem(i18n("Delete &local remote-tracking branch"));
    const auto answer = askOrAbort(parent, question, bothItem, localItem);
    if (answer == KMessageBox::SecondaryAction) {
        return deleteRefReportingFailure(git, ref, parent);
    }
    if (answer != KMessageBox::PrimaryAction) {
        return false;
    }
    const QString shortName = ref.mid(QLatin1String("refs/remotes/").size());
    const qsizetype slash = shortName.indexOf(QLatin1Char('/'));
    // TortoiseGit shows "Deleting remote refs..."; ProgressDlg shows git's output too, a failure included
    ProgressDlg progress(git.workingDirectory(), parent);
    progress.addGitCmd(BrowseRefsDlg::deleteRemoteRefsArguments(shortName.left(slash), {shortName.mid(slash + 1)}));
    progress.exec();
    return true;
}

/**
 * The refs/stash part of CAppUtils::DeleteRef: every stash, or only the newest.
 * @param git Runs git.
 * @param ref The full ref name.
 * @param parent Parent of the dialogs.
 * @returns False if the user aborted; a failing git command only shows its error, as in TortoiseGit.
 */
bool deleteStash(const Git& git, const QString& ref, QWidget *parent)
{
    qsizetype count = 0;
    const QStringList reflog{QStringLiteral("reflog"), QStringLiteral("show"), QStringLiteral("--format=%H"), ref};
    try {
        count = git.run(reflog, RunMode::ReadOnly).count('\n');
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    const QString question = i18n("Do you really want to delete ALL %1 stash?", count);
    const KGuiItem allItem(i18n("&Delete"));
    const KGuiItem oneItem(i18n("Drop &one stash"));
    const auto answer = askOrAbort(parent, question, allItem, oneItem);
    if (answer != KMessageBox::PrimaryAction && answer != KMessageBox::SecondaryAction) {
        return false;
    }
    const QStringList arguments = answer == KMessageBox::PrimaryAction
        ? QStringList{QStringLiteral("stash"), QStringLiteral("clear")}
        : QStringList{QStringLiteral("stash"), QStringLiteral("drop"), QStringLiteral("refs/stash@{0}")};
    try {
        std::ignore = git.run(arguments);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(parent, e.error().message, QString(), KMessageBox::Options());
    }
    return true;
}

} // namespace

namespace kgt
{

void AppUtils::initIconResources()
{
    registerIconResources();
}

KConfigGroup AppUtils::generalSettings()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("General"));
}

QByteArray AppUtils::outputOrEmpty(const GitCall& call)
{
    try {
        return call.output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        qCWarning(KGT_PROC) << "Loading the branches or tags failed:" << e.error().message;
        return {};
    }
}

QIcon AppUtils::appIcon()
{
    registerIconResources();
    QIcon bundled;
    for (const int size : std::array{16, 22, 32, 48, 64, 128, 256}) {
        bundled.addFile(QStringLiteral(":/kortoisegit/icons/kortoisegit-%1.png").arg(size), QSize(size, size));
    }
    return QIcon::fromTheme(QStringLiteral("kortoisegit"), bundled);
}

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

bool AppUtils::gitReset(const QString& workingTree, const QString& ref, QWidget *parent)
{
    ResetDlg dlg(Git(workingTree), ref, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    return reset(workingTree, dlg.resetType(), dlg.resetToVersion(), parent);
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

bool AppUtils::createBranchTag(const QString& workingTree, const CreateBranchTagParams& params, QWidget *parent)
{
    const Git git(workingTree);
    CreateBranchTagDlg dlg(git, params, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    CreateBranchTagOptions options = dlg.options();
    options.versionName = git.fixBranchName(options.versionName);

    // A tag's message goes to git in a file, as SaveCommitUnicodeFile writes it; it must outlive git tag
    QTemporaryFile messageFile(QDir::temp().absoluteFilePath(QStringLiteral("kortoisegit-tag-XXXXXX.txt")));
    if (params.isTag) {
        options.message = options.message.trimmed();
        if (!options.message.isEmpty() && (!messageFile.open() || messageFile.write(options.message.toUtf8()) < 0 || !messageFile.flush())) {
            KMessageBox::error(parent, i18n("Could not save tag message"), QString(), KMessageBox::Options());
            return false;
        }
    }

    try {
        std::ignore = git.run(params.isTag ? createTagArguments(options, messageFile.fileName()) : createBranchArguments(options));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(parent, e.error().message, QString(), KMessageBox::Options());
        return false;
    }

    if (params.isTag) {
        if (options.push) {
            push(workingTree, QStringLiteral("refs/tags/") + options.branchTagName, parent);
        }
        return true;
    }

    if (options.switchToNewBranch) {
        // It is a new branch and the user has requested to switch to it
        SwitchOptions switchOptions;
        switchOptions.ref = options.branchTagName;
        performSwitch(workingTree, switchOptions, parent);
    }
    if (!options.message.isEmpty()) {
        updateBranchDescription(git, options.branchTagName, options.message);
    }
    return true;
}

QStringList AppUtils::createBranchArguments(const CreateBranchTagOptions& options)
{
    QStringList arguments{QStringLiteral("branch")};
    if (options.force) {
        arguments.append(QStringLiteral("-f"));
    }
    if (options.track == Qt::Checked) {
        arguments.append(QStringLiteral("--track"));
    } else if (options.track == Qt::Unchecked) {
        arguments.append(QStringLiteral("--no-track"));
    }
    arguments << QStringLiteral("--") << options.branchTagName << options.versionName;
    return arguments;
}

QStringList AppUtils::createTagArguments(const CreateBranchTagOptions& options, const QString& messageFile)
{
    QStringList arguments{QStringLiteral("tag")};
    if (options.force) {
        arguments.append(QStringLiteral("-f"));
    }
    if (options.sign) {
        arguments.append(QStringLiteral("-s"));
    }
    // Without a message the tag is lightweight
    if (!options.message.isEmpty()) {
        arguments << QStringLiteral("-F") << messageFile;
    }
    arguments << QStringLiteral("--") << options.branchTagName << options.versionName;
    return arguments;
}

bool AppUtils::push(const QString& workingTree, const QString& selectLocalBranch, QWidget *parent)
{
    PushDlg dlg(workingTree, parent, selectLocalBranch);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    ProgressDlg progress(workingTree, parent);
    progress.addGitCmd(PushCommand::pushArguments(dlg.options()));
    progress.exec();
    return progress.gitStatus() == 0;
}

bool AppUtils::deleteRef(const Git& git, const QString& ref, QWidget *parent)
{
    if (ref.startsWith(QLatin1String("refs/remotes/"))) {
        return deleteRemoteTrackingBranch(git, ref, parent);
    }
    if (ref.startsWith(QLatin1String("refs/stash"))) {
        return deleteStash(git, ref, parent);
    }

    QString message = i18n("Do you really want to delete \"%1\"?", ref);
    // Check if branch is fully merged in HEAD
    if (ref.startsWith(QLatin1String("refs/heads/")) && !git.isFastForward(ref, QStringLiteral("HEAD"))) {
        message += QStringLiteral("\n\n") + i18n("This branch is not fully merged into HEAD.");
    }
    const KGuiItem erase(i18n("&Delete"));
    const KGuiItem stop(i18n("A&bort"));
    const auto answer = KMessageBox::questionTwoActions(parent, message, QString(), erase, stop, QString(), noNotify);
    return answer == KMessageBox::PrimaryAction && deleteRefReportingFailure(git, ref, parent);
}

bool AppUtils::updateBranchDescription(const Git& git, const QString& branch, QString description)
{
    if (branch.isEmpty()) {
        return false;
    }

    const QString key = QStringLiteral("branch.%1.description").arg(branch);
    description.remove(QLatin1Char('\r'));
    description = description.trimmed();
    try {
        if (description.isEmpty()) {
            git.unsetConfigValue(key);
        } else {
            git.setConfigValue(key, description);
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        qCWarning(KGT_PROC) << "Updating the branch description failed:" << e.error().message;
    }
    return true;
}

} // namespace kgt
