// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/Git.h (CGit): runs command-line git in one working tree. This is the git
 * layer's entry point, shared by the Dolphin plugin and kortoisegitproc.
 */

#pragma once

#include "git/gitcall.hpp"
#include "utils/result.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace kgt
{

/**
 * Which branches a branch list holds, CGit's BRANCH_TYPE.
 */
enum BranchType : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
    BranchLocal = 1, // BRANCH_LOCAL
    BranchRemote = 2, // BRANCH_REMOTE: listed as remotes/<remote>/<branch>
    BranchFetchHead = 4, // BRANCH_FETCH_HEAD: FETCH_HEAD, if it names one commit to merge
    BranchAll = BranchLocal | BranchRemote, // BRANCH_ALL
    BranchAllF = BranchAll | BranchFetchHead, // BRANCH_ALL_F
};

/**
 * A sorted branch list and which entry is the current branch, as CGit::GetBranchList returns them.
 */
struct BranchList {
    QStringList branches; // Local first, then remotes/..., each part in logical order
    int current = -1; // Index of the current branch, or -1 (detached HEAD, or skipped)
};

/**
 * What the index holds for an unmerged file, the out-parameters of CGit::ParseConflictHashesFromLsFile.
 */
struct ConflictHashes {
    QString baseHash; // Stage 1, empty if the common ancestor has no such file
    QString localHash; // Stage 2, empty if our side has no such file
    QString remoteHash; // Stage 3, empty if their side has no such file
    bool baseIsFile = true; // False for a submodule (mode 160000)
    bool localIsFile = true; // False for a submodule
    bool remoteIsFile = true; // False for a submodule
};

/**
 * Runs git in one directory, either blocking (for the plugin, which Dolphin calls on a worker thread) or
 * asynchronously (for the app, whose UI thread must never wait on git).
 */
class Git
{
public:
    /**
     * @param workingDirectory The directory git runs in: a working tree or any directory inside one.
     */
    explicit Git(QString workingDirectory);

    /**
     * @returns The directory git runs in.
     */
    [[nodiscard]] const QString& workingDirectory() const;

    /**
     * Limits how long run() waits, for callers that mustn't hang: a git that is stuck is killed and reported as a
     * failure.
     * @param timeoutMs The limit; negative for none, the default.
     */
    void setRunTimeout(int timeoutMs);

    /**
     * Runs git and waits for it to exit.
     * @param arguments The arguments after "git".
     * @param mode Whether git may take the repository's optional locks.
     * @param input What git reads on stdin; empty for none.
     * @returns Everything git wrote to stdout.
     * @throws RuntimeError (category Git) if git fails to start or exits non-zero.
     */
    [[nodiscard]] QByteArray run(const QStringList& arguments, RunMode mode = RunMode::ReadWrite, const QByteArray& input = {}) const;

    /**
     * Starts git without waiting for it.
     * @param arguments The arguments after "git".
     * @param mode Whether git may take the repository's optional locks.
     * @param input What git reads on stdin; empty for none.
     * @returns The running call: connect to its finished() signal, then read its output().
     */
    [[nodiscard]] std::unique_ptr<GitCall> runAsync(const QStringList& arguments, RunMode mode = RunMode::ReadWrite, const QByteArray& input = {}) const;

    /**
     * Finds the top of the working tree holding a path, with git rev-parse --show-toplevel.
     * @param path A file or directory inside a working tree.
     * @returns The working tree's root directory, as git reports it (symbolic links resolved).
     * @throws RuntimeError (category Git) if the path is not inside a working tree.
     */
    [[nodiscard]] static QString workingTreeRoot(const QString& path);

    /**
     * @param path A path in a working tree or a bare repository.
     * @returns The working tree's root holding it, or, for a bare repository, which has none, the path itself.
     */
    [[nodiscard]] static QString repositoryRoot(const QString& path);

    /**
     * Reads one field of git's -z style output.
     * @param output The whole output.
     * @param[in,out] pos Where the field starts; moved past the delimiter.
     * @param delimiter What ends the field.
     * @returns The field, or nothing if the delimiter is missing or the field is empty.
     */
    [[nodiscard]] static std::optional<QString> takeField(const QByteArray& output, qsizetype& pos, char delimiter);

    /**
     * Ported from CGit::StripRefName.
     * @param ref A ref such as refs/heads/main or refs/tags/v1.
     * @returns Its short name, such as main or tags/v1.
     */
    [[nodiscard]] static QString stripRefName(const QString& ref);

    /**
     * @param text The text to check.
     * @returns Whether it is a full commit hash (SHA-1 or SHA-256) in git's lower-case hex.
     */
    [[nodiscard]] static bool isHash(QByteArrayView text);

    /**
     * Ported from CGit::ParseConflictHashesFromLsFile.
     * @param output What git ls-files -u -t -z printed for one file.
     * @returns That file's stages, or an error if the output is empty (the file isn't in conflict) or malformed.
     */
    [[nodiscard]] static Result<ConflictHashes> parseConflictHashesFromLsFile(const QByteArray& output);

