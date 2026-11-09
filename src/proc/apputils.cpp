// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AppUtils.cpp

#include "apputils.hpp"

#include "browserefsdlg.hpp"
#include "commands/pushcommand.hpp"
#include "createbranchtagdlg.hpp"
#include "deleteconflictdlg.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "gitswitchdlg.hpp"
#include "kortoisegitproc_debug.hpp"
#include "mergeabortdlg.hpp"
#include "progressdlg.hpp"
#include "pushdlg.hpp"
#include "resetdlg.hpp"
#include "stashsave.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>
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

constexpr qsizetype shortHashLength = 8;

/**
 * What the messages of CAppUtils::StashApply and StashPop say.
 */
struct StashMessages {
    QString success;
    QString conflicts;
    QString failed;
};

/**
 * The IDS_PROC_STASHRUNNING window, TortoiseGit's CSysProgressDlg. It closes when git ends and not before, as the
 * result is read then.
 */
class StashRunningDlg : public QDialog
{
public:
    using QDialog::QDialog;

    /**
     * Ignores Esc and the window's close button.
     */
    void reject() override
    {
    }
};

/**
 * The body CAppUtils::StashApply and StashPop share.
 * @param workingTree The working tree's root.
 * @param arguments The arguments after "git".
 * @param messages What to tell the user.
 * @param parent Parent of the windows.
 * @returns False if git failed.
 */
bool runStash(const QString& workingTree, const QStringList& arguments, const StashMessages& messages, QWidget *parent)
{
    const std::unique_ptr<GitCall> call = Git(workingTree).runAsync(arguments);

    StashRunningDlg running(parent);
    running.setWindowTitle(i18n("KortoiseGit"));
    auto *layout = new QVBoxLayout(&running);
    layout->addWidget(new QLabel(i18n("Stash operation running..."), &running));
    layout->addWidget(new QLabel(i18n("Please wait..."), &running));
    QObject::connect(call.get(), &GitCall::finished, &running, &QDialog::accept);
    running.exec();

    const QString output = call->combinedOutput();
    // TODO: TortoiseGit adds IDS_SEECHANGES to these and opens repostatus, once that is ported
    switch (AppUtils::stashOutcome(call->exitCode(), output)) {
    case StashOutcome::Success:
        QMessageBox::information(parent, i18n("KortoiseGit"), messages.success);
        return true;
    case StashOutcome::Conflicts:
        QMessageBox::warning(parent, i18n("KortoiseGit"), messages.conflicts);
        return true;
    case StashOutcome::Failed:
        break;
    }
    QMessageBox::critical(parent, i18n("KortoiseGit"), messages.failed + QLatin1Char('\n') + output);
    return false;
}

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

/**
 * Ported from CTGitPath::IsRebaseActive.
 * @param adminDir The working tree's admin directory.
 * @returns Whether a rebase is in progress.
 */
bool isRebaseActive(const QString& adminDir)
{
    const QDir dir(adminDir);
    const auto isDirectory = [&dir](const QString& name) {
        return QFileInfo(dir.filePath(name)).isDir();
    };
    return isDirectory(QStringLiteral("rebase-apply")) || isDirectory(QStringLiteral("tgitrebase.active"));
}

/**
 * Ported from CStringUtils::ReadStringFromTextFile.
 * @param path A file.
 * @returns Its first line, or nothing if it can't be read.
 */
std::optional<QString> readFirstLine(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return QString::fromUtf8(file.readLine()).trimmed();
}

/**
 * Ported from CGit::GetHash.
 * @param git Where to look.
 * @param ref A ref or a file in the admin directory that holds a hash, such as MERGE_HEAD.
 * @returns The hash, or an empty string if there is no such ref.
 */
