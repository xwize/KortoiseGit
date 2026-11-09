// SPDX-FileCopyrightText: 2009-2016, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/ProgressCommands/ResolveProgressCommand.cpp

#include "progresscommands/resolveprogresscommand.hpp"

#include "adddlg.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "git/tgitpath.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <utility>

namespace
{

using namespace kgt;

/**
 * The paths of each git command of a resolve (ResolveProgressCommand::Run's CMassiveGitTask objects).
 */
struct ResolveTasks {
    QStringList remove; // git rm -f
    QStringList checkoutIndex; // git checkout-index -f --stage=<n>
    QStringList add; // git add -f
    QStringList skipped; // Reported, and left alone
};

/**
 * The ResolveWith::Current branch of ResolveProgressCommand::Run.
 * @param entries The files to resolve.
 * @returns What to do with them.
 */
ResolveTasks currentTasks(const QList<GitStatusListEntry>& entries)
{
    ResolveTasks tasks;
    for (const GitStatusListEntry& entry : entries) {
        (entry.actions & TGitPath::Action::Unmerged ? tasks.add : tasks.skipped).append(entry.path);
    }
    return tasks;
}

/**
 * Sorts one file into the tasks that give it the chosen side.
 * @param theirs Whether theirs was chosen, rather than mine.
 * @param root The working tree.
 * @param path The file, relative to the root.
 * @param hashes Its stages.
 * @param[in,out] tasks Where it goes.
 */
void addSideTask(bool theirs, const QDir& root, const QString& path, const ConflictHashes& hashes, ResolveTasks& tasks)
{
    if ((theirs ? hashes.remoteHash : hashes.localHash).isEmpty()) {
        const QFileInfo onDisk(root.filePath(path));
        if (onDisk.isDir() && !onDisk.isSymLink()) {
            // TODO: The side keeping a submodule was not chosen: git rm -f, asking before deleting what it refuses to
            tasks.skipped.append(path);
        } else {
            tasks.remove.append(path);
        }
        return;
    }
    if (!(theirs ? hashes.remoteIsFile : hashes.localIsFile)) {
        // TODO: targetWillBeAFile: update-index --cacheinfo for an uninitialised submodule, otherwise GitReset in it
        tasks.skipped.append(path);
        return;
    }
    tasks.checkoutIndex.append(path);
    tasks.add.append(path);
}

/**
 * @param action What happened to the files, CGitProgressList's action column.
 * @param paths The files.
 * @returns One line for each file.
 */
QString notifyLines(const QString& action, const QStringList& paths)
{
    QString lines;
    for (const QString& path : paths) {
        lines += QStringLiteral("%1  %2\n").arg(action, path);
    }
    return lines;
}

} // namespace

namespace kgt
{

ResolveProgressCommand::ResolveProgressCommand(ResolveWith resolveWith, QObject *parent)
    : QObject(parent)
    , m_resolveWith(resolveWith)
{
}

ResolveProgressCommand::~ResolveProgressCommand() = default;

void ResolveProgressCommand::setPathList(const QList<GitStatusListEntry>& entries)
{
    m_targetPathList = entries;
}

void ResolveProgressCommand::readConflictInfo(const QString& workingTree)
{
    Q_ASSERT(!m_lsFilesCall);
    m_workingTree = workingTree;
    m_conflictInfo.clear();
    m_error.reset();
    m_nextEntry = 0;
    readNextConflictInfo();
}

const std::optional<Error>& ResolveProgressCommand::error() const
{
    return m_error;
}

void ResolveProgressCommand::readNextConflictInfo()
{
    if (m_nextEntry >= m_targetPathList.size()) {
        Q_EMIT conflictInfoRead();
        return;
    }
    const QStringList arguments{
        QStringLiteral("--literal-pathspecs"),
        QStringLiteral("ls-files"),
        QStringLiteral("-u"),
        QStringLiteral("-t"),
        QStringLiteral("-z"),
        QStringLiteral("--"),
        m_targetPathList.at(m_nextEntry).path,
    };
    m_lsFilesCall = Git(m_workingTree).runAsync(arguments, RunMode::ReadOnly);
    connect(m_lsFilesCall.get(), &GitCall::finished, this, &ResolveProgressCommand::onLsFilesFinished);
}

void ResolveProgressCommand::onLsFilesFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_lsFilesCall.release();
    call->deleteLater();
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        m_error = e.error();
        Q_EMIT conflictInfoRead();
        return;
    }
    // Empty output: CGit::ParseConflictHashesFromLsFile's 1, no longer in conflict, which setUp() reports as skipped
    if (!output.isEmpty()) {
        Result<ConflictHashes> hashes = Git::parseConflictHashesFromLsFile(output);
        if (!hashes) {
            m_error = hashes.error();
            Q_EMIT conflictInfoRead();
            return;
        }
        m_conflictInfo.insert(m_targetPathList.at(m_nextEntry).path, *hashes);
    }
    ++m_nextEntry;
    readNextConflictInfo();
}

