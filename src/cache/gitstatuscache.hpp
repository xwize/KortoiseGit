// SPDX-FileCopyrightText: 2005-2008, 2011-2012, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/GitStatusCache.h. Saving the cache to disk is left out.
 */

#pragma once

#include "cacheddirectory.hpp"
#include "cacheinterface.hpp"
#include "directorywatcher.hpp"
#include "foldercrawler.hpp"
#include "shellupdater.hpp"

#include <QDeadlineTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace kgt
{

/**
 * The status of every working tree the plugin has asked about, folder by folder, kept current by DirectoryWatcher and
 * FolderCrawler. CGitStatusCache's locks aren't needed: everything here runs on one event loop.
 */
class GitStatusCache : public QObject
{
    Q_OBJECT

public:
    /**
     * @param maxWatches At most this many inotify watches are placed; negative for as many as the kernel allows.
     * @param parent Owner.
     */
    explicit GitStatusCache(int maxWatches = -1, QObject *parent = nullptr);

    /**
     * Ported from CGitStatusCache::GetStatusForPath, for all of a folder's items. Never touches git: a working tree
     * seen for the first time starts being crawled, and is answered once that has finished.
     * @param directory The folder Dolphin shows.
     * @returns Its items' states, or nothing if the cache can't say, so the caller reads git itself.
     */
    [[nodiscard]] std::optional<CacheFolderStatus> folderStatus(const QString& directory);

    /**
     * Ported from CGitStatusCache::IsPathGood.
     * @param root A working tree's top folder.
     * @returns Whether it may be crawled, which it can't while git holds its locks.
     */
    [[nodiscard]] bool isPathGood(const QString& root) const;

    /**
     * Ported from CGitStatusCache::UpdateShell.
     * @param path An item whose status changed.
     */
    void updateShell(const QString& path);

    /**
     * Passes a folder's status to its parent, the end of CCachedDirectory::UpdateCurrentStatus.
     * @param directory The folder.
     * @param status Its status.
     * @param notify Whether to tell the shell.
     */
    void tellParent(const QString& directory, const RecursiveStatus& status, bool notify);

    /**
     * Stores what a crawl found: the role of CCachedDirectory::GetStatusFromGit and its callback.
     * @param result What was found.
     */
    void applyCrawlResult(const CrawlResult& result);

    /**
     * Drops a working tree whose status can't be read for now; the plugin reads it itself in the meantime.
     * @param root Its top folder.
     */
    void crawlFailed(const QString& root);

    /**
     * @param path A folder.
     * @returns Whether a watch is on it.
     */
    [[nodiscard]] bool isPathWatched(const QString& path) const;

    /**
     * @param root A working tree's top folder.
     * @returns Whether its first crawl has finished, so folderStatus() can answer for it.
     */
    [[nodiscard]] bool isTreeReady(const QString& root) const;

Q_SIGNALS:
    /**
     * @param paths The items whose status changed, debounced by ShellUpdater.
     */
    void pathsChanged(const QStringList& paths);

private:
    /**
     * A working tree in the cache.
     */
    struct WatchedTree {
        QString adminDir; // Its admin directory
        bool ready = false; // The first crawl has finished
    };

    /**
     * The folders one crawl result affects.
     */
    struct Refresh {
        QString root;
        bool notify = false; // Whether to tell the shell what changed
        std::set<QString> refreshed; // Folders being read again: any not seen again has gone
        std::set<QString> seen; // Folders that turned out to exist
        std::set<QString> touched; // Folders whose status must be worked out again
    };

    CachedDirectory& directoryCacheEntry(const Refresh& refresh, const QString& path);
    [[nodiscard]] std::optional<CacheFolderStatus> collapsedFolderStatus(const QString& directory) const;
    [[nodiscard]] bool isPathAllowed(const QString& root) const;
    void addTree(const QString& root);
    void removeTree(const QString& root);
    void refuseTree(const QString& root, std::chrono::seconds duration);
    void blockPath(const QString& root, std::chrono::seconds duration);
    void removeTimedoutBlocks();
    void onPathChanged(const TreePath& item);

    void beginScope(Refresh& refresh, const QString& path);
    void addEntries(Refresh& refresh, const QList<GitStatusEntry>& entries);
    void addEntry(Refresh& refresh, const QString& relativePath, GitWcStatus status);
    void setTrackedDirs(Refresh& refresh, const QStringList& trackedDirs);
    [[nodiscard]] bool scanFolder(Refresh& refresh, const QString& folder);
    void removeDirectory(const Refresh& refresh, const QString& path);
    void finishRefresh(const Refresh& refresh);

    DirectoryWatcher m_watcher;
    FolderCrawler m_folderCrawler;
    ShellUpdater m_shellUpdater;
    std::map<QString, std::unique_ptr<CachedDirectory>> m_directoryCache; // Sorted, so a folder's children follow it
    QHash<QString, WatchedTree> m_trees;
    QHash<QString, QDeadlineTimer> m_noWatchPaths; // CGitStatusCache::m_NoWatchPaths: blocked while git holds locks
    QHash<QString, QDeadlineTimer> m_refusedTrees; // Trees not to take on again for now
    QTimer m_blockTimer; // Ends the earliest block, RemoveTimedoutBlocks' role
};

} // namespace kgt
