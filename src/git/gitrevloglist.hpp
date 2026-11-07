// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitRevLoglist.h (GitRevLoglist), src/Git/GitRev.h (GitRev) and the log command
 * line of CGit::GetLogCmd: one commit as the log dialog shows it, read from git log instead of gitdll, and the
 * files it changed, read from git diff-tree.
 */

#pragma once

#include "git/gitnumstat.hpp"
#include "git/gitstatus.hpp"
#include "git/tgitpath.hpp"
#include "utils/result.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * A ref shown as a label on its commit, as TortoiseGit's log list draws them (CGitLogListBase::m_HashMap).
 */
struct GitRef {
    /**
     * What kind of ref it is; picks the label's colour (CColors::CurrentBranch, LocalBranch, ...).
     */
    enum class Type {
        Head, // A detached HEAD
        LocalBranch, // refs/heads/
        RemoteBranch, // refs/remotes/
        Tag, // refs/tags/
        Stash, // refs/stash
        Other, // Any other ref, such as refs/notes/ or refs/bisect/
    };

    Type type = Type::Other; // What kind of ref it is
    QString name; // The short name shown: "main", "origin/main", "v1.0"; the full name for Other
    bool isCurrent = false; // HEAD points at it: the current branch, or a detached HEAD

    bool operator==(const GitRef&) const = default;
};

/**
 * The files a commit changed against its first parent: their status from the raw diff, and their line counts.
 */
struct GitRevFiles {
    QList<GitStatusEntry> status; // One entry per file, in git's order, with its TGitPath actions
    QList<NumStatEntry> numStat; // The line counts, matched to status by path
};

/**
 * One commit of the log. TortoiseGit loads the files and actions of a commit later and on demand; so does the
 * log dialog, which sets actions once it has them.
 */
struct GitRevLoglist {
    QString hash; // The full commit hash
    QStringList parents; // The parents' hashes, first parent first; empty for a root commit
    QString authorName; // GitRev::m_AuthorName
    QString authorEmail; // GitRev::m_AuthorEmail
    QDateTime authorDate; // GitRev::m_AuthorDate, in local time
    QString subject; // GitRev::m_Subject: the message's first paragraph, joined into one line
    QString body; // GitRev::m_Body: the rest of the message, without trailing newlines
    QList<GitRef> refs; // The refs pointing at the commit, in git's order
    TGitPath::Actions actions; // GitRevLoglist::m_Action: what the commit did to its files; empty until known

    bool operator==(const GitRevLoglist&) const = default;

    /**
     * @param paths Pathspecs relative to the working tree root that limit the log, or empty for the whole tree.
     * @returns The arguments after "git" for a log of HEAD in TortoiseGit's default order (topological), in the
     *          format takeRecords() reads.
     */
    [[nodiscard]] static QStringList logArguments(const QStringList& paths = {});

    /**
     * Parses the complete records at the start of git log's output, so the log can be shown while git still
     * writes it. An incomplete record at the end is left in the buffer for the next call.
     * @param[in,out] buffer Output of git with logArguments(); the parsed records are removed from its start.
     * @returns The commits, in git's order, or a Parse error naming the first bad record.
     */
    [[nodiscard]] static Result<QList<GitRevLoglist>> takeRecords(QByteArray& buffer);

    /**
     * Parses git's %D decoration as git log --decorate=full prints it.
     * @param decoration Such as "HEAD -> refs/heads/main, tag: refs/tags/v1.0, refs/remotes/origin/main".
     * @returns The refs, in the same order.
     */
    [[nodiscard]] static QList<GitRef> parseRefs(const QString& decoration);

    /**
     * @param rev A commit.
     * @returns The arguments after "git" for a diff-tree of the commit against its first parent (or against
     *          nothing for a root commit) that parseFiles() reads.
     */
    [[nodiscard]] static QStringList filesArguments(const GitRevLoglist& rev);

    /**
     * @param output The stdout of git with filesArguments().
     * @returns The changed files, or a Parse error naming the first bad record.
     */
    [[nodiscard]] static Result<GitRevFiles> parseFiles(const QByteArray& output);
};

} // namespace kgt