void ResolveProgressCommand::setUp(const QString& workingTree, ProgressDlg& dialog) const
{
    const QDir root(workingTree);
    QStringList absolutePaths;
    for (const GitStatusListEntry& entry : m_targetPathList) {
        absolutePaths.append(root.filePath(entry.path));
    }

    // CGitProgressList::SetWindowTitle: IDS_PROGRS_TITLE_RESOLVE on the paths' common root
    const QString commonRoot = QDir::toNativeSeparators(AddDlg::commonRoot(absolutePaths));
    dialog.setWindowTitle(QStringLiteral("%1 - %2 - %3").arg(commonRoot, i18n("Resolve"), i18n("KortoiseGit")));

    const bool theirs = m_resolveWith == ResolveWith::Theirs;
    ResolveTasks tasks;
    if (m_resolveWith == ResolveWith::Current) {
        tasks = currentTasks(m_targetPathList);
    } else {
        for (const GitStatusListEntry& entry : m_targetPathList) {
            if (const auto hashes = m_conflictInfo.constFind(entry.path); hashes != m_conflictInfo.cend()) {
                addSideTask(theirs, root, entry.path, *hashes, tasks);
            } else {
                tasks.skipped.append(entry.path);
            }
        }
    }

    const int destinationStage = theirs ? 3 : 2;
    const QString stageOption = QStringLiteral("--stage=%1").arg(destinationStage);
    const QString force = QStringLiteral("-f");
    const auto addTask = [&dialog](const QStringList& params, const QStringList& paths) {
        MassiveGitTask task(params);
        task.setPaths(paths);
        if (!task.isEmpty()) {
            dialog.addGitCmd(task.arguments(), task.pathSpecFile());
        }
    };
    addTask({QStringLiteral("rm"), force}, tasks.remove);
    addTask({QStringLiteral("checkout-index"), force, stageOption}, tasks.checkoutIndex);
    addTask({QStringLiteral("add"), force}, tasks.add);

    // CGitProgressList::AddNotify's lines, in the log: ProgressDlg has no list of files
    const QStringList resolved = tasks.remove + tasks.add;
    dialog.setPostExecCallback([resolved, skipped = tasks.skipped](int& exitCode, QString& extraMsg) {
        // Every path was skipped: ProgressDlg calls having no command a failure, CMassiveGitTask an empty success
        if (resolved.isEmpty()) {
            exitCode = 0;
        }
        extraMsg = notifyLines(i18n("Skipped"), skipped);
        if (exitCode == 0) {
            extraMsg += notifyLines(i18n("Resolved"), resolved);
            // ResolveProgressCommand::ShowInfo
            extraMsg += i18n("You need commit your change after resolve conflict") + QLatin1Char('\n');
        }
    });
}

} // namespace kgt
