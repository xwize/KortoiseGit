// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"
#include "testrepo.hpp"

#include <KFileItem>

#include <QAction>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace
{

using namespace kgt;

/**
 * @param path A local file or folder.
 * @returns Dolphin's item for it.
 */
KFileItem item(const QString& path)
{
    return KFileItem(QUrl::fromLocalFile(path));
}

} // namespace

/**
 * ShellExt as Dolphin drives it: retrieval, overlays, and the refresh after the index changes.
 */
class ShellExtTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void noOverlaysOutsideARepository()
    {
        ShellExt plugin(nullptr, {});
        const QTemporaryDir notARepo;
        QVERIFY(!plugin.beginRetrieval(notARepo.path() + QLatin1Char('/')));
        plugin.endRetrieval();
    }

    void reportsItemVersions()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.write(QStringLiteral("dir/b.txt"), "b\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("new.txt"), "new\n");

        ShellExt plugin(nullptr, {});
        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        QCOMPARE(plugin.itemVersion(item(repo.filePath(QStringLiteral("a.txt")))), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
        QCOMPARE(plugin.itemVersion(item(repo.filePath(QStringLiteral("new.txt")))), KVersionControlPlugin::UnversionedVersion);
        QCOMPARE(plugin.itemVersion(item(repo.filePath(QStringLiteral("dir")))), KVersionControlPlugin::NormalVersion);
        plugin.endRetrieval();

        // A failed retrieval leaves nothing behind from the previous one
        const QTemporaryDir notARepo;
        QVERIFY(!plugin.beginRetrieval(notARepo.path() + QLatin1Char('/')));
        QCOMPARE(plugin.itemVersion(item(repo.filePath(QStringLiteral("a.txt")))), KVersionControlPlugin::UnversionedVersion);
    }

    void commitEntryShowsBranch()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("new.txt"), "new\n");

        ShellExt plugin(nullptr, {});
        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        plugin.endRetrieval();
        // Let the folder's state reach the main thread
        QCoreApplication::processEvents();

        QList<QAction *> actions = plugin.versionControlActions({item(repo.filePath(QStringLiteral("a.txt")))});
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.constFirst()->text(), QStringLiteral("Git &Commit -> \"main\"..."));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{repo.filePath(QStringLiteral("a.txt"))});

        // An unversioned file can't be committed from its menu, as in TortoiseGit
        actions = plugin.versionControlActions({item(repo.filePath(QStringLiteral("new.txt")))});
        QCOMPARE(actions.size(), 1);
        QVERIFY(!actions.constFirst()->text().contains(QStringLiteral("Commit")));

        // The folder shown, when nothing is selected
        actions = plugin.versionControlActions({item(repo.path())});
        QCOMPARE(actions.size(), 2);
    }

    void stagingRefreshesOverlays()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("a.txt"), "changed\n");

        ShellExt plugin(nullptr, {});
        QSignalSpy changed(&plugin, &KVersionControlPlugin::itemVersionsChanged);
        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        plugin.endRetrieval();
        // Let the index watch be set up on this (the main) thread
        QCoreApplication::processEvents();
        QVERIFY(changed.isEmpty());

        // As if from a terminal: only .git/index changes, nothing Dolphin's folder listing would notice
        repo.run({QStringLiteral("add"), QStringLiteral("a.txt")});
        QVERIFY(changed.wait(5000));
        // One git command, one refresh: its several index events are collected into one
        QVERIFY(!changed.wait(1500));
        QCOMPARE(changed.count(), 1);
        changed.clear();

        QVERIFY(plugin.beginRetrieval(repo.path() + QLatin1Char('/')));
        QCOMPARE(plugin.itemVersion(item(repo.filePath(QStringLiteral("a.txt")))), KVersionControlPlugin::LocallyModifiedVersion);
        plugin.endRetrieval();

        // The retrieval itself must not touch the index, or every refresh would trigger another
        QVERIFY(!changed.wait(1000));
    }
};

QTEST_MAIN(ShellExtTest)

#include "shellexttest.moc"
