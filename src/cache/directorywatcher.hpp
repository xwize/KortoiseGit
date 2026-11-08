// SPDX-FileCopyrightText: 2005-2008, 2011-2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/DirectoryWatcher.h.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QString>

#include <map>
#include <memory>

namespace kgt
{

/**
 * A path, and the working tree it belongs to.
 */
struct TreePath {
    QString root; // The working tree's top folder
    QString path; // The file or folder
};

/**
 * Receives file system change events for working trees. ReadDirectoryChangesW watches a whole tree through one
 * handle; inotify needs one watch per folder, so GitStatusCache adds every folder it wants watched and never the
 * ignored ones. Events arrive through the event loop, which stands in for CDirectoryWatcher's worker thread.
 */
class DirectoryWatcher : public QObject
{
    Q_OBJECT

public:
    /**
     * @param maxWatches At most this many watches are placed; negative for as many as the kernel allows.
     * @param parent Owner.
     */
    explicit DirectoryWatcher(int maxWatches = -1, QObject *parent = nullptr);
    ~DirectoryWatcher() override;

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    DirectoryWatcher(DirectoryWatcher&&) = delete;
    DirectoryWatcher& operator=(DirectoryWatcher&&) = delete;

    /**
     * Ported from CDirectoryWatcher::AddPath: watches one folder of a working tree, not what is below it.
     * @param folder The folder, and its working tree.
     * @returns False if the watch limit (fs.inotify.max_user_watches) is exhausted. A folder that can't be watched
     *          for another reason, such as having gone already, counts as done.
     */
    [[nodiscard]] bool addPath(const TreePath& folder);

    /**
     * Watches the admin directory, for the index and HEAD and their lock files.
     * @param adminDir The working tree's admin directory.
     * @returns False if the watch limit is exhausted.
     */
    [[nodiscard]] bool addAdminDir(const TreePath& adminDir);

    /**
     * Ported from CDirectoryWatcher::RemovePathAndChildren.
     * @param path A folder; it and everything watched below it stop being watched.
     */
    void removePathAndChildren(const QString& path);

    /**
     * @param path A folder.
     * @returns Whether it has a watch. Ported from CDirectoryWatcher::IsPathWatched, without its recursion.
     */
    [[nodiscard]] bool isPathWatched(const QString& path) const;

    /**
     * @returns How many watches are in place.
     */
    [[nodiscard]] qsizetype watchCount() const;

Q_SIGNALS:
    /**
     * A file or folder of a working tree changed, in a way CDirectoryWatcher::WorkerThread counts.
     * @param item The item, and its working tree.
     */
    void pathChanged(const TreePath& item);

    /**
     * index.lock or HEAD.lock appeared: git is changing the repository.
     * @param root The working tree's top folder.
     */
    void lockTaken(const QString& root);

    /**
     * index.lock or HEAD.lock went, or the index or HEAD were written.
     * @param root The working tree's top folder.
     */
    void lockReleased(const QString& root);

    /**
     * The kernel's event queue overflowed, so changes went unseen, as when CDirectoryWatcher restarts its watches.
     */
    void eventsLost();

private:
    /**
     * What one inotify watch is for.
     */
    struct Watch {
        QString root; // The working tree's top folder
        QString path; // The folder watched
        bool isAdminDir = false; // Whether it is the working tree's admin directory
    };

    /**
     * One event, decoded from the kernel's buffer.
     */
    struct Event {
        int watch = -1; // Its watch descriptor
        unsigned int mask = 0; // Its IN_* bits
        QString name; // The item inside the watched folder; empty for the folder itself
    };

    [[nodiscard]] bool addWatch(const Watch& watch, unsigned int mask);
    void readEvents();
    void handleEvent(const Event& event);
    void handleAdminEvent(const Watch& watch, const Event& event);
    void removeWatchDescriptor(int descriptor);

    int m_inotify = -1;
    int m_maxWatches;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QHash<int, Watch> m_watches;
    std::map<QString, int> m_paths; // Watched folders; sorted so that a folder's children follow it
};

} // namespace kgt
