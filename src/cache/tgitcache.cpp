// SPDX-FileCopyrightText: 2005-2008, 2011-2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/TGITCache.cpp (WinMain): the status cache's process

#include "cacheinterface.hpp"
#include "cacheservice.hpp"
#include "gitstatuscache.hpp"

#include <QCoreApplication>
#include <QDBusConnection>

#include <sys/resource.h>

#include <chrono>

namespace
{

using namespace std::chrono_literals;

constexpr std::chrono::minutes idleExitDelay = 5min;
constexpr int backgroundNice = 10; // CFolderCrawler::WorkerThread's THREAD_MODE_BACKGROUND_BEGIN; git inherits it

} // namespace

int main(int argc, char *argv[])
{
    using namespace kgt;

    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kortoisegitcache"));
    setpriority(PRIO_PROCESS, 0, backgroundNice);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return 1;
    }

    GitStatusCache cache;
    CacheService service(cache, idleExitDelay);
    QObject::connect(&service, &CacheService::idle, &app, &QCoreApplication::quit);
    if (!bus.registerObject(QString(cacheObjectPath), &service, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        return 1;
    }
    // Owning the bus name is TGitCache's single-instance mutex: if another instance has it, that one does the work
    if (!bus.registerService(QString(cacheServiceName))) {
        return 0;
    }
    return QCoreApplication::exec();
}
