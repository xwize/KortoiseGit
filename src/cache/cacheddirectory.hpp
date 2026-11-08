// SPDX-FileCopyrightText: 2005-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/CachedDirectory.h.
 */

#pragma once

#include "cacheinterface.hpp"

#include <QHash>
#include <QSet>
#include <QString>

#include <optional>

namespace kgt
{

class GitStatusCache;

/**
 * A folder's status including everything below it: what CCachedDirectory::CalculateRecursiveStatus works out.
 * TortoiseGit needs one value; the flags are what tells our unversioned and ignored folders apart (GitFolderStatus).
 */
struct RecursiveStatus {
    GitWcStatus change = GitWcStatus::None; // The most important change below the folder
    bool hasUntracked = false; // An untracked file lies below it
    bool hasIgnored = false; // An ignored file or folder lies below it

    bool operator==(const RecursiveStatus&) const = default;
};

/**
 * One folder of a working tree in the cache. It holds the state of its direct members and the recursive status of each
 * child folder, so a change only has to travel up through its parents.
 *
 * git status lists changed files only, so unlike m_entryCache here members are stored just when they aren't normal:
 * on a kernel-sized tree that is a few entries, not eighty thousand.
 */
class CachedDirectory
{
public:
    /**
     * @param cache The cache this folder belongs to.
     * @param directoryPath The folder, absolute and clean.
     * @param isRoot Whether it is its working tree's top folder, which has no parent to tell.
     */
    CachedDirectory(GitStatusCache& cache, const QString& directoryPath, bool isRoot);

    /**
     * Forgets all members, remembering them so that finishRefresh() can tell what changed: the role of
     * CCachedDirectory::EnumFiles' m_entryCache_tmp.
     */
    void beginRefresh();

    /**
     * Forgets one member, which git is being asked about again.
     * @param name The member's file name.
     */
    void clearMember(const QString& name);

    /**
     * Ported from CCachedDirectory::AddEntry.
     * @param name The member's file name.
     * @param status What git says it is; of two entries for one path the more important wins.
     */
    void addEntry(const QString& name, GitWcStatus status);

    /**
     * @param name A member's file name.
     * @returns Its state, if it isn't normal.
     */
    [[nodiscard]] std::optional<GitWcStatus> memberStatus(const QString& name) const;

    /**
     * Forgets which children are tracked, before a whole-tree crawl states them again.
     */
    void clearTrackedChildren();

    /**
     * @param name A child folder or submodule that is in HEAD, so it shows as normal when nothing in it changed.
     */
    void addTrackedChild(const QString& name);

    /**
     * Reports the members whose state changed since beginRefresh() or clearMember().
     * @param notify Whether to tell the shell.
     */
    void finishRefresh(bool notify);

    /**
     * Ported from CCachedDirectory::CalculateRecursiveStatus.
     * @returns This folder's status including everything below it.
     */
    [[nodiscard]] RecursiveStatus calculateRecursiveStatus() const;

    /**
     * Ported from CCachedDirectory::UpdateCurrentStatus: recalculates this folder's status, tells the shell if it
     * changed, and always tells the parent.
     * @param notify Whether to tell the shell.
     */
    void updateCurrentStatus(bool notify);

    /**
     * Ported from CCachedDirectory::UpdateChildDirectoryStatus: receives a child's status, which carries on up
     * through the parents if it changed.
     * @param name The child folder's file name.
     * @param status Its status; the default for a child with nothing to report, or one that has gone.
     * @param notify Whether to tell the shell.
     */
    void updateChildDirectoryStatus(const QString& name, const RecursiveStatus& status, bool notify);

    /**
     * @returns The cache's answer for this folder's items, GetStatusForMember's role for all of them at once.
     */
    [[nodiscard]] CacheFolderStatus folderStatus() const;

private:
    void rememberMember(const QString& name);

    GitStatusCache *m_cache;
    QString m_directoryPath;
    bool m_isRoot;
    QHash<QString, GitWcStatus> m_entryCache; // The members that aren't normal
    QHash<QString, std::optional<GitWcStatus>> m_refreshedMembers; // What the members being read again were before
    QHash<QString, RecursiveStatus> m_childDirectories; // The child folders with something to report
    QSet<QString> m_trackedChildren; // Child folders and submodules in HEAD
    RecursiveStatus m_currentFullStatus;
};

} // namespace kgt
