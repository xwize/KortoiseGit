// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2020, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitDiff.cpp (CGitDiff). TortoiseGit writes each revision to a temporary
 * file and starts its diff viewer on them; here git difftool does both.
 */

#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace kgt
{

/**
 * One run of git difftool.
 */
struct DiffInvocation {
    QString workingDirectory; // Where git runs
    QStringList arguments; // The arguments after "git"

    bool operator==(const DiffInvocation&) const = default;
};

/**
 * Builds git difftool command lines and starts them.
 */
class GitDiff
{
public:
    GitDiff() = delete;

    /**
     * Ported from CGitDiff::Diff.
     * @param path The right-hand file, relative to the working tree root.
     * @param path2 The left-hand file, relative to the working tree root; differs from @p path for a rename.
     * @param rev1 The right-hand revision; empty for the working tree (GitRev::GetWorkingCopyRef).
     * @param rev2 The left-hand revision.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList diffArguments(const QString& path, const QString& path2, const QString& rev1, const QString& rev2);

    /**
     * Ported from CGitDiff::DiffNull, for a file added in the working tree: compares it with nothing.
     * @param path The file.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList diffNullArguments(const QString& path);

    /**
     * Ported from CAppUtils::StartExtDiff: compares two files directly, which need not be in a repository.
     * @param file1 The left-hand file.
     * @param file2 The right-hand file.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList extDiffArguments(const QString& file1, const QString& file2);

    /**
     * Starts git difftool without waiting for it, and tells the user if it can't be started.
     * @param parent The parent of the error message; may be null.
     * @param invocation What to run.
     * @returns Whether it started.
     */
    [[nodiscard]] static bool start(QWidget *parent, const DiffInvocation& invocation);
};

} // namespace kgt
