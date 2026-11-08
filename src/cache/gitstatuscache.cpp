// SPDX-FileCopyrightText: 2005-2008, 2011-2012, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/GITStatusCache.cpp

#include "gitstatuscache.hpp"

#include "cachedebug.hpp"
#include "git/gitadmindir.hpp"

#include <QDirIterator>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>
#include <chrono>
#include <ranges>

namespace
{

using namespace kgt;
using namespace std::chrono_literals;

constexpr std::chrono::seconds blockPathDefaultTimeout = 600s; // BLOCK_PATH_DEFAULT_TIMEOUT
constexpr std::chrono::seconds blockPathWaitAfterUnlock = 2s; // BLOCK_PATH_WAIT_AFTER_UNLOCK
constexpr std::chrono::seconds retryAfterFailure = 30s;
constexpr std::chrono::seconds retryAfterWatchLimit = 600s;

/**
 * @param path An absolute path that isn't the file system's root.
 * @returns The folder holding it.
 */
QString parentPath(const QString& path)
{
    return path.left(path.lastIndexOf(QLatin1Char('/')));
}

/**
 * @param path An absolute path.
 * @returns Its last component.
 */
QString fileName(const QString& path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

} // namespace

namespace kgt
{

GitStatusCache::GitStatusCache(int maxWatches, QObject *parent)
    : QObject(parent)
    , m_watcher(maxWatches)
    , m_folderCrawler(*this)
{
    connect(&m_watcher, &DirectoryWatcher::pathChanged, this, &GitStatusCache::onPathChanged);
    connect(&m_watcher, &DirectoryWatcher::lockTaken, this, [this](const QString& root) {
        blockPath(root, blockPathDefaultTimeout);
    });
    // CDirectoryWatcher::WorkerThread: the block stays a little longer, as during a rebase the lock comes and goes quickly
    connect(&m_watcher, &DirectoryWatcher::lockReleased, this, [this](const QString& root) {
        blockPath(root, blockPathWaitAfterUnlock);
    });
    connect(&m_watcher, &DirectoryWatcher::eventsLost, this, [this] {
        for (auto it = m_trees.cbegin(); it != m_trees.cend(); ++it) {
            m_folderCrawler.addFolderForCrawling(TreePath{.root = it.key(), .path = it.key()});
        }
    });
    connect(&m_shellUpdater, &ShellUpdater::pathsChanged, this, &GitStatusCache::pathsChanged);
    m_blockTimer.setSingleShot(true);
    connect(&m_blockTimer, &QTimer::timeout, this, &GitStatusCache::removeTimedoutBlocks);
}

std::optional<CacheFolderStatus> GitStatusCache::folderStatus(const QString& directory)
{
    const QString folder = QFileInfo(directory).canonicalFilePath();
    const QString root = folder.isEmpty() ? QString() : GitAdminDir::projectTopDir(folder);
    if (root.isEmpty()) {
        return std::nullopt;
    }
    const auto tree = m_trees.constFind(root);
    if (tree == m_trees.constEnd()) {
        addTree(root);
        return std::nullopt;
    }
    if (!tree->ready) {
        return std::nullopt;
    }
    const auto found = m_directoryCache.find(folder);
    return found != m_directoryCache.end() ? std::optional(found->second->folderStatus()) : collapsedFolderStatus(folder);
}

std::optional<CacheFolderStatus> GitStatusCache::collapsedFolderStatus(const QString& directory) const
{
    // A folder inside one git reports as a whole ("build/") has no entry of its own: its nearest cached ancestor knows.
    // The working tree's top folder is always cached, so the search ends there at the latest
    for (QString child = directory; child.contains(QLatin1Char('/')); child = parentPath(child)) {
        const auto parent = m_directoryCache.find(parentPath(child));
        if (parent == m_directoryCache.end()) {
            continue;
        }
        const std::optional<GitWcStatus> status = parent->second->memberStatus(fileName(child));
        if (status) {
            CacheFolderStatus result;
            result.wholeFolder = status;
            result.hasUnversioned = *status == GitWcStatus::Unversioned;
            return result;
        }
        return std::nullopt; // Created a moment ago: the watcher hasn't reported it yet
    }
    return std::nullopt;
}

bool GitStatusCache::isPathAllowed(const QString& root) const
{
    // ShellCache::IsPathAllowed leaves network drives out by default; inotify wouldn't see other machines' changes either
    static const QSet<QByteArray> remote{"nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "afs", "ceph", "fuse.sshfs"};
    return !remote.contains(QStorageInfo(root).fileSystemType());
}

void GitStatusCache::addTree(const QString& root)
{
    const auto refused = m_refusedTrees.constFind(root);
    if (refused != m_refusedTrees.constEnd()) {
        if (!refused->hasExpired()) {
            return;
        }
        m_refusedTrees.erase(refused);
    }
    if (!isPathAllowed(root)) {
        refuseTree(root, retryAfterWatchLimit);
        return;
    }
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(root);
    if (!m_watcher.addAdminDir(TreePath{.root = root, .path = adminDir}) || !m_watcher.addPath(TreePath{.root = root, .path = root})) {
        refuseTree(root, retryAfterWatchLimit);
        return;
    }
    m_trees.insert(root, WatchedTree{.adminDir = adminDir, .ready = false});
    m_folderCrawler.addFolderForCrawling(TreePath{.root = root, .path = root});
}

void GitStatusCache::removeTree(const QString& root)
{
    const auto tree = m_trees.constFind(root);
    if (tree != m_trees.constEnd()) {
        m_watcher.removePathAndChildren(tree->adminDir);
        m_trees.erase(tree);
    }
    m_watcher.removePathAndChildren(root);
    m_folderCrawler.removeTree(root);
    m_noWatchPaths.remove(root);
    const QString prefix = root + QLatin1Char('/');
    auto found = m_directoryCache.lower_bound(root);
    while (found != m_directoryCache.end() && (found->first == root || found->first.startsWith(prefix))) {
        found = m_directoryCache.erase(found);
    }
}

void GitStatusCache::refuseTree(const QString& root, std::chrono::seconds duration)
{
    qCInfo(KGT_CACHE) << "Not caching" << root << "for now";
    removeTree(root);
    m_refusedTrees.insert(root, QDeadlineTimer(duration));
}

bool GitStatusCache::isPathGood(const QString& root) const
{
    const auto block = m_noWatchPaths.constFind(root);
    return block == m_noWatchPaths.constEnd() || block->hasExpired();
}

void GitStatusCache::blockPath(const QString& root, std::chrono::seconds duration)
{
    m_noWatchPaths.insert(root, QDeadlineTimer(duration));
    removeTimedoutBlocks();
}

void GitStatusCache::removeTimedoutBlocks()
{
    std::optional<qint64> nextTimeoutMs;
    for (auto it = m_noWatchPaths.begin(); it != m_noWatchPaths.end();) {
        if (it->hasExpired()) {
            const QString root = it.key();
            it = m_noWatchPaths.erase(it);
            // UnBlockPath: "we don't know what we missed during the lock"
            m_folderCrawler.addFolderForCrawling(TreePath{.root = root, .path = root});
        } else {
            nextTimeoutMs = std::min(nextTimeoutMs.value_or(it->remainingTime()), it->remainingTime());
            ++it;
        }
    }
    if (nextTimeoutMs) {
        m_blockTimer.start(std::chrono::milliseconds(*nextTimeoutMs));
    } else {
        m_blockTimer.stop();
    }
}

void GitStatusCache::onPathChanged(const TreePath& item)
{
    if (m_trees.contains(item.root)) {
        m_folderCrawler.addPathForUpdate(item);
    }
}

void GitStatusCache::updateShell(const QString& path)
{
    m_shellUpdater.addPathForUpdate(path);
}

void GitStatusCache::tellParent(const QString& directory, const RecursiveStatus& status, bool notify)
{
    const auto parent = m_directoryCache.find(parentPath(directory));
    if (parent != m_directoryCache.end()) {
        parent->second->updateChildDirectoryStatus(fileName(directory), status, notify);
    }
}

CachedDirectory& GitStatusCache::directoryCacheEntry(const Refresh& refresh, const QString& path)
{
    std::unique_ptr<CachedDirectory>& entry = m_directoryCache[path];
    if (!entry) {
        entry = std::make_unique<CachedDirectory>(*this, path, path == refresh.root);
    }
    return *entry;
}

void GitStatusCache::crawlFailed(const QString& root)
{
    refuseTree(root, retryAfterFailure);
}

void GitStatusCache::applyCrawlResult(const CrawlResult& result)
{
    const auto tree = m_trees.find(result.root);
    if (tree == m_trees.end()) {
        return;
    }
    // CGitStatusCache::Create: "crawled paths will trigger a shell notification, which makes the desktop flash": the
    // first crawl is news to nobody, as until it finishes the plugin reads git itself
    Refresh refresh{.root = result.root, .notify = tree->ready, .refreshed = {}, .seen = {}, .touched = {}};
    for (const QString& path : result.scope) {
        beginScope(refresh, path);
    }
    addEntries(refresh, result.entries);
    if (result.trackedDirs) {
        setTrackedDirs(refresh, *result.trackedDirs);
    }
    for (const QString& path : result.scope) {
        if (QFileInfo(path).isDir() && !scanFolder(refresh, path)) {
            // fs.inotify.max_user_watches is exhausted: an unwatched tree is left to the plugin's normal retrieval
            refuseTree(result.root, retryAfterWatchLimit);
            return;
        }
    }
    finishRefresh(refresh);
    tree->ready = true;
}

void GitStatusCache::beginScope(Refresh& refresh, const QString& path)
{
    const QFileInfo info(path);
    const bool isFolder = m_directoryCache.contains(path) || (info.isDir() && !info.isSymLink());
    if (path != refresh.root) {
        CachedDirectory& parent = directoryCacheEntry(refresh, parentPath(path));
        parent.clearMember(fileName(path));
        refresh.seen.insert(parentPath(path));
        refresh.touched.insert(parentPath(path));
    }
    if (!isFolder) {
        return;
    }
    const QString prefix = path + QLatin1Char('/');
    for (auto it = m_directoryCache.lower_bound(path); it != m_directoryCache.end() && (it->first == path || it->first.startsWith(prefix)); ++it) {
        it->second->beginRefresh();
        refresh.refreshed.insert(it->first);
        refresh.touched.insert(it->first);
    }
}

void GitStatusCache::addEntries(Refresh& refresh, const QList<GitStatusEntry>& entries)
{
    for (const GitStatusEntry& entry : entries) {
        addEntry(refresh, entry.path, GitStatus::wcStatus(entry));
        if (!entry.origPath.isEmpty()) {
            // A rename also takes a file away from the source's folder
            addEntry(refresh, entry.origPath, GitWcStatus::Deleted);
        }
    }
}

void GitStatusCache::addEntry(Refresh& refresh, const QString& relativePath, GitWcStatus status)
{
    // A folder git reports as a whole ("build/") is a member of its parent
    const QString relative = relativePath.endsWith(QLatin1Char('/')) ? relativePath.chopped(1) : relativePath;
    const qsizetype slash = relative.lastIndexOf(QLatin1Char('/'));
    QString folder = slash < 0 ? refresh.root : refresh.root + QLatin1Char('/') + relative.left(slash);
    directoryCacheEntry(refresh, folder).addEntry(relative.mid(slash + 1), status);
    refresh.touched.insert(folder);
    // Every folder above it exists too, even if git lists no file directly inside it
    while (refresh.seen.insert(folder).second && folder != refresh.root) {
        folder = parentPath(folder);
        directoryCacheEntry(refresh, folder);
        refresh.touched.insert(folder);
    }
}

void GitStatusCache::setTrackedDirs(Refresh& refresh, const QStringList& trackedDirs)
{
    const QString prefix = refresh.root + QLatin1Char('/');
    for (auto it = m_directoryCache.lower_bound(refresh.root); it != m_directoryCache.end() && (it->first == refresh.root || it->first.startsWith(prefix));
         ++it) {
        it->second->clearTrackedChildren();
    }
    for (const QString& relative : trackedDirs) {
        const qsizetype slash = relative.lastIndexOf(QLatin1Char('/'));
        const QString folder = slash < 0 ? refresh.root : prefix + relative.left(slash);
        directoryCacheEntry(refresh, folder).addTrackedChild(relative.mid(slash + 1));
    }
}

bool GitStatusCache::scanFolder(Refresh& refresh, const QString& folder)
{
    QStringList stack{folder};
    while (!stack.isEmpty()) {
        const QString current = stack.takeLast();
        const CachedDirectory& cached = directoryCacheEntry(refresh, current);
        refresh.seen.insert(current);
        refresh.touched.insert(current);
        if (!m_watcher.addPath(TreePath{.root = refresh.root, .path = current})) {
            return false;
        }
        QDirIterator children(current, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
        while (children.hasNext()) {
            const QString child = children.next();
            const QString name = children.fileName();
            // Never into .git, a folder git reports as a whole (an ignored build/), or another repository or submodule
            if (name != QLatin1String(".git") && !cached.memberStatus(name) && !QFileInfo::exists(child + QLatin1String("/.git"))) {
                stack.append(child);
            }
        }
    }
    return true;
}

void GitStatusCache::removeDirectory(const Refresh& refresh, const QString& path)
{
    m_watcher.removePathAndChildren(path);
    const QString prefix = path + QLatin1Char('/');
    auto found = m_directoryCache.lower_bound(path);
    while (found != m_directoryCache.end() && (found->first == path || found->first.startsWith(prefix))) {
        found = m_directoryCache.erase(found);
    }
    if (path != refresh.root) {
        tellParent(path, RecursiveStatus{}, refresh.notify);
    }
}

void GitStatusCache::finishRefresh(const Refresh& refresh)
{
    for (const QString& path : refresh.refreshed) {
        if (!refresh.seen.contains(path) && m_directoryCache.contains(path)) {
            removeDirectory(refresh, path);
        }
    }
    // Children sort after their parents, so going backwards works from the bottom of the tree up
    for (const QString& path : refresh.touched | std::views::reverse) {
        const auto found = m_directoryCache.find(path);
        if (found != m_directoryCache.end()) {
            found->second->finishRefresh(refresh.notify);
            found->second->updateCurrentStatus(refresh.notify);
        }
    }
}

bool GitStatusCache::isPathWatched(const QString& path) const
{
    return m_watcher.isPathWatched(path);
}

bool GitStatusCache::isTreeReady(const QString& root) const
{
    const auto tree = m_trees.constFind(root);
    return tree != m_trees.constEnd() && tree->ready;
}

} // namespace kgt
