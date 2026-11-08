// SPDX-FileCopyrightText: 2008-2018, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitStatus.h: the status of every changed, untracked or ignored file in a
 * working tree. TortoiseGit reads the index through libgit2; here it comes from
 * git status --porcelain=v2 -z, which keeps paths with spaces, newlines or unicode intact.
 */

#pragma once

#include "git/git.hpp"
#include "git/tgitpath.hpp"
#include "utils/result.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * One side of a change: what the index holds against HEAD, or what the working tree holds against the index.
 * The letters are git's porcelain status letters.
 */
enum class FileState {
    Unmodified, // '.'
    Modified, // 'M'
    TypeChanged, // 'T'
    Added, // 'A'
    Deleted, // 'D'
    Renamed, // 'R'
    Copied, // 'C'
    Unmerged, // 'U': the conflicting side of a conflict
};

/**
 * Which kind of porcelain v2 record an entry came from.
 */
enum class GitStatusKind {
    Changed, // '1': an ordinary change
    RenamedOrCopied, // '2': a rename or copy, with a source path
    Unmerged, // 'u': a conflict
    Untracked, // '?'
    Ignored, // '!'
};

/**
 * The status of one path in a working tree.
 */
struct GitStatusEntry {
    GitStatusKind kind = GitStatusKind::Changed; // Which record it came from
    QString path; // Relative to the working tree root, '/'-separated
    QString origPath; // The rename or copy source, relative to the root; empty otherwise
    FileState index = FileState::Unmodified; // The index against HEAD
    FileState worktree = FileState::Unmodified; // The working tree against the index
    TGitPath::Actions actions; // TortoiseGit's LOGACTIONS_* flags for both sides together

    bool operator==(const GitStatusEntry&) const = default;
};

/**
 * Whether a status lists ignored files.
 */
enum class IgnoredFiles {
    Exclude,
    Include, // As git status --ignored=matching
};

/**
 * Reads a working tree's status. For the app, run arguments() with Git::runAsync() and parse() the output;
 * the plugin calls status(), which blocks.
 */
class GitStatus
{
public:
    /**
     * @param ignored Whether to list ignored files.
     * @returns The git arguments for a status that parse() understands.
     */
    [[nodiscard]] static QStringList arguments(IgnoredFiles ignored);

    /**
     * @param output The stdout of git with arguments().
     * @returns An entry for each path, in git's order, or a Parse error naming the first record that isn't
     *          porcelain v2.
     */
    [[nodiscard]] static Result<QList<GitStatusEntry>> parse(const QByteArray& output);

    /**
     * The index against another commit than HEAD, as CGit::GetWorkingTreeChanges compares with HEAD~1 when
     * amending. git status only compares with HEAD, so this diff replaces its index column (withIndexAgainst()).
     * @param base The commit to compare the index with, such as "HEAD~1".
     * @returns The git arguments for a diff that withIndexDiff() understands.
     */
    [[nodiscard]] static QStringList indexDiffArguments(const QString& base);

    /**
     * Puts a status against another commit: the index column comes from the diff, while the working tree column,
     * conflicts, untracked and ignored files stay as they are. A file that differs from neither is dropped, and one
     * the diff has but the status doesn't is added. Tracked files come first, sorted by path, as git status lists
     * them.
     * @param status The status, against HEAD.
     * @param indexDiffOutput The stdout of git with indexDiffArguments().
     * @returns The status against the other commit, or a Parse error if the diff can't be read.
     */
    [[nodiscard]] static Result<QList<GitStatusEntry>> withIndexDiff(const QList<GitStatusEntry>& status, const QByteArray& indexDiffOutput);

    /**
     * Runs git status and waits for it, without taking optional locks.
     * @param git Runs in the working tree.
     * @param ignored Whether to list ignored files.
     * @returns An entry for each changed, untracked (and if asked, ignored) path.
     * @throws RuntimeError (category Git) if git fails, or (category Parse) if its output can't be read.
     */
    [[nodiscard]] static QList<GitStatusEntry> status(const Git& git, IgnoredFiles ignored = IgnoredFiles::Exclude);
};

} // namespace kgt
