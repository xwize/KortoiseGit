// SPDX-FileCopyrightText: 2003-2014, 2017 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2017, 2019, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseShell/RemoteCacheLink.h: the plugin's side of its conversation with the status
 * cache process.
 */

#pragma once

#include "cache/cacheinterface.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>

namespace kgt
{

/**
 * Asks kortoisegitcache for a folder's overlays and hears when they change. "We NEVER MUST BLOCK THE SHELL": calls are
 * only made from Dolphin's retrieval thread and have a short timeout, and any failure just means no answer, for which
 * the plugin reads git itself.
 */
class RemoteCacheLink : public QObject
{
    Q_OBJECT

public:
    /**
     * Where to find the cache.
     */
    struct Options {
        QString executable; // The cache program, for when D-Bus can't start it
        QString service = QString(cacheServiceName); // The bus name to talk to
    };

    /**
     * @param options Where to find the cache.
     * @param parent Owner.
     */
    explicit RemoteCacheLink(const Options& options, QObject *parent = nullptr);

    /**
     * Ported from CRemoteCacheLink::GetStatusFromRemoteCache, for all of a folder's items. Only for a worker thread.
     * @param directory The folder Dolphin shows.
     * @returns The cache's answer, or nothing if it is missing, slow or doesn't know the folder.
     */
    [[nodiscard]] std::optional<CacheFolderStatus> folderStatus(const QString& directory);

Q_SIGNALS:
    /**
     * @param paths The items whose status changed. Emitted on the thread this object lives on.
     */
    void itemsChanged(const QStringList& paths);

private:
    /**
     * Ported from CRemoteCacheLink::RunTGitCacheProcess: starts the cache, detached, no more often than
     * GetStatusFromRemoteCache's m_lastTimeout allows.
     */
    void runCacheProcess();

    QString m_executable;
    QString m_service;
    std::atomic<qint64> m_retryAt = 0; // Monotonic ms: no calls before this, after a failure
    std::atomic<qint64> m_nextStartAt = 0; // Monotonic ms: no starting the cache before this
};

} // namespace kgt
