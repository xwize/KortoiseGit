// SPDX-FileCopyrightText: 2005-2008, 2011-2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/DirectoryWatcher.cpp

#include "directorywatcher.hpp"

#include "cachedebug.hpp"

#include <QFile>

#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

namespace
{

// FILE_NOTIFY_CHANGE_FILE_NAME | DIR_NAME | LAST_WRITE, and IN_ATTRIB as git tracks the executable bit
constexpr unsigned int treeMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF;
constexpr unsigned int adminMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY;
constexpr unsigned int watchFlags = IN_ONLYDIR | IN_DONT_FOLLOW | IN_EXCL_UNLINK;
constexpr unsigned int appearedMask = static_cast<unsigned int>(IN_CREATE) | static_cast<unsigned int>(IN_MOVED_TO);
constexpr unsigned int wentMask = static_cast<unsigned int>(IN_DELETE) | static_cast<unsigned int>(IN_MOVED_FROM);
constexpr qsizetype bufferSize = 64LL * 1024;

/**
 * Ported from the filters in CDirectoryWatcher::WorkerThread: temporary files and folders don't change a file's status.
 * TortoiseGit tests the whole path; here only the part inside the working tree, or a repository under /tmp would never
 * update.
 * @param root The working tree's top folder.
 * @param path A path in it.
 * @returns Whether to ignore events for it.
 */
bool isTemporary(const QString& root, const QString& path)
{
    const QStringView relative = QStringView(path).mid(root.size());
    if (relative.contains(QLatin1String(".tmp"), Qt::CaseInsensitive)) {
        return true;
    }
    qsizetype found = relative.indexOf(QLatin1String("/tmp"), 0, Qt::CaseInsensitive);
    while (found >= 0) {
        const qsizetype end = found + 4;
        if (end == relative.size() || relative.at(end) == QLatin1Char('/')) {
            return true;
        }
        found = relative.indexOf(QLatin1String("/tmp"), end, Qt::CaseInsensitive);
    }
    return false;
}

} // namespace

