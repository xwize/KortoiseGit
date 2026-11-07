// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"
#include "testrepo.hpp"

#include <KFileItem>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <tuple>

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

/**
 * @param plugin The plugin, after a retrieval.
 * @param items The selected items.
 * @returns The texts of the context menu entries the plugin offers for them.
 */
QStringList menuTexts(const ShellExt& plugin, const KFileItemList& items)
{
    QStringList texts;
    for (const QAction *action : plugin.versionControlActions(items)) {
        texts.append(action->text());
    }
    return texts;
}

/**
 * Asks the plugin about every item of a folder, as Dolphin does after each retrieval.
 * @param plugin The plugin.
 * @param directory The folder.
 * @returns Whether the retrieval succeeded.
 */
bool retrieve(ShellExt& plugin, const QString& directory)
{
    if (!plugin.beginRetrieval(directory + QLatin1Char('/'))) {
        return false;
    }
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& entry : entries) {
        std::ignore = plugin.itemVersion(item(entry.absoluteFilePath()));
    }
    plugin.endRetrieval();
    // Let the folder's state reach the main thread, where the context menu reads it
    QCoreApplication::processEvents();
    return true;
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

    void offersAddForUnversionedItems()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "*.log\n");
        repo.write(QStringLiteral("tracked.txt"), "a\n");
        repo.write(QStringLiteral("clean/a.txt"), "a\n");
        repo.write(QStringLiteral("withnew/a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("new.txt"), "new\n");
        repo.write(QStringLiteral("build.log"), "log\n");
        repo.write(QStringLiteral("withnew/deeper/new.txt"), "new\n");
        repo.write(QStringLiteral("untracked/b.txt"), "b\n");

        ShellExt plugin(nullptr, {});
        QVERIFY(retrieve(plugin, repo.path()));
        const QString commit = QStringLiteral("Git &Commit -> \"main\"...");
        const QString log = QStringLiteral("Show &log");
        const QString add = QStringLiteral("&Add...");
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("tracked.txt")))}), (QStringList{commit, log}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("clean")))}), (QStringList{commit, log}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral(".git")))}), QStringList{log});
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("new.txt")))}), (QStringList{log, add}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("build.log")))}), (QStringList{log, add}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("withnew")))}), (QStringList{commit, log, add}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("untracked")))}), (QStringList{log, add}));
        // One addable item among others is enough, as in TortoiseGit
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("tracked.txt"))), item(repo.filePath(QStringLiteral("new.txt")))}),
                 (QStringList{commit, log, add}));
        // Nothing selected: Dolphin asks about the folder it shows
        QCOMPARE(menuTexts(plugin, {item(repo.path())}), (QStringList{commit, log, add}));

        // An expanded folder is read too; the items of the first folder keep their entries
        QVERIFY(retrieve(plugin, repo.filePath(QStringLiteral("clean"))));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("clean/a.txt")))}), (QStringList{commit, log}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("new.txt")))}), (QStringList{log, add}));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("clean")))}), (QStringList{commit, log}));

        // Once the file is added, the next retrieval no longer offers it
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});
        QVERIFY(retrieve(plugin, repo.path()));
        QCOMPARE(menuTexts(plugin, {item(repo.filePath(QStringLiteral("new.txt")))}), (QStringList{commit, log}));
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
        QCOMPARE(actions.size(), 2); // Show log and Add
        QVERIFY(!actions.constFirst()->text().contains(QStringLiteral("Commit")));

        // The folder shown, when nothing is selected
        actions = plugin.versionControlActions({item(repo.path())});
        QCOMPARE(actions.size(), 3); // Commit, Show log, and Add for new.txt
        QCOMPARE(actions.constFirst()->text(), QStringLiteral("Git &Commit -> \"main\"..."));
    }

    void createRepositoryEntryOutsideRepositories()
    {
        const ShellExt plugin(nullptr, {});
        const QTemporaryDir plain;
        const QList<QAction *> actions = plugin.outOfVersionControlActions({item(plain.path())});
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.constFirst()->text(), QStringLiteral("Git Create repositor&y here..."));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{plain.path()});

        // Not for a file, several items, a working tree or a bare repository
        const QString file = plain.filePath(QStringLiteral("file.txt"));
        QFile created(file);
        QVERIFY(created.open(QIODevice::WriteOnly));
        QVERIFY(plugin.outOfVersionControlActions({item(file)}).isEmpty());
        QVERIFY(plugin.outOfVersionControlActions({item(plain.path()), item(file)}).isEmpty());
        const TestRepo repo;
        QVERIFY(plugin.outOfVersionControlActions({item(repo.path())}).isEmpty());
        const QString bare = plain.filePath(QStringLiteral("bare.git"));
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), bare});
        QVERIFY(plugin.outOfVersionControlActions({item(bare)}).isEmpty());
    }

    void newRepositoryRefreshesDolphin()
    {
        ShellExt plugin(nullptr, {});
        QSignalSpy changed(&plugin, &KVersionControlPlugin::itemVersionsChanged);
        const QTemporaryDir folder;
        plugin.watchForNewRepository(folder.path());
        QVERIFY(!changed.wait(1000));

        const TestRepo helper;
        helper.run({QStringLiteral("init"), QStringLiteral("--quiet"), folder.path()});
        QVERIFY(changed.wait(5000));
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
