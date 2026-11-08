// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "remotecachelink.hpp"
#include "cacheservice.hpp"
#include "gitfolderstatus.hpp"
#include "gitstatuscache.hpp"
#include "shellext.hpp"
#include "testrepo.hpp"
#include "utils/buildconfig.hpp"

#include <KFileItem>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <signal.h>

#include <chrono>
#include <optional>
#include <tuple>

namespace
{

using namespace kgt;
using namespace std::chrono_literals;

constexpr int startTimeoutMs = 15000;
constexpr qint64 quickMs = 2000; // Comfortably above the link's own timeout, far below a hang

/**
 * Asks until the cache answers: the first questions start it and its crawl.
 * @param link The link.
 * @param directory The folder.
 * @returns The answer.
 */
std::optional<CacheFolderStatus> waitForAnswer(RemoteCacheLink& link, const QString& directory)
{
    std::optional<CacheFolderStatus> status;
    std::ignore = QTest::qWaitFor(
        [&] {
            status = link.folderStatus(directory);
            return status.has_value();
        },
        startTimeoutMs);
    return status;
}

/**
 * Stops the cache process owning the real service name, if any.
 */
void stopCache()
{
    const QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(QString(cacheServiceName));
    if (pid.isValid()) {
        kill(static_cast<pid_t>(pid.value()), SIGTERM);
        std::ignore = QTest::qWaitFor(
            [] {
                return !QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(cacheServiceName)).value();
            },
            startTimeoutMs);
    }
}

} // namespace

/**
 * The plugin's side of the conversation with the cache process, on a private session bus (see tests/CMakeLists.txt).
 */
class RemoteCacheLinkTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
        QVERIFY(QDBusConnection::sessionBus().isConnected());
    }

    void cleanup()
    {
        stopCache();
    }

    void noAnswerWhenTheCacheIsMissing()
    {
        RemoteCacheLink link(RemoteCacheLink::Options{.executable = {}, .service = QStringLiteral("org.kde.kortoisegitcache.nobody")});
        const TestRepo repo;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 5; ++i) {
            QVERIFY(!link.folderStatus(repo.path()).has_value());
        }
        QVERIFY(timer.elapsed() < quickMs);
    }

    void pluginFallsBackToGitWithoutTheCache()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/b/file.txt"), "one\n");
        repo.commitAll(QStringLiteral("first"));
        repo.write(QStringLiteral("a/b/file.txt"), "two\n");

        RemoteCacheLink link(RemoteCacheLink::Options{.executable = {}, .service = QStringLiteral("org.kde.kortoisegitcache.nobody")});
        // ShellExt::beginRetrieval's choice, for a cache that gives no answer
        QVERIFY(!link.folderStatus(repo.path()).has_value());
        QCOMPARE(GitFolderStatus::fetch(repo.path()).itemVersion(QStringLiteral("a"), true), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
    }

    void aHungCacheCostsOnlyTheTimeout()
    {
        // Owns a name but never answers: registered on a connection whose thread is this one, blocked in the call below
        QDBusConnection hung = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("hung"));
        QVERIFY(hung.registerService(QStringLiteral("org.kde.kortoisegitcache.hung")));
        GitStatusCache cache;
        CacheService service(cache, 1h);
        QVERIFY(hung.registerObject(QString(cacheObjectPath), &service, QDBusConnection::ExportScriptableSlots));

        RemoteCacheLink link(RemoteCacheLink::Options{.executable = {}, .service = QStringLiteral("org.kde.kortoisegitcache.hung")});
        const TestRepo repo;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!link.folderStatus(repo.path()).has_value());
        QVERIFY(timer.elapsed() < quickMs);
        // And it is then left alone rather than waited for on every folder
        timer.restart();
        QVERIFY(!link.folderStatus(repo.path()).has_value());
        QVERIFY(timer.elapsed() < 50);
        QDBusConnection::disconnectFromBus(QStringLiteral("hung"));
    }

    void startsTheCacheAndHearsAboutChanges()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("src/lib/foo.cpp"), "one\n");
        repo.commitAll(QStringLiteral("first"));

        RemoteCacheLink link(RemoteCacheLink::Options{.executable = QStringLiteral(KGT_CACHE_EXE)});
        QSignalSpy changed(&link, &RemoteCacheLink::itemsChanged);
        const std::optional<CacheFolderStatus> first = waitForAnswer(link, repo.path());
        QVERIFY(first.has_value());
        QVERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(cacheServiceName)).value());

        repo.write(QStringLiteral("src/lib/foo.cpp"), "two\n");
        QVERIFY(changed.wait(startTimeoutMs));
        QVERIFY(changed.constFirst().constFirst().toStringList().contains(QFileInfo(repo.path()).canonicalFilePath() + QStringLiteral("/src")));

        const std::optional<CacheFolderStatus> second = link.folderStatus(repo.path());
        if (!second) {
            QFAIL("No answer after the change");
        }
        const qsizetype index = second->names.indexOf(QStringLiteral("src"));
        QVERIFY(index >= 0);
        QCOMPARE(unpackItemState(second->states.at(index)).status, GitWcStatus::ModifiedUnstaged);
    }

    void aSaveDeepDownRefreshesTheFolderShown()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("src/lib/foo.cpp"), "one\n");
        repo.commitAll(QStringLiteral("first"));

        // Starts the cache: the plugin looks for it beside its own .so, which a test isn't
        RemoteCacheLink link(RemoteCacheLink::Options{.executable = QStringLiteral(KGT_CACHE_EXE)});
        QVERIFY(waitForAnswer(link, repo.path()).has_value());

        ShellExt plugin(nullptr, {});
        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        QCoreApplication::processEvents(); // The retrieval tells the main thread which folder it is showing

        QSignalSpy refreshed(&plugin, &KVersionControlPlugin::itemVersionsChanged);
        QElapsedTimer timer;
        timer.start();
        repo.write(QStringLiteral("src/lib/foo.cpp"), "two\n");
        if constexpr (!useStatusCache) {
            // Compiled out, the plugin hears nothing from the cache, even with one running
            QVERIFY(!refreshed.wait(1500));
            return;
        }
        QVERIFY(refreshed.wait(startTimeoutMs));
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));

        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        QCOMPARE(plugin.itemVersion(KFileItem(QUrl::fromLocalFile(repo.filePath(QStringLiteral("src"))))),
                 KVersionControlPlugin::LocallyModifiedUnstagedVersion);
        QTest::qWait(500);
        QCOMPARE(refreshed.size(), 1);
    }

    void aSecondInstanceLeavesTheFirstInCharge()
    {
        RemoteCacheLink link(RemoteCacheLink::Options{.executable = QStringLiteral(KGT_CACHE_EXE)});
        const TestRepo repo;
        QVERIFY(waitForAnswer(link, repo.path()).has_value());

        QProcess second;
        second.start(QStringLiteral(KGT_CACHE_EXE), QStringList{});
        QVERIFY(second.waitForFinished(startTimeoutMs));
        QCOMPARE(second.exitCode(), 0);
        QVERIFY(link.folderStatus(repo.path()).has_value());
    }

    void aCrashIsOnlyAMissingAnswer()
    {
        RemoteCacheLink link(RemoteCacheLink::Options{.executable = QStringLiteral(KGT_CACHE_EXE)});
        const TestRepo repo;
        QVERIFY(waitForAnswer(link, repo.path()).has_value());

        const QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(QString(cacheServiceName));
        QVERIFY(pid.isValid());
        kill(static_cast<pid_t>(pid.value()), SIGKILL);
        QVERIFY(QTest::qWaitFor(
            [] {
                return !QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(cacheServiceName)).value();
            },
            startTimeoutMs));

        QElapsedTimer timer;
        timer.start();
        QVERIFY(!link.folderStatus(repo.path()).has_value());
        QVERIFY(timer.elapsed() < quickMs);
    }

    void exitsWhenTheLastClientGoes()
    {
        GitStatusCache cache;
        CacheService service(cache, 300ms);
        QSignalSpy idle(&service, &CacheService::idle);
        QVERIFY(QDBusConnection::sessionBus().registerObject(QString(cacheObjectPath), &service, QDBusConnection::ExportScriptableSlots));
        const auto unregister = qScopeGuard([] {
            QDBusConnection::sessionBus().unregisterObject(QString(cacheObjectPath));
        });

        // With no client at all it times out from the start
        QVERIFY(idle.wait(startTimeoutMs));
        idle.clear();

        // A client that stays keeps it alive, however long it is quiet
        {
            // disconnectFromBus only closes a connection once no object refers to it
            const QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("client"));
            QDBusMessage call = QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(),
                                                               QString(cacheObjectPath),
                                                               QString(cacheInterfaceName),
                                                               QStringLiteral("folderStatus"));
            call << QDir::tempPath();
            QVERIFY(client.send(call));
        }
        QTest::qWait(1000);
        QCOMPARE(idle.size(), 0);

        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
        QVERIFY(idle.wait(startTimeoutMs));
    }
};

QTEST_MAIN(RemoteCacheLinkTest)

#include "remotecachelinktest.moc"
