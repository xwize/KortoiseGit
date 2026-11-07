// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from the --numstat handling in TortoiseGit src/Git/TGitPathList.cpp (CTGitPathList::ParserFromLog): how
 * many lines each changed file gains and loses against HEAD, for the status list's Lines added and Lines removed
 * columns.
 */

#pragma once

#include "utils/result.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * The line counts of one changed file. The counts are git's text, as TortoiseGit keeps them (m_StatAdd and
 * m_StatDel): a number, or "-" for a binary file.
 */
struct NumStatEntry {
    QString path; // Relative to the working tree root, '/'-separated
    QString origPath; // The rename or copy source; empty otherwise
    QString added; // Lines added, or "-" for a binary file
    QString removed; // Lines removed, or "-" for a binary file

    bool operator==(const NumStatEntry&) const = default;
};

/**
 * Reads git diff --numstat -z: the working tree against a commit, staged and unstaged changes together.
 */
class GitNumStat
{
public:
    /**
     * @param base The commit to compare the working tree with, normally HEAD.
     * @param paths Pathspecs to limit the diff to, or empty for the whole working tree.
     * @returns The git arguments for a diff that parse() understands.
     */
    [[nodiscard]] static QStringList arguments(const QString& base, const QStringList& paths = {});

    /**
     * @param output The stdout of git with arguments().
     * @returns An entry for each changed file, in git's order, or a Parse error naming the first bad record.
     */
    [[nodiscard]] static Result<QList<NumStatEntry>> parse(const QByteArray& output);
};

} // namespace kgt