    /**
     * Ported from CGit::GuessRefForHash: a friendly name for a commit, preferring a branch, then a remote branch,
     * then a tag.
     * @param hash A full commit hash.
     * @returns The ref's short name, or nothing if no ref points at the commit.
     * @throws RuntimeError (category Git) if git fails.
     */
    [[nodiscard]] std::optional<QString> guessRefForHash(const QString& hash) const;

    /**
     * Ported from CGit::HasWorkingTreeConflicts: whether the index holds unmerged entries, with git ls-files -u.
     * @returns True if a file is in conflict.
     * @throws RuntimeError (category Git) if git fails.
     */
    [[nodiscard]] bool hasWorkingTreeConflicts() const;

    /**
     * Ported from CGit::IsUnbornBranch, for HEAD.
     * @returns Whether HEAD names a branch that has no commit yet.
     */
    [[nodiscard]] bool isUnbornBranch() const;

    /**
     * Ported from CGit::GetConfigValue.
     * @param name A configuration key, such as merge.log.
     * @returns Its value, or an empty string if it isn't set.
     * @throws RuntimeError (category Git) if git fails for another reason.
     */
    [[nodiscard]] QString configValue(const QString& name) const;

    /**
     * Ported from CGit::SetConfigValue, for the repository's own configuration.
     * @param name A configuration key, such as branch.main.description.
     * @param value Its new value.
     * @throws RuntimeError (category Git) if git fails.
     */
    void setConfigValue(const QString& name, const QString& value) const;

    /**
     * Ported from CGit::UnsetConfigValue, for the repository's own configuration.
     * @param name A configuration key.
     * @throws RuntimeError (category Git) if git fails, which includes the key not being set.
     */
    void unsetConfigValue(const QString& name) const;

    /**
     * Ported from CGit::GetCurrentBranch.
     * @returns The current branch's name, or "(no branch)" for a detached HEAD.
     */
    [[nodiscard]] QString currentBranch() const;

    /**
     * Ported from CGit::DeleteRef's git command line.
     * @param reference A full ref name under refs/heads/, refs/tags/ or refs/remotes/.
     * @throws RuntimeError (category Git) if git fails or the ref is of another kind.
     */
    void deleteRef(const QString& reference) const;

    /**
     * Ported from CGit::IsFastForward.
     * @param from A revision.
     * @param target A revision.
     * @returns Whether @p from is an ancestor of @p target; any failure counts as no.
     */
    [[nodiscard]] bool isFastForward(const QString& from, const QString& target) const;

    /**
     * Ported from CGit::GetFullRefName.
     * @param shortRefName A ref such as main or origin/main.
     * @returns Its full name, such as refs/heads/main, or an empty string if it isn't a ref.
     */
    [[nodiscard]] QString fullRefName(const QString& shortRefName) const;

    /**
     * Ported from CGit::IsBranchTagNameUnique.
     * @param name A short name.
     * @returns False if both a branch and a tag have that name.
     */
    [[nodiscard]] bool isBranchTagNameUnique(const QString& name) const;

    /**
     * Ported from CGit::BranchTagExists.
     * @param name A short name.
     * @param isBranch Whether to look for a branch (refs/heads/) rather than a tag (refs/tags/).
     * @returns Whether a branch, or a tag, has that name.
     */
    [[nodiscard]] bool branchTagExists(const QString& name, bool isBranch = true) const;

    /**
     * Ported from CGit::IsBranchNameValid, with git check-ref-format in place of libgit2.
     * @param name A branch name.
     * @returns Whether git accepts it.
     */
    [[nodiscard]] static bool isBranchNameValid(const QString& name);

    /**
     * Ported from CGit::DerefFetchHead: the commit FETCH_HEAD asks to merge.
     * @returns Its hash, or an empty string if FETCH_HEAD names none or several.
     */
    [[nodiscard]] QString derefFetchHead() const;

    /**
     * Ported from CGit::FixBranchName: FETCH_HEAD becomes the commit it names.
     * @param branchName A branch name.
     * @returns The name to give git.
     */
    [[nodiscard]] QString fixBranchName(const QString& branchName) const;

    /**
     * The git command CGit::GetBranchList runs.
     * @param type Which branches, BranchType bits.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList branchListArguments(unsigned int type);

    /**
     * Ported from CGit::GetBranchList: parses git branch's output and sorts it.
     * @param output What branchListArguments() printed.
     * @param type The same BranchType bits; BranchFetchHead adds FETCH_HEAD.
     * @param skipCurrent Whether to leave the current branch out.
     * @param hasFetchHead Whether derefFetchHead() names a commit.
     * @returns The sorted list and the current branch's index.
     */
    [[nodiscard]] static BranchList parseBranchList(const QByteArray& output, unsigned int type, bool skipCurrent, bool hasFetchHead);

    /**
     * Ported from CGit::GetTagList: parses git tag -l's output and sorts it logically.
     * @param output What git tag -l printed.
     * @returns The tags.
     */
    [[nodiscard]] static QStringList parseTagList(const QByteArray& output);

private:
    QString m_workingDirectory;
    int m_runTimeoutMs = -1; // How long run() waits; negative for ever
};

} // namespace kgt
