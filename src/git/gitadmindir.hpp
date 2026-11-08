// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitAdminDir.h: finds a working tree's administrative directory (.git) by looking
 * at the file system, without running git, so the plugin can use it while building a context menu.
 */

#pragma once

#include <QString>

namespace kgt
{

/**
 * Locates .git directories, following a "gitdir:" file as linked worktrees and submodules have.
 */
class GitAdminDir
{
public:
    GitAdminDir() = delete;

    /**
     * Ported from GitAdminDir::GetWorktreeAdminDirPath: walks up from @p path to the nearest .git.
     * @param path A file or directory, possibly inside a working tree.
     * @returns The admin directory of the working tree holding @p path (for a linked worktree, its own one under
     *          the main repository's worktrees/), or an empty string if @p path is in none.
     */
    [[nodiscard]] static QString worktreeAdminDirPath(const QString& path);

    /**
     * Ported from GitAdminDir::HasAdminDir: walks up from @p path to the nearest .git, which must hold a valid
     * repository (HEAD, config, objects/ and refs/heads/, read from the common directory for a linked worktree).
     * A bare repository on the way stops the search, so the working tree around it doesn't claim its files.
     * @param path A file or directory.
     * @returns The top directory of the working tree holding @p path, or an empty string if it is in none, or
     *          is .git or inside it.
     */
    [[nodiscard]] static QString projectTopDir(const QString& path);

    /**
     * Ported from GitAdminDir::IsAdminDirPath.
     * @param path A path.
     * @returns Whether it is a .git directory or lies inside one.
     */
    [[nodiscard]] static bool isAdminDirPath(const QString& path);

    /**
     * Ported from GitAdminDir::IsBareRepo: whether a folder is a bare repository's top, by its files.
     * @param path A folder.
     * @returns True if it holds HEAD, config, objects/ and refs/heads/.
     */
    [[nodiscard]] static bool isBareRepo(const QString& path);

    /**
     * Ported from CGit::GetCurrentBranchFromFile: reads HEAD directly, as git-gui does.
     * @param adminDir A working tree's admin directory, from worktreeAdminDirPath().
     * @param fallback Give a detached HEAD's commit hash instead of "HEAD".
     * @returns The current branch's name (it may not exist yet, in a repository without commits), "HEAD" or the hash
     *          (see @p fallback) when HEAD is detached, or an empty string if HEAD can't be read.
     */
    [[nodiscard]] static QString currentBranchFromFile(const QString& adminDir, bool fallback = false);
};

} // namespace kgt
