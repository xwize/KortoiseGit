// SPDX-FileCopyrightText: 2005-2008, 2011-2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Plays the role of the pipe threads in TortoiseGit src/TGitCache/TGITCache.cpp

#include "cacheservice.hpp"

#include <QDBusConnection>
#include <QDBusMessage>

#include <chrono>

namespace
{

using namespace std::chrono_literals;

constexpr std::chrono::seconds busCheckInterval = 60s;

} // namespace

namespace kgt
{

CacheService::CacheService(GitStatusCache& cache, std::chrono::milliseconds idleDelay, QObject *parent)
    : QObject(parent)
    , m_cache(&cache)
{
    connect(m_cache, &GitStatusCache::pathsChanged, this, &CacheService::itemsChanged);

    m_clientWatcher.setConnection(QDBusConnection::sessionBus());
    m_clientWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_clientWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &CacheService::removeClient);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(idleDelay);
    connect(&m_idleTimer, &QTimer::timeout, this, &CacheService::idle);
    m_idleTimer.start();

    m_busCheckTimer.setInterval(busCheckInterval);
    connect(&m_busCheckTimer, &QTimer::timeout, this, [this] {
        if (!QDBusConnection::sessionBus().isConnected()) {
            Q_EMIT idle();
        }
    });
    m_busCheckTimer.start();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): QtDBus maps these reference parameters to the reply's arguments
bool CacheService::folderStatus(const QString& directory, int& wholeFolder, bool& hasUnversioned, QStringList& names, QList<int>& states)
{
    if (calledFromDBus()) {
        addClient(message().service());
    }
    wholeFolder = -1;
    hasUnversioned = false;
    const std::optional<CacheFolderStatus> status = m_cache->folderStatus(directory);
    if (!status) {
        return false;
    }
    if (status->wholeFolder) {
        wholeFolder = static_cast<int>(*status->wholeFolder);
    }
    hasUnversioned = status->hasUnversioned;
    names = status->names;
    states = status->states;
    return true;
}

void CacheService::addClient(const QString& client)
{
    if (client.isEmpty() || m_clients.contains(client)) {
        return;
    }
    m_clients.insert(client);
    m_clientWatcher.addWatchedService(client);
    m_idleTimer.stop();
}

void CacheService::removeClient(const QString& client)
{
    m_clients.remove(client);
    m_clientWatcher.removeWatchedService(client);
    if (m_clients.isEmpty()) {
        m_idleTimer.start();
    }
}

} // namespace kgt
