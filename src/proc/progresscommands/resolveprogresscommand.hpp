// SPDX-FileCopyrightText: 2014, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressCommands/ResolveProgressCommand.h. TortoiseGit's CGitProgressDlg
 * is not ported, so this runs in ProgressDlg; its post-command buttons aren't either.
 */

#pragma once

#include "git/git.hpp"
#include "gitstatuslistmodel.hpp"

#include "utils/error.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace kgt
{

class GitCall;
class ProgressDlg;

/**
 * What to resolve a conflict with (TortoiseGit's ResolveWith).
 */
enum class ResolveWith {
    Current, // The file as it stands in the working tree
    Theirs, // The incoming version, stage 3
    Mine, // The current branch's version, stage 2
};

/**
 * Marks files as resolved. With ResolveWith::Current that is git add -f, and files that aren't unmerged are left alone
 * and reported as skipped. With Theirs or Mine, each file first takes that side: a side with no version of the file
 * removes it (git rm -f), and otherwise git checkout-index writes that version and git add -f marks it resolved.
 *
 * TortoiseGit reads each file's stages on the progress dialog's worker thread. ProgressDlg's commands are fixed when
 * it starts, so here they are read first, without blocking the UI: call readConflictInfo() and, after
 * conflictInfoRead(), setUp().
 */
class ResolveProgressCommand : public QObject
{
    Q_OBJECT

public:
    /**
     * @param resolveWith What to resolve the files with (m_resolveWith).
     * @param parent The owning object.
     */
    explicit ResolveProgressCommand(ResolveWith resolveWith = ResolveWith::Current, QObject *parent = nullptr);
    ~ResolveProgressCommand() override;

    ResolveProgressCommand(const ResolveProgressCommand&) = delete;
    ResolveProgressCommand& operator=(const ResolveProgressCommand&) = delete;
    ResolveProgressCommand(ResolveProgressCommand&&) = delete;
    ResolveProgressCommand& operator=(ResolveProgressCommand&&) = delete;

    /**
     * @param entries The files to resolve (m_targetPathList).
     */
    void setPathList(const QList<GitStatusListEntry>& entries);

    /**
     * Reads each file's stages with git ls-files -u -t -z, one file at a time, in the background. Needed for Theirs
     * and Mine only. conflictInfoRead() follows, even if there is nothing to read.
     * @param workingTree The top of the working tree the entries' paths are relative to.
     */
    void readConflictInfo(const QString& workingTree);

    /**
     * @returns Why the last readConflictInfo() failed, or nothing if it succeeded.
     */
    [[nodiscard]] const std::optional<Error>& error() const;

    /**
     * Gives the dialog its title, git commands and report (ResolveProgressCommand::Run). They run when it is shown.
     * @param workingTree The top of the working tree, where @p dialog runs git.
     * @param dialog The progress dialog; nothing may have been added to it.
     */
    void setUp(const QString& workingTree, ProgressDlg& dialog) const;

Q_SIGNALS:
    /**
     * readConflictInfo() has ended; error() says whether it worked.
     */
    void conflictInfoRead();

private:
    void readNextConflictInfo();
    void onLsFilesFinished();

    ResolveWith m_resolveWith;
    QList<GitStatusListEntry> m_targetPathList;
    QHash<QString, ConflictHashes> m_conflictInfo; // The stages of the files that are in conflict, by path
    QString m_workingTree; // Of the running readConflictInfo()
    qsizetype m_nextEntry = 0; // Index in m_targetPathList of the file to read next
    std::unique_ptr<GitCall> m_lsFilesCall;
    std::optional<Error> m_error;
};

} // namespace kgt
