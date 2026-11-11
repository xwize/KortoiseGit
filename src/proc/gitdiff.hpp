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

#include "apputils.hpp"

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
     * Compares two versions of a whole tree in one run of the diff tool, for the commit comparisons that
     * CGitDiff::DiffCommit shows in a CFileDiffDlg.
     * @param paths The files and folders to limit it to, relative to the working tree root; empty for all of them.
     * @param rev1 The right-hand revision; empty for the working tree.
     * @param rev2 The left-hand revision.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList dirArguments(const QStringList& paths, const QString& rev1, const QString& rev2);

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
     * Names a diff tool on a git difftool command line when the user's settings don't: Meld, otherwise KDiff3.
     * @param arguments The arguments after "git", from one of the functions above.
     * @param diffTool The value of diff.tool; empty if it isn't set.
     * @param mergeTool The value of merge.tool; empty if it isn't set.
     * @param isInstalled Says whether an executable is on the PATH.
     * @returns The arguments, unchanged if either setting is set or neither tool is installed, which leaves it to git.
     */
    [[nodiscard]] static QStringList
    withDefaultTool(const QStringList& arguments, const QString& diffTool, const QString& mergeTool, const AppUtils::IsInstalled& isInstalled);

    /**
     * Starts git difftool without waiting for it, and tells the user if it has no tool it can run, can't be
     * started or fails.
     * @param parent The parent of the error message; may be null.
     * @param invocation What to run.
     * @returns Whether it started.
     */
    [[nodiscard]] static bool start(QWidget *parent, const DiffInvocation& invocation);
};

} // namespace kgt
