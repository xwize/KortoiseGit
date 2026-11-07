// SPDX-FileCopyrightText: 2003-2008, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/TGitPath.h (CTGitPath): so far only the per-file action flags (LOGACTIONS_*)
 * that the status list, the commit dialog and the log's file list share. The path class itself follows when a
 * feature needs it.
 */

#pragma once

#include <QFlags>

namespace kgt
{

/**
 * A path in a working tree and what happened to it. For now only its action flags.
 */
class TGitPath
{
public:
    /**
     * What happened to a file (TortoiseGit's LOGACTIONS_*, with the same values).
     */
    enum class Action : unsigned int {
        Added = 0x00000001, // LOGACTIONS_ADDED
        Modified = 0x00000002, // LOGACTIONS_MODIFIED (also a type change)
        Replaced = 0x00000004, // LOGACTIONS_REPLACED: renamed
        Deleted = 0x00000008, // LOGACTIONS_DELETED
        Unmerged = 0x00000010, // LOGACTIONS_UNMERGED: conflicted
        Copy = 0x00000040, // LOGACTIONS_COPY
        Merged = 0x00000080, // LOGACTIONS_MERGED
        AssumeValid = 0x00000200, // LOGACTIONS_ASSUMEVALID
        SkipWorktree = 0x00000400, // LOGACTIONS_SKIPWORKTREE
        Missing = 0x00001000, // LOGACTIONS_MISSING
        Unversioned = 0x80000000, // LOGACTIONS_UNVER
        Ignore = 0x40000000, // LOGACTIONS_IGNORE
    };
    Q_DECLARE_FLAGS(Actions, Action)

    /**
     * Ported from CTGitPath::ParseStatus.
     * @param status One git status letter (M, R, A, D, U, K, C or T).
     * @returns The matching action, or no flags for any other character (such as '.' for unchanged).
     */
    [[nodiscard]] static Actions parseStatus(char status);
};

} // namespace kgt

Q_DECLARE_OPERATORS_FOR_FLAGS(kgt::TGitPath::Actions)
