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
#include <QHash>
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

    /**
     * @returns The full ref name, as CGit::GetMapHashToFriendName lists it: "refs/heads/main".
     */
    [[nodiscard]] QString fullName() const;

    /**
     * @param fullName A full ref name such as refs/tags/v1.
     * @returns The ref, not marked as current.
     */
    [[nodiscard]] static GitRef fromFullName(const QString& fullName);
};

using GitRefMap = QHash<QString, QList<GitRef>>; // Each commit's refs by full hash (CGitLogListBase::m_HashMap)

/**
 * What git log walks: the walk bits of CGit::GetLogCmd's mask (LOG_INFO_*) and its CFilterData date range.
 */
struct GitLogOptions {
    bool noMerges = false; // LOG_INFO_NO_MERGE
    bool firstParent = false; // LOG_INFO_FIRST_PARENT
    bool follow = false; // LOG_INFO_FOLLOW
    bool fullHistory = false; // LOG_INFO_FULL_HISTORY
    qint64 from = -1; // CFilterData::m_From: oldest commit time in seconds, applied when positive
    qint64 to = -1; // CFilterData::m_To: newest commit time in seconds, -1 for none

    bool operator==(const GitLogOptions&) const = default;
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
    QString committerName; // GitRev::m_CommitterName
    QString committerEmail; // GitRev::m_CommitterEmail
    QDateTime commitDate; // GitRev::m_CommitterDate, in local time
    QString subject; // GitRev::m_Subject: the message's first paragraph, joined into one line
    QString body; // GitRev::m_Body: the rest of the message, without trailing newlines
    QString notes; // GitRevLoglist::m_Notes: the commit's git notes, without trailing newlines
    QList<GitRef> refs; // The refs pointing at the commit, in git's order
    TGitPath::Actions actions; // GitRevLoglist::m_Action: what the commit did to its files; empty until known
    QList<GitRevFiles> files; // GitRevLoglist::m_Files: the changes against each parent, in parent order
    FilesState filesState = FilesState::NotLoaded; // Whether files and actions are known
    bool isBoundary = false; // GitRevLoglist::IsBoundary(): marked '-' by git log --boundary, which isn't used yet
    unsigned int rebaseAction = 0; // GitRevLoglist::m_RebaseAction: RebaseAction bits, set by the rebase dialog
    QString ref; // GitRevLoglist::m_Ref: a reflog entry's name, such as refs/stash@{1}
    QString refAction; // GitRevLoglist::m_RefAction: what made a reflog entry, such as "commit (amend)"

    bool operator==(const GitRevLoglist&) const = default;

    /**
     * @param paths Pathspecs relative to the working tree root that limit the log, or empty for the whole tree.
     * @param range The revisions to list, such as "upstream..branch" (CGit::GetLogCmd's range); empty for HEAD.
     * @param options What to walk (CGit::GetLogCmd's mask and date filter).
     * @returns The arguments after "git" for a log in TortoiseGit's default order (topological), in the format
     *          takeRecords() reads.
     */
    [[nodiscard]] static QStringList logArguments(const QStringList& paths = {}, const QString& range = {}, const GitLogOptions& options = {});

    /**
     * Ported from GitRevLoglist::GetSubjectBody. TortoiseGit's m_Body starts with the blank line after the subject.
     * @returns The commit's message, ending with a newline.
     */
    [[nodiscard]] QString subjectBody() const;

    /**
     * @param ref A ref or HEAD.
     * @returns The arguments after "git" of the command GitRevLoglist::GetRefLog runs.
     */
    [[nodiscard]] static QStringList refLogArguments(const QString& ref);

    /**
     * Ported from GitRevLoglist::GetRefLog. A line that isn't an entry, such as a signature check's, takes no
     * position, while the numbering keeps every entry git printed.
     * @param output What refLogArguments() printed.
     * @param ref The same ref.
     * @returns The entries, newest first, each named <ref>@{<n>}, with its message split at the first ": " into
     *          refAction and subject.
     */
    [[nodiscard]] static QList<GitRevLoglist> parseRefLog(const QByteArray& output, const QString& ref);

    /**
     * @returns The arguments after "git" of the command CGit::GetMapHashToFriendName runs, plus HEAD.
     */
    [[nodiscard]] static QStringList refMapArguments();

    /**
     * Ported from CGit::GetMapHashToFriendName, for CGitLogListBase::ReloadHashMap: the refs git log's decorations
     * would list for each commit, in the same order, so labels don't move when they are read again.
     * @param output What refMapArguments() printed.
     * @param currentBranch The current branch's short name (CGit::GetCurrentBranch), which is marked as current.
     * @returns Each commit's refs by full hash. A detached HEAD gets a GitRef::Type::Head entry.
     */
    [[nodiscard]] static GitRefMap parseRefMap(const QByteArray& output, const QString& currentBranch);

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

    /**
     * The batched form of GitRevLoglist::SafeGetSimpleList, for the log filter's Paths field: one git process for
     * many commits instead of one per commit. Git reads the commits' hashes on stdin, one per line.
     * @returns The arguments after "git" for a diff-tree that parseSimpleList() reads.
     */
    [[nodiscard]] static QStringList simpleListArguments();

    /**
     * @param output The stdout of git with simpleListArguments().
     * @returns Each commit's changed paths against every parent, by hash (m_SimpleFileList).
     */
    [[nodiscard]] static QHash<QString, QStringList> parseSimpleList(const QByteArray& output);

    /**
     * Reads every annotated tag in one git process, where CGitLogListBase::GetTagInfo asks for one at a time.
     * @returns The arguments after "git" for a for-each-ref that parseTagInfo() reads.
     */
    [[nodiscard]] static QStringList tagInfoArguments();

    /**
     * @param output The stdout of git with tagInfoArguments().
     * @returns The text of each annotated tag as CGit::GetTagInfo writes it, by full ref name.
     */
    [[nodiscard]] static QHash<QString, QString> parseTagInfo(const QByteArray& output);
};

} // namespace kgt
