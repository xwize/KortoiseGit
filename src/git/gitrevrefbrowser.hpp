// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitRevRefBrowser.h: one ref as the Browse References dialog shows it, read with a
 * single git for-each-ref.
 */

#pragma once

#include "utils/result.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * The kind of object a ref points at, as git_object_t.
 */
enum class GitObjectType {
    Unknown, // Anything else, such as a blob
    Commit, // GIT_OBJECT_COMMIT
    Tag, // GIT_OBJECT_TAG: an annotated tag
    Tree, // GIT_OBJECT_TREE
};

/**
 * Which refs GetGitRevRefMap lists, TortoiseGit's merge filter combo box.
 */
enum class RefMergeFilter {
    All, // Every ref
    Merged, // --merged HEAD
    Unmerged, // --no-merged HEAD
};

/**
 * What the repository's configuration says for the ref browser: its remotes and its branches' descriptions.
 */
struct RefBrowserConfig {
    QStringList remotes; // CGit::GetRemoteList, sorted
    QHash<QString, QString> descriptions; // CGit::GetBranchDescriptions: branch.<name>.description by branch name
};

/**
 * One ref, TortoiseGit's GitRevRefBrowser. For an annotated tag the subject, author and committer are the tag's.
 */
struct GitRevRefBrowser {
    QString hash; // m_CommitHash: the object the ref points at (the tag object for an annotated tag)
    GitObjectType objectType = GitObjectType::Unknown; // m_objectType
    QString upstreamRef; // m_UpstreamRef: the full upstream ref, such as refs/remotes/origin/main
    bool upstreamGone = false; // The upstream ref no longer exists
    QString subject; // GetSubject()
    QString authorName; // GetAuthorName()
    QDateTime authorDate; // GetAuthorDate(); invalid if unknown
    QString committerName; // GetCommitterName()
    QDateTime committerDate; // GetCommitterDate(); invalid if unknown
    QString description; // m_Description: branch.<name>.description, for a local branch

    /**
     * The git for-each-ref that lists every ref, GetGitRevRefMap's command line. Fields are NUL-separated.
     * @param filter Which refs to list.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList forEachRefArguments(RefMergeFilter filter);

    /**
     * Parses forEachRefArguments()'s output, GetGitRevRefMap.
     * @param output What git for-each-ref printed.
     * @param descriptions The branch descriptions, by branch name.
     * @returns The refs by full name, such as refs/heads/main, or a Parse error.
     */
    [[nodiscard]] static Result<QMap<QString, GitRevRefBrowser>> parseRefMap(const QByteArray& output, const QHash<QString, QString>& descriptions);

    /**
     * The git config call that reads the remotes and the branch descriptions in one go. It exits with 1 when none is
     * set, which means an empty result.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList configArguments();

    /**
     * Parses configArguments()'s output.
     * @param output What git config printed.
     * @returns The remotes and descriptions.
     */
    [[nodiscard]] static RefBrowserConfig parseConfig(const QByteArray& output);
};

} // namespace kgt