namespace kgt
{

DirectoryWatcher::DirectoryWatcher(int maxWatches, QObject *parent)
    : QObject(parent)
    , m_inotify(inotify_init1(IN_NONBLOCK | IN_CLOEXEC))
    , m_maxWatches(maxWatches)
{
    if (m_inotify < 0) {
        // Too many inotify instances: addPath() fails and every tree falls back to the plugin's own retrieval
        qCInfo(KGT_CACHE) << "inotify is unavailable:" << std::strerror(errno);
        return;
    }
    m_notifier = std::make_unique<QSocketNotifier>(m_inotify, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &DirectoryWatcher::readEvents);
}

DirectoryWatcher::~DirectoryWatcher()
{
    m_notifier.reset();
    if (m_inotify >= 0) {
        close(m_inotify);
    }
}

bool DirectoryWatcher::addPath(const TreePath& folder)
{
    return addWatch(Watch{.root = folder.root, .path = folder.path, .isAdminDir = false}, treeMask);
}

bool DirectoryWatcher::addAdminDir(const TreePath& adminDir)
{
    return addWatch(Watch{.root = adminDir.root, .path = adminDir.path, .isAdminDir = true}, adminMask);
}

bool DirectoryWatcher::addWatch(const Watch& watch, unsigned int mask)
{
    if (m_inotify < 0 || (m_maxWatches >= 0 && m_watches.size() >= m_maxWatches && !m_paths.contains(watch.path))) {
        return false;
    }
    const int descriptor = inotify_add_watch(m_inotify, QFile::encodeName(watch.path).constData(), mask | watchFlags);
    if (descriptor < 0) {
        return errno != ENOSPC && errno != ENOMEM;
    }
    m_watches.insert(descriptor, watch);
    m_paths[watch.path] = descriptor;
    return true;
}

void DirectoryWatcher::removePathAndChildren(const QString& path)
{
    const QString prefix = path + QLatin1Char('/');
    auto found = m_paths.lower_bound(path);
    while (found != m_paths.end() && (found->first == path || found->first.startsWith(prefix))) {
        inotify_rm_watch(m_inotify, found->second);
        m_watches.remove(found->second);
        found = m_paths.erase(found);
    }
}

bool DirectoryWatcher::isPathWatched(const QString& path) const
{
    return m_paths.contains(path);
}

qsizetype DirectoryWatcher::watchCount() const
{
    return m_watches.size();
}

void DirectoryWatcher::readEvents()
{
    std::array<char, bufferSize> buffer{};
    for (;;) {
        const ssize_t length = read(m_inotify, buffer.data(), buffer.size());
        if (length <= 0) {
            return;
        }
        const QByteArrayView data(buffer.data(), length);
        qsizetype offset = 0;
        while (offset + static_cast<qsizetype>(sizeof(inotify_event)) <= data.size()) {
            inotify_event header{};
            std::memcpy(&header, data.sliced(offset).data(), sizeof(header));
            const qsizetype nameOffset = offset + static_cast<qsizetype>(sizeof(inotify_event));
            // The name is padded with NULs to the record's length
            const QByteArrayView name = data.sliced(nameOffset, header.len);
            handleEvent(Event{.watch = header.wd,
                              .mask = header.mask,
                              .name = QFile::decodeName(name.first(static_cast<qsizetype>(qstrnlen(name.data(), name.size()))).toByteArray())});
            offset = nameOffset + header.len;
        }
    }
}

void DirectoryWatcher::handleEvent(const Event& event)
{
    if ((event.mask & IN_Q_OVERFLOW) != 0) {
        Q_EMIT eventsLost();
        return;
    }
    const auto found = m_watches.constFind(event.watch);
    if (found == m_watches.constEnd()) {
        return;
    }
    if ((event.mask & IN_IGNORED) != 0) {
        // The kernel dropped the watch: its folder has gone
        removeWatchDescriptor(event.watch);
        return;
    }
    const Watch watch = *found;
    if (watch.isAdminDir) {
        handleAdminEvent(watch, event);
        return;
    }
    if (event.name == QLatin1String(".git")) {
        return;
    }
    const QString path = event.name.isEmpty() ? watch.path : watch.path + QLatin1Char('/') + event.name;
    if (isTemporary(watch.root, path)) {
        return;
    }
    if ((event.mask & IN_ISDIR) != 0 && (event.mask & IN_MOVED_FROM) != 0) {
        // A watch follows the folder's inode, so one that moved would go on reporting under its old path
        removePathAndChildren(path);
    }
    Q_EMIT pathChanged(TreePath{.root = watch.root, .path = path});
}

void DirectoryWatcher::handleAdminEvent(const Watch& watch, const Event& event)
{
    // CDirectoryWatcher::WorkerThread matches these names as substrings, which FETCH_HEAD would satisfy on every fetch
    const bool isLock = event.name == QLatin1String("index.lock") || event.name == QLatin1String("HEAD.lock");
    const bool isTarget = event.name == QLatin1String("index") || event.name == QLatin1String("HEAD");
    if (isLock && (event.mask & appearedMask) != 0) {
        Q_EMIT lockTaken(watch.root);
    } else if ((isLock && (event.mask & wentMask) != 0) || (isTarget && (event.mask & IN_MODIFY) != 0)) {
        Q_EMIT lockReleased(watch.root);
    }
}

void DirectoryWatcher::removeWatchDescriptor(int descriptor)
{
    const auto found = m_watches.constFind(descriptor);
    if (found == m_watches.constEnd()) {
        return;
    }
    // A folder recreated at once already has a new watch under the same path
    const auto path = m_paths.find(found->path);
    if (path != m_paths.end() && path->second == descriptor) {
        m_paths.erase(path);
    }
    m_watches.erase(found);
}

} // namespace kgt