QString hashOf(const Git& git, const QString& ref)
{
    const QStringList arguments{
        QStringLiteral("rev-parse"),
        QStringLiteral("--verify"),
        QStringLiteral("--quiet"),
        ref,
    };
    try {
        return QString::fromLatin1(git.run(arguments, RunMode::ReadOnly)).trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

/**
 * The ref-and-hash text CAppUtils::GetConflictTitles builds with CGit::GuessRefForHash.
 * @param git Where to look.
 * @param hash A commit.
 * @returns "ref, hash", or the short hash alone if no ref points at it.
 */
QString guessedRefText(const Git& git, const QString& hash)
{
    const QString shortHash = hash.left(shortHashLength);
    const std::optional<QString> ref = git.guessRefForHash(hash);
    return ref ? *ref + QStringLiteral(", ") + shortHash : shortHash;
}

/**
 * The rebase branch of CAppUtils::GetConflictTitles.
 * @param git Where to look.
 * @param adminDir The working tree's admin directory.
 * @returns The titles, with our side being the branch rebased onto.
 */
ConflictTitles rebaseConflictTitles(const Git& git, const QString& adminDir)
{
    ConflictTitles titles{
        .mineText = i18n("Branch being rebased onto"),
        .mineHash = {},
        .theirsText = i18n("Branch being rebased"),
        .theirsHash = {},
    };
    const QDir dir(adminDir);
    const QString tgitHeadName = dir.filePath(QStringLiteral("tgitrebase.active/head-name"));
    const QString applyHeadName = dir.filePath(QStringLiteral("rebase-apply/head-name"));
    if (const std::optional<QString> onto = readFirstLine(dir.filePath(QStringLiteral("tgitrebase.active/onto")))) {
        titles.mineText = *onto;
    } else if (const QString hash = hashOf(git, QStringLiteral("rebase-apply/onto")); !hash.isEmpty()) {
        titles.mineText = guessedRefText(git, hash);
        titles.mineHash = hash;
    }
    if (const std::optional<QString> head = readFirstLine(tgitHeadName)) {
        titles.theirsText = *head;
    } else if (const std::optional<QString> applyHead = readFirstLine(applyHeadName)) {
        titles.theirsText = Git::stripRefName(*applyHead);
    }
    return titles;
}

/**
 * The merge, cherry-pick and revert branch of CAppUtils::GetConflictTitles. Its ConflictDontGuessBranchNames setting
 * isn't ported.
 * @param git Where to look.
 * @returns The titles, with our side being HEAD.
 */
ConflictTitles mergeConflictTitles(const Git& git)
{
    ConflictTitles titles{
        .mineText = QStringLiteral("HEAD"),
        .mineHash = {},
        .theirsText = i18n("changes to-be-integrated"),
        .theirsHash = {},
    };
    const auto headTitle = [](const QString& head, const QString& commit) {
        return i18nc("%1 is a ref such as MERGE_HEAD, %2 names its commit", "%1 (%2)", head, commit);
    };
    const QString mergeHead = QStringLiteral("MERGE_HEAD");
    const QString cherryPickHead = QStringLiteral("CHERRY_PICK_HEAD");
    if (const QString hash = hashOf(git, mergeHead); !hash.isEmpty()) {
        titles.theirsText = headTitle(mergeHead, guessedRefText(git, hash));
        titles.theirsHash = hash;
    } else if (const QString pickHash = hashOf(git, cherryPickHead); !pickHash.isEmpty()) {
        titles.theirsText = headTitle(cherryPickHead, pickHash.left(shortHashLength));
        titles.theirsHash = pickHash;
    } else if (const QString revertHash = hashOf(git, QStringLiteral("REVERT_HEAD")); !revertHash.isEmpty()) {
        titles.theirsText = i18n("Parent of %1", revertHash.left(shortHashLength));
        titles.theirsHash = revertHash;
    }
    return titles;
}

/**
 * Ported from CAppUtils::DescribeConflictFile.
 * @param hasFile Whether this side has the file.
 * @param hasBase Whether the common ancestor has it.
 * @returns What this side did to the file.
 */
QString describeConflictFile(bool hasFile, bool hasBase)
{
    if (!hasFile) {
        return i18n("Deleted");
    }
    return hasBase ? i18n("Modified") : i18n("Created");
}

/**
 * The setup of CDeleteConflictDlg in CAppUtils::ConflictEdit: during a rebase the two sides swap.
 * @param hashes The file's stages.
 * @param titles The titles of the two sides.
 * @param isRebase Whether a rebase is in progress.
 * @returns The dialog's sides; the caller fills in the paths.
 */
DeleteConflictParams deleteConflictParams(const ConflictHashes& hashes, const ConflictTitles& titles, bool isRebase)
{
    const bool hasBase = !hashes.baseHash.isEmpty();
    const bool hasLocal = !hashes.localHash.isEmpty();
    const bool hasRemote = !hashes.remoteHash.isEmpty();
    DeleteConflictParams params;
    params.showModifiedButton = hasBase;
    if (isRebase) {
        params.localStatus = describeConflictFile(hasRemote, hasBase);
        params.remoteStatus = describeConflictFile(hasLocal, hasBase);
        params.localHash = titles.theirsHash;
        params.remoteHash = titles.mineHash;
        params.localRef = titles.theirsText;
        params.remoteRef = titles.mineText;
        params.diffMine = !hasLocal;
    } else {
        params.localStatus = describeConflictFile(hasLocal, hasBase);
        params.remoteStatus = describeConflictFile(hasRemote, hasBase);
        params.localHash = titles.mineHash;
        params.remoteHash = titles.theirsHash;
        params.localRef = titles.mineText;
        params.remoteRef = titles.theirsText;
        params.diffMine = hasLocal;
    }
    return params;
}

/**
 * Writes the common ancestor's version of a file where it doesn't show up in the working tree, as the prepareFile
 * step of CAppUtils::ConflictEdit does next to the file. git writes it to the root of the working tree, and
 * --prefix doesn't change that for --temp, so it is moved out.
 * @param git Runs git at the working tree's root.
 * @param path The file, relative to the root.
 * @param directory Where the copy goes.
 * @returns The copy's path.
 * @throws RuntimeError (category Git) if git fails, or category Io if the copy can't be moved.
 */
QString writeBaseVersion(const Git& git, const QString& path, const QTemporaryDir& directory)
{
    const QStringList arguments{
        QStringLiteral("--literal-pathspecs"),
        QStringLiteral("checkout-index"),
        QStringLiteral("--temp"),
        QStringLiteral("--stage=1"),
        QStringLiteral("--"),
        path,
    };
    const QByteArray output = git.run(arguments);
    // <temporary name>\t<path>\n
    const QString tempName = QString::fromUtf8(output.left(output.indexOf('\t')));
    const QString written = QDir(git.workingDirectory()).filePath(tempName);
    // CAppUtils::GetMergeTempFile's name, so a diff tool still sees the file's type
    const QFileInfo info(path);
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    const QString target = directory.filePath(info.fileName() + QStringLiteral(".BASE") + suffix);
    if (!QFile::rename(written, target)) {
        QFile::remove(written);
        const QString message = i18n("Could not write the base version of %1.", path);
        throw RuntimeError({.category = ErrorCategory::Io, .message = message});
    }
    return target;
}

/**
 * The content conflict branch of CAppUtils::ConflictEdit, which opens TortoiseMerge. That has no equivalent here, so
 * git mergetool runs the user's merge tool, detached, and marks the file resolved itself. F5 shows the result.
 * @param git Runs git at the working tree's root.
 * @param path The file, relative to the root.
 * @param parent Parent of the error message.
 */
void startMergeTool(const Git& git, const QString& path, QWidget *parent)
{
    const auto isInstalled = [](const QString& tool) {
        return !QStandardPaths::findExecutable(tool).isEmpty();
    };
    const QString configuredTool = git.configValue(QStringLiteral("merge.tool"));
    const QStringList arguments = AppUtils::mergeToolArguments(configuredTool, isInstalled);
    const auto showError = [parent](const QString& detail) {
        KMessageBox::error(parent, i18n("Could not start external merge program!\n\n%1", detail), QString(), noNotify);
    };
    if (arguments.isEmpty()) {
        showError(i18n("merge.tool is not set, and neither KDiff3 nor Meld is installed."));
        return;
    }
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(arguments + QStringList{QStringLiteral("--"), path});
    process.setWorkingDirectory(git.workingDirectory());
    // git asks "Was the merge successful?" if the tool left the file unchanged: end-of-file answers no and cleans up
    process.setStandardInputFile(QProcess::nullDevice());
    if (!process.startDetached()) {
        showError(QStringLiteral("git mergetool"));
    }
}

/**
 * The delete/modify branch of CAppUtils::ConflictEdit.
 * @param git Runs git at the working tree's root.
 * @param path The file, relative to the root.
 * @param hashes The file's stages.
 * @param parent Parent of the dialog.
 * @throws RuntimeError (category Git or Io) if git or the base version's copy fails.
 */
void editDeleteConflict(const Git& git, const QString& path, const ConflictHashes& hashes, QWidget *parent)
{
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(git.workingDirectory());
    const bool isRebase = isRebaseActive(adminDir);
    const ConflictTitles titles = isRebase ? rebaseConflictTitles(git, adminDir) : mergeConflictTitles(git);
    DeleteConflictParams params = deleteConflictParams(hashes, titles, isRebase);
    params.workingTree = git.workingDirectory();
    params.file = path;

    // Removed when this returns, which is when the dialog has closed
    const QTemporaryDir baseDirectory;
    if (params.showModifiedButton) {
        params.fileBaseVersion = writeBaseVersion(git, path, baseDirectory);
    }

    DeleteConflictDlg dlg(params, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString command = dlg.isDelete() ? QStringLiteral("rm") : QStringLiteral("add");
    std::ignore = git.run({QStringLiteral("--literal-pathspecs"), command, QStringLiteral("--"), path});
}

} // namespace

namespace kgt
{

void AppUtils::initIconResources()
{
    registerIconResources();
}

bool AppUtils::isRebaseActive(const QString& workingTree)
{
    return ::isRebaseActive(GitAdminDir::worktreeAdminDirPath(workingTree));
}

ConflictTitles AppUtils::getConflictTitles(const Git& git, bool rebaseActive)
{
    if (rebaseActive) {
        return rebaseConflictTitles(git, GitAdminDir::worktreeAdminDirPath(git.workingDirectory()));
    }
    return mergeConflictTitles(git);
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

void AppUtils::copyToClipboard(const QString& text)
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
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

QStringList AppUtils::stashSaveArguments(const StashSaveOptions& options)
{
    QStringList arguments{QStringLiteral("stash"), QStringLiteral("push")};
    if (options.includeUntracked) {
        arguments.append(QStringLiteral("--include-untracked"));
    } else if (options.all) {
        arguments.append(QStringLiteral("--all"));
    }
    if (!options.message.isEmpty()) {
        arguments << QStringLiteral("-m") << options.message;
    }
    return arguments;
}

bool AppUtils::stashSave(const Git& git, const QString& message, QWidget *parent)
{
    StashSaveDlg dlg(git.workingDirectory(), parent);
    dlg.setMessage(message);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    ProgressDlg progress(git.workingDirectory(), parent);
    progress.addGitCmd(stashSaveArguments(dlg.options()));
    return progress.exec() == QDialog::Accepted && progress.gitStatus() == 0;
}

StashOutcome AppUtils::stashOutcome(int exitCode, const QString& output)
{
    const bool hasConflicts = output.contains(QLatin1String("CONFLICT"));
    if (exitCode != 0 && !(exitCode == 1 && hasConflicts)) {
        return StashOutcome::Failed;
    }
    return hasConflicts ? StashOutcome::Conflicts : StashOutcome::Success;
}

QStringList AppUtils::stashApplyArguments(const QString& ref)
{
    QStringList arguments{QStringLiteral("stash"), QStringLiteral("apply")};
    QString name = ref;
    if (name.startsWith(QLatin1String("refs/"))) {
        name = name.mid(5);
    }
    if (name.startsWith(QLatin1String("stash{"))) {
        name = QStringLiteral("stash@") + name.mid(5);
    }
    if (!name.isEmpty()) {
        arguments.append(name);
    }
    return arguments;
}

bool AppUtils::stashApply(const Git& git, const QString& ref, QWidget *parent)
{
    const StashMessages messages{
        .success = i18n("Stash Apply successful"),
        .conflicts = i18n("Stash Apply failed, there are conflicts"),
        .failed = i18n("Stash Apply failed!"),
    };
    return runStash(git.workingDirectory(), stashApplyArguments(ref), messages, parent);
}

bool AppUtils::stashPop(const QString& workingTree, QWidget *parent)
{
    const StashMessages messages{
        .success = i18n("Stash POP successful"),
        .conflicts = i18n("Stash POP failed, there are conflicts"),
        .failed = i18n("Stash POP failed!"),
    };
    return runStash(workingTree, {QStringLiteral("stash"), QStringLiteral("pop")}, messages, parent);
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

ConflictKind AppUtils::conflictKind(const ConflictHashes& hashes)
{
    if (!hashes.baseIsFile || !hashes.localIsFile || !hashes.remoteIsFile) {
        return ConflictKind::Submodule;
    }
    const bool bothSides = !hashes.localHash.isEmpty() && !hashes.remoteHash.isEmpty();
    return bothSides ? ConflictKind::Content : ConflictKind::DeleteModify;
}

QStringList AppUtils::mergeToolArguments(const QString& configuredTool, const IsInstalled& isInstalled)
{
    QStringList arguments{
        QStringLiteral("--literal-pathspecs"),
        QStringLiteral("mergetool"),
        QStringLiteral("--no-prompt"),
    };
    if (configuredTool.isEmpty()) {
        // The graphical three-way tools, in the relative order of git's list_merge_tool_candidates
        const QStringList candidates{QStringLiteral("kdiff3"), QStringLiteral("meld")};
        const auto found = std::ranges::find_if(candidates, isInstalled);
        if (found == candidates.cend()) {
            return {};
        }
        arguments.append(QStringLiteral("--tool=") + *found);
    }
    return arguments;
}

bool AppUtils::conflictEdit(const Git& git, const QString& path, QWidget *parent)
{
    try {
        const QStringList lsFiles{
            QStringLiteral("--literal-pathspecs"),
            QStringLiteral("ls-files"),
            QStringLiteral("-u"),
            QStringLiteral("-t"),
            QStringLiteral("-z"),
            QStringLiteral("--"),
            path,
        };
        const Result<ConflictHashes> hashes = Git::parseConflictHashesFromLsFile(git.run(lsFiles, RunMode::ReadOnly));
        if (!hashes) {
            KMessageBox::error(parent, hashes.error().message, QString(), noNotify);
            return false;
        }
        QString message;
        switch (conflictKind(*hashes)) {
        case ConflictKind::Submodule:
            // TODO: CSubmoduleResolveConflictDlg. git mergetool would stop on a prompt, with no terminal to answer it
            message = i18n("%1 is a submodule conflict, which can't be edited yet.", path);
            KMessageBox::error(parent, message, QString(), noNotify);
            return false;
        case ConflictKind::Content:
            startMergeTool(git, path, parent);
            return false;
        case ConflictKind::DeleteModify:
            editDeleteConflict(git, path, *hashes, parent);
            return true;
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git && e.error().category != ErrorCategory::Io) {
            throw;
        }
        KMessageBox::error(parent, e.error().message, QString(), noNotify);
    }
    return false;
}

} // namespace kgt
