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
 * The files a commit changed against one of its parents: their status from the raw diff, and their line counts.
 */
struct GitRevFiles {
    QList<GitStatusEntry> status; // One entry per file, in git's order, with its TGitPath actions
    QList<NumStatEntry> numStat; // The line counts, matched to status by path

    bool operator==(const GitRevFiles&) const = default;
};

/**
 * One commit of the log. As in TortoiseGit, its files and actions are loaded later, in the background
 * (GitRevLoglist::GetFiles and CGitLogListBase::AsyncDiffThread).
 */
struct GitRevLoglist {
    /**
     * Whether the commit's files are known (GitRevLoglist::m_IsDiffFiles).
     */
    enum class FilesState {
        NotLoaded, // Not fetched yet
        Loaded, // files and actions are set
        Failed, // git diff-tree failed
    };

    /**
     * What the rebase dialog does with the commit, and how far it got: CGitLogListBase's LOGACTIONS_REBASE_*, with
     * the same values. One of Pick, Squash, Edit and Skip is the mode; Current and Done are added as it runs.
     */
    enum RebaseAction : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
        RebaseCurrent = 0x08000000, // LOGACTIONS_REBASE_CURRENT: being rebased now
        RebasePick = 0x04000000, // LOGACTIONS_REBASE_PICK
        RebaseSquash = 0x02000000, // LOGACTIONS_REBASE_SQUASH: squashed into the commit below
        RebaseEdit = 0x01000000, // LOGACTIONS_REBASE_EDIT: stop to amend it
        RebaseDone = 0x00800000, // LOGACTIONS_REBASE_DONE
        RebaseSkip = 0x00400000, // LOGACTIONS_REBASE_SKIP: left out
        RebaseMask = 0x0FC00000, // LOGACTIONS_REBASE_MASK
        RebaseModeMask = 0x07C00000, // LOGACTIONS_REBASE_MODE_MASK
    };

    QString hash; // The full commit hash
    QStringList parents; // The parents' hashes, first parent first; empty for a root commit
    QString authorName; // GitRev::m_AuthorName
    QString authorEmail; // GitRev::m_AuthorEmail
    QDateTime authorDate; // GitRev::m_AuthorDate, in local time
    QString subject; // GitRev::m_Subject: the message's first paragraph, joined into one line
    QString body; // GitRev::m_Body: the rest of the message, without trailing newlines
    QList<GitRef> refs; // The refs pointing at the commit, in git's order
    TGitPath::Actions actions; // GitRevLoglist::m_Action: what the commit did to its files; empty until known
    QList<GitRevFiles> files; // GitRevLoglist::m_Files: the changes against each parent, in parent order
    FilesState filesState = FilesState::NotLoaded; // Whether files and actions are known
    unsigned int rebaseAction = 0; // GitRevLoglist::m_RebaseAction: RebaseAction bits, set by the rebase dialog

    bool operator==(const GitRevLoglist&) const = default;

    /**
     * @param paths Pathspecs relative to the working tree root that limit the log, or empty for the whole tree.
     * @param range The revisions to list, such as "upstream..branch" (CGit::GetLogCmd's range); empty for HEAD.
     * @param noMerges Whether to leave merge commits out (CGit::LOG_INFO_NO_MERGE).
     * @returns The arguments after "git" for a log in TortoiseGit's default order (topological), in the format
     *          takeRecords() reads.
     */
    [[nodiscard]] static QStringList logArguments(const QStringList& paths = {}, const QString& range = {}, bool noMerges = false);

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
     * @returns How many diffs make up the commit's files: one per parent, and one for a root commit
     *          (GitRevLoglist::SafeFetchFullInfo diffs a merge against each of its parents).
     */
    [[nodiscard]] qsizetype diffCount() const;

    /**
     * @param rev A commit.
     * @param parentNo Which parent to compare with, counting from 0 (TortoiseGit's m_ParentNo); ignored for a
     *                 root commit, which is compared with nothing.
     * @returns The arguments after "git" for a diff-tree that parseFiles() reads.
     */
    [[nodiscard]] static QStringList filesArguments(const GitRevLoglist& rev, qsizetype parentNo);

    /**
     * @param output The stdout of git with filesArguments().
     * @returns The changed files, or a Parse error naming the first bad record.
     */
    [[nodiscard]] static Result<GitRevFiles> parseFiles(const QByteArray& output);
};

} // namespace kgt
