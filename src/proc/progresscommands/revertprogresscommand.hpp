// SPDX-FileCopyrightText: 2009-2014, 2016, 2019, 2022-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressCommands/RevertProgressCommand.h: discards the local changes of
 * some files, putting them back to a revision (HEAD by default). TortoiseGit runs it in its progress dialog, which
 * is not ported: here it runs in the background and reports only its outcome.
 */

#pragma once

#include "gitstatuslistmodel.hpp"

#include "utils/error.hpp"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace kgt
{

class GitCall;

/**
 * Reverts files the way TortoiseGit does, with one git command per kind of change, each over every file of that
 * kind (CMassiveGitTask), in this order:
 *  1. an added file, or a renamed one whose new name is gone, is removed from the index and kept on disk
 *     (git rm -f --cached);
 *  2. a renamed file's new name is removed (git rm -f --ignore-unmatch);
 *  3. every other file, and a renamed file's old name, is restored from the revision (git checkout -f), which
 *     unstages it too and takes the revision's side of a conflict;
 *  4. a deleted file is added back to the index (git add -f).
 * A renamed submodule is moved back with git mv and checked out under its old name.
 *
 * TortoiseGit first moves the files to the recycle bin (RevertWithRecycleBin); that is not ported.
 */
class RevertProgressCommand : public QObject
{
    Q_OBJECT

public:
    /**
     * @param revertToRevision The revision to restore the files from.
     * @param parent The owning object.
     */
    explicit RevertProgressCommand(QString revertToRevision = QStringLiteral("HEAD"), QObject *parent = nullptr);
    ~RevertProgressCommand() override;

    RevertProgressCommand(const RevertProgressCommand&) = delete;
    RevertProgressCommand& operator=(const RevertProgressCommand&) = delete;
    RevertProgressCommand(RevertProgressCommand&&) = delete;
    RevertProgressCommand& operator=(RevertProgressCommand&&) = delete;

    /**
     * @param entries The files to revert, with their status (TortoiseGit's m_targetPathList).
     */
    void setPathList(const QList<GitStatusListEntry>& entries);

    /**
     * Starts reverting in the background (RevertProgressCommand::Run). finished() follows, even if nothing was
     * started.
     * @param workingTree The top of the working tree the entries' paths are relative to.
     */
    void run(const QString& workingTree);

    /**
     * @returns Whether the revert is running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @returns Why the last run failed, or nothing if it succeeded. The steps before the failing one have been
     *          done.
     */
    [[nodiscard]] const std::optional<Error>& error() const;

Q_SIGNALS:
    /**
     * The run has finished; error() says whether it worked.
     */
    void finished();

private:
    /**
     * One git command.
     */
    struct Step {
        QStringList arguments; // After "git"
        QByteArray input; // On stdin, for --pathspec-from-file=-
    };

    [[nodiscard]] std::optional<Error> prepare(const QString& workingTree);
    void runNextStep();
    void onStepFinished();
    void finish(std::optional<Error> error);

    QString m_revertToRevision;
    QList<GitStatusListEntry> m_entries;
    QString m_workingTree;
    QList<Step> m_steps; // Still to run, first first
    std::unique_ptr<GitCall> m_call;
    std::optional<Error> m_error;
    bool m_running = false;
};

} // namespace kgt
