// SPDX-FileCopyrightText: 2005-2008, 2011-2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Plays the role of TortoiseGit src/TGitCache/TGITCache.cpp's pipe threads: the cache's connection to its clients, here
 * over the session bus.
 */

#pragma once

#include "gitstatuscache.hpp"

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>

namespace kgt
{

/**
 * The object kortoisegitcache publishes on the session bus. It answers the plugin's requests, relays notifications, and
 * says when no client has been around for a while or the session has ended.
 */
class CacheService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kortoisegitcache")

public:
    /**
     * @param cache The cache to serve.
     * @param idleDelay How long without any client before idle() is emitted.
     * @param parent Owner.
     */
    CacheService(GitStatusCache& cache, std::chrono::milliseconds idleDelay, QObject *parent = nullptr);

public Q_SLOTS:
    /**
     * The request behind CRemoteCacheLink::GetStatusFromRemoteCache. Answers from memory at once.
     * @param directory The folder Dolphin shows.
     * @param[out] wholeFolder The state of a folder that lies wholly inside an ignored one, otherwise -1.
     * @param[out] hasUnversioned Whether an untracked file lies somewhere in the folder.
     * @param[out] names The items that have a state.
     * @param[out] states Their packed states.
     * @returns Whether the cache knows the folder; if false the caller reads git itself.
     */
    Q_SCRIPTABLE bool folderStatus(const QString& directory, int& wholeFolder, bool& hasUnversioned, QStringList& names, QList<int>& states);

Q_SIGNALS:
    /**
     * CShellUpdater's notification, sent to every client.
     * @param paths The items whose status changed.
     */
    Q_SCRIPTABLE void itemsChanged(const QStringList& paths);

    /**
     * No client has been around for the idle delay, or the session bus has gone: time to exit.
     */
    void idle();

private:
    void addClient(const QString& client);
    void removeClient(const QString& client);

    GitStatusCache *m_cache;
    QDBusServiceWatcher m_clientWatcher;
    QSet<QString> m_clients;
    QTimer m_idleTimer;
    QTimer m_busCheckTimer; // The session's bus going away is the session ending
};

} // namespace kgt
