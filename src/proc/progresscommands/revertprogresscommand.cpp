// SPDX-FileCopyrightText: 2009-2014, 2016, 2019, 2022-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/ProgressCommands/RevertProgressCommand.cpp

#include "progresscommands/revertprogresscommand.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

/**
 * @param entry A file to revert.
 * @returns Whether it is a rename with a known old name.
 */
bool isRename(const GitStatusListEntry& entry)
{
    return (entry.actions & Action::Replaced) && !entry.origPath.isEmpty();
}

/**
 * The paths of each git command of a revert (RevertProgressCommand::Run's CMassiveGitTask objects).
 */
struct RevertTasks {
    QStringList unstage; // git rm -f --cached
    QStringList remove; // git rm -f --ignore-unmatch
    QStringList checkout; // git checkout -f <revision>
    QStringList add; // git add -f
    QList<const GitStatusListEntry *> moves; // Renamed submodules, moved back with git mv

    /**
     * Adds a renamed file.
     * @param root The working tree.
     * @param entry The file; it must outlive the tasks.
     * @returns An error if the rename can't be reverted.
     */
    [[nodiscard]] std::optional<Error> addRename(const QDir& root, const GitStatusListEntry& entry)
    {
        if (QFileInfo(root.filePath(entry.origPath)).isDir()) {
            return Error{
                .category = ErrorCategory::Git,
                .message = i18n("Revert failed:\nCannot revert renaming of \"%1\". A directory with the old name \"%2\" exists.", entry.path, entry.origPath)};
        }
        const QFileInfo renamed(root.filePath(entry.path));
        if (!renamed.exists()) {
            unstage.append(entry.path);
            // TortoiseGit checks out the new name here, which HEAD doesn't have: the old name is what comes back
            checkout.append(entry.origPath);
        } else if (renamed.isDir()) {
            moves.append(&entry);
        } else {
            remove.append(entry.path);
            checkout.append(entry.origPath);
        }
        return std::nullopt;
    }
};

} // namespace

namespace kgt
{

RevertProgressCommand::RevertProgressCommand(QString revertToRevision, QObject *parent)
    : QObject(parent)
    , m_revertToRevision(std::move(revertToRevision))
{
}

RevertProgressCommand::~RevertProgressCommand() = default;

void RevertProgressCommand::setPathList(const QList<GitStatusListEntry>& entries)
{
    m_entries = entries;
}

void RevertProgressCommand::run(const QString& workingTree)
{
    Q_ASSERT(!m_running);
    m_running = true;
    m_error.reset();
    m_workingTree = workingTree;
    if (std::optional<Error> error = prepare(workingTree)) {
        finish(std::move(error));
        return;
    }
    runNextStep();
}

bool RevertProgressCommand::isRunning() const
{
    return m_running;
}

const std::optional<Error>& RevertProgressCommand::error() const
{
    return m_error;
}

std::optional<Error> RevertProgressCommand::prepare(const QString& workingTree)
{
    m_steps.clear();
    const QDir root(workingTree);
    RevertTasks tasks;
    for (const GitStatusListEntry& entry : std::as_const(m_entries)) {
        if (isRename(entry)) {
            // Its old name comes back with checkout. TortoiseGit also adds a deleted rename's new name, which
            // fails, there being no such file
            if (std::optional<Error> error = tasks.addRename(root, entry)) {
                return error;
            }
            continue;
        }
        if (entry.actions & Action::Added) {
            tasks.unstage.append(entry.path);
        } else {
            tasks.checkout.append(entry.path);
        }
        if (entry.actions & Action::Deleted) {
            tasks.add.append(entry.path);
        }
    }

    const auto addTask = [this](const QStringList& params, const QStringList& paths) {
        MassiveGitTask task(params);
        task.setPaths(paths);
        if (!task.isEmpty()) {
            m_steps.append(Step{.arguments = task.arguments(), .input = task.pathSpecFile()});
        }
    };
    addTask({QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("--cached")}, tasks.unstage);
    // TortoiseGit runs git rm --ignore-unmatch after moving the files to the recycle bin, which is not ported:
    // with the renamed file still there git would refuse, its new name being staged, so force it
    addTask({QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("--ignore-unmatch")}, tasks.remove);
    addTask({QStringLiteral("checkout"), QStringLiteral("-f"), QStringLiteral("--end-of-options"), m_revertToRevision}, tasks.checkout);
    addTask({QStringLiteral("add"), QStringLiteral("-f")}, tasks.add);

    for (const GitStatusListEntry *entry : std::as_const(tasks.moves)) {
        QStringList move{QStringLiteral("mv")};
        // If the names differ only in case, git mv needs -f
        if (entry->path.compare(entry->origPath, Qt::CaseInsensitive) == 0) {
            move.append(QStringLiteral("-f"));
        }
        move.append({QStringLiteral("--"), entry->path, entry->origPath});
        m_steps.append(Step{.arguments = move, .input = {}});
        m_steps.append(Step{.arguments = {QStringLiteral("checkout"),
                                          QStringLiteral("-f"),
                                          QStringLiteral("--end-of-options"),
                                          m_revertToRevision,
                                          QStringLiteral("--"),
                                          entry->origPath},
                            .input = {}});
    }
    return std::nullopt;
}

void RevertProgressCommand::runNextStep()
{
    if (m_steps.isEmpty()) {
        finish(std::nullopt);
        return;
    }
    const Step step = m_steps.takeFirst();
    m_call = Git(m_workingTree).runAsync(step.arguments, RunMode::ReadWrite, step.input);
    connect(m_call.get(), &GitCall::finished, this, &RevertProgressCommand::onStepFinished);
}

void RevertProgressCommand::onStepFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_call.release();
    call->deleteLater();
    try {
        std::ignore = call->output();
    } catch (const RuntimeError& e) {
        m_steps.clear();
        finish(e.error());
        return;
    }
    runNextStep();
}

void RevertProgressCommand::finish(std::optional<Error> error)
{
    m_error = std::move(error);
    m_running = false;
    Q_EMIT finished();
}

} // namespace kgt
