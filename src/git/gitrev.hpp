// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitRev.h and GitRev.cpp (GitRev): one commit, or the working tree, read by name.
 * So far only what the Changed Files dialog uses.
 */

#pragma once

#include "utils/result.hpp"

#include <QDateTime>
#include <QString>

namespace kgt
{

class Git;

/**
 * A commit, a tree or the working tree, with the fields a dialog shows about it.
 */
struct GitRev {
    QString hash; // m_CommitHash: the full hash; empty for the working tree
    QString subject; // GetSubject(): "Working Tree" for the working tree
    QString authorName; // GetAuthorName(), with the mailmap applied (ApplyMailmap)
    QDateTime authorDate; // GetAuthorDate()
    QDateTime committerDate; // GetCommitterDate()
    bool isCommit = true; // IsCommit(): false for a tree

    bool operator==(const GitRev&) const = default;

    /**
     * @returns GitRev::GetWorkingCopyRef: how the working tree is named as a version, forty zeros.
     */
    [[nodiscard]] static QString workingCopyRef();

    /**
     * The test GitRev::GetCommit makes first.
     * @param refName A version as typed.
     * @returns Whether it names the working tree: at least eight zeros, and no more than forty.
     */
    [[nodiscard]] static bool isWorkingCopyRef(const QString& refName);

    /**
     * Ported from GitRev::GetCommit(refName, allowTree = true): reads a commit, or failing that a tree, by name.
     * @param git Runs git in the repository.
     * @param refName A hash, ref or workingCopyRef(); FETCH_HEAD is resolved (CGit::FixBranchName).
     * @returns The version, or a Git error.
     */
    [[nodiscard]] static Result<GitRev> getCommit(const Git& git, const QString& refName);

    /**
     * @param output What getCommit's git log printed: %H, %s, %aN, %at and %ct, separated by NULs.
     * @returns The commit, or a Parse error.
     */
    [[nodiscard]] static Result<GitRev> parseCommit(const QByteArray& output);
};

} // namespace kgt
