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

/**
 * TortoiseGit's menu texts.
 */
struct MenuTexts {
    QString commit = QStringLiteral("Git &Commit -> \"main\"...");
    QString log = QStringLiteral("Show &log");
    QString push = QStringLiteral("Pus&h...");
    QString revert = QStringLiteral("Re&vert...");
    QString add = QStringLiteral("&Add...");
    QString clone = QStringLiteral("Git Clone...");
    QString createRepo = QStringLiteral("Git Create repositor&y here...");
    QString refs = QStringLiteral("Browse References");
    QString pull = QStringLiteral("&Pull...");
    QString fetch = QStringLiteral("F&etch...");
    QString merge = QStringLiteral("&Merge...");
    QString mergeAbort = QStringLiteral("Abort &Merge");
    QString switchTo = QStringLiteral("S&witch/Checkout...");
    QString rebase = QStringLiteral("Rebase...");
    QString diff = QStringLiteral("&Diff");
    QString prevDiff = QStringLiteral("&Diff with previous version");
};

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

    void menuForItemKinds()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "*.log\nvendor/\n");
        repo.write(QStringLiteral("tracked.txt"), "a\n");
        repo.write(QStringLiteral("modified.txt"), "a\n");
        repo.write(QStringLiteral("clean/a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("modified.txt"), "changed\n");
        repo.write(QStringLiteral("staged.txt"), "new\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});
        repo.write(QStringLiteral("new.txt"), "new\n");
        repo.write(QStringLiteral("build.log"), "log\n");
        repo.write(QStringLiteral("untracked/b.txt"), "b\n");
        repo.write(QStringLiteral("vendor/lib.txt"), "lib\n");

        // Decided from each item's own path on every request: no retrieval has run
        const ShellExt plugin(nullptr, {});
        const auto menu = [&plugin, &repo](const QStringList& names) {
            KFileItemList items;
            for (const QString& name : names) {
                items.append(item(name.isEmpty() ? repo.path() : repo.filePath(name)));
            }
            return menuTexts(plugin, items);
        };
        // The working tree root, as when nothing is selected, and every folder in it, versioned or not
        const QStringList versionedFolder{pull, fetch, push, commit, log, refs, rebase, revert, switchTo, merge, add};
        QCOMPARE(menu({QString()}), versionedFolder);
        QCOMPARE(menu({QStringLiteral("clean")}), versionedFolder);
        QCOMPARE(menu({QStringLiteral("untracked")}), versionedFolder);
        // An ignored folder is where a nested repository belongs
        QCOMPARE(menu({QStringLiteral("vendor")}),
                 (QStringList{clone, pull, fetch, push, commit, log, refs, rebase, revert, switchTo, merge, createRepo, add}));

        QCOMPARE(menu({QStringLiteral("tracked.txt")}), (QStringList{commit, diff, prevDiff, log}));
        QCOMPARE(menu({QStringLiteral("modified.txt")}), (QStringList{commit, diff, prevDiff, log, revert}));
        // An added file has no history yet
        QCOMPARE(menu({QStringLiteral("staged.txt")}), (QStringList{commit, diff, revert}));
        QCOMPARE(menu({QStringLiteral("new.txt")}), QStringList{add});
        QCOMPARE(menu({QStringLiteral("build.log")}), QStringList{add});

        // Nothing for .git and what is in it
        QVERIFY(menu({QStringLiteral(".git")}).isEmpty());
        QVERIFY(menu({QStringLiteral(".git/config")}).isEmpty());

        // Several items: the first one decides, and Push and Show log are for one item only
        QCOMPARE(menu({QStringLiteral("tracked.txt"), QStringLiteral("new.txt")}), (QStringList{commit, diff}));
        QCOMPARE(menu({QStringLiteral("new.txt"), QStringLiteral("tracked.txt")}), (QStringList{diff, add}));
        QCOMPARE(menu({QStringLiteral("modified.txt"), QStringLiteral("tracked.txt")}), (QStringList{commit, diff, revert}));
        // Diff takes exactly two files, and no folder
        QCOMPARE(menu({QStringLiteral("tracked.txt"), QStringLiteral("modified.txt"), QStringLiteral("staged.txt")}), QStringList{commit});
        QCOMPARE(menu({QStringLiteral("tracked.txt"), QStringLiteral("clean")}), QStringList{commit});
        QCOMPARE(menu({QStringLiteral("clean"), QStringLiteral("untracked")}), (QStringList{commit, revert, add}));
        // Except the working tree root, which keeps Pull, Fetch and Push
        QCOMPARE(menu({QString(), QStringLiteral("clean")}), (QStringList{pull, fetch, push, commit, revert, add}));

        // The state is read afresh: once added, the file is no longer offered for Add
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});
        QCOMPARE(menu({QStringLiteral("new.txt")}), (QStringList{commit, diff, revert}));
    }

    void menuFollowsRepositoryMovedIntoNewFolder()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
        // The repository is browsed while a new folder sits in it, then everything, .git included, is moved into
        // that folder: the folder is now a working tree's root, and the old root is in no repository
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.write(QStringLiteral("dir/b.txt"), "b\n");
        repo.commitAll(QStringLiteral("initial"));
        const QString moved = repo.filePath(QStringLiteral("moved"));
        QVERIFY(QDir().mkdir(moved));

        ShellExt plugin(nullptr, {});
        QVERIFY(retrieve(plugin, repo.path()));
        QCOMPARE(menuTexts(plugin, {item(repo.path())}), (QStringList{pull, fetch, push, commit, log, refs, rebase, revert, switchTo, merge, add}));

        for (const QString& name : {QStringLiteral(".git"), QStringLiteral("a.txt"), QStringLiteral("dir")}) {
            QVERIFY(QDir(repo.path()).rename(name, QStringLiteral("moved/") + name));
        }

        // Shown, or seen from the old root, which Dolphin then asks through outOfVersionControlActions()
        const QStringList root{pull, fetch, push, commit, log, refs, rebase, revert, switchTo, merge, add};
        QCOMPARE(menuTexts(plugin, {item(moved)}), root);
        QCOMPARE(plugin.outOfVersionControlActions({item(moved)}).size(), root.size());
        QCOMPARE(plugin.outOfVersionControlActions({item(moved)}).at(3)->text(), commit);
        QCOMPARE(menuTexts(plugin, {item(QDir(moved).filePath(QStringLiteral("a.txt")))}), (QStringList{commit, diff, prevDiff, log}));
        QCOMPARE(menuTexts(plugin, {item(QDir(moved).filePath(QStringLiteral("dir")))}),
                 (QStringList{pull, fetch, push, commit, log, refs, rebase, revert, switchTo, merge, add}));
        // The old root is a plain folder now
        QCOMPARE(menuTexts(plugin, {item(repo.path())}), (QStringList{clone, createRepo}));

        // The same as a fresh plugin, as after restarting Dolphin, after reading the moved folder
        QVERIFY(retrieve(plugin, moved));
        const ShellExt fresh(nullptr, {});
        QCOMPARE(menuTexts(plugin, {item(moved)}), menuTexts(fresh, {item(moved)}));
    }

    void offersRevertForChangedFiles()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("clean.txt"), "a\n");
        repo.write(QStringLiteral("modified.txt"), "a\n");
        repo.write(QStringLiteral("staged.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("modified.txt"), "changed\n");
        repo.write(QStringLiteral("staged.txt"), "changed\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});
        repo.write(QStringLiteral("new.txt"), "new\n");

        ShellExt plugin(nullptr, {});
        QVERIFY(retrieve(plugin, repo.path()));
        const auto offersRevert = [&plugin, &repo](const QStringList& names) {
            KFileItemList items;
            for (const QString& name : names) {
                items.append(item(repo.filePath(name)));
            }
            return menuTexts(plugin, items).contains(QStringLiteral("Re&vert..."));
        };
        QVERIFY(offersRevert({QStringLiteral("modified.txt")}));
        QVERIFY(offersRevert({QStringLiteral("staged.txt")}));
        QVERIFY(!offersRevert({QStringLiteral("clean.txt")}));
        QVERIFY(!offersRevert({QStringLiteral("new.txt")}));
        // The first item decides, as in TortoiseGit
        QVERIFY(offersRevert({QStringLiteral("modified.txt"), QStringLiteral("clean.txt")}));
        QVERIFY(!offersRevert({QStringLiteral("clean.txt"), QStringLiteral("modified.txt")}));
    }

    void commitEntryShowsBranch()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
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
        QCOMPARE(actions.size(), 4);
        QCOMPARE(actions.constFirst()->text(), QStringLiteral("Git &Commit -> \"main\"..."));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{repo.filePath(QStringLiteral("a.txt"))});

        // An unversioned file can't be committed from its menu, as in TortoiseGit
        actions = plugin.versionControlActions({item(repo.filePath(QStringLiteral("new.txt")))});
        QCOMPARE(actions.size(), 1); // Add
        QCOMPARE(actions.constFirst()->text(), add);

        // The folder shown, when nothing is selected
        actions = plugin.versionControlActions({item(repo.path())});
        QCOMPARE(actions.size(), 11); // Pull, Fetch, Push, Commit, Show log, Browse References, Rebase, Revert, Switch/Checkout, Merge, and Add for new.txt
        QCOMPARE(actions.at(0)->text(), QStringLiteral("&Pull..."));
        QCOMPARE(actions.at(0)->data().toStringList(), QStringList{repo.path()});
        QCOMPARE(actions.at(1)->text(), QStringLiteral("F&etch..."));
        QCOMPARE(actions.at(2)->text(), QStringLiteral("Pus&h..."));
        QCOMPARE(actions.at(3)->text(), QStringLiteral("Git &Commit -> \"main\"..."));
    }

    void createRepositoryEntryOutsideRepositories()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
        const ShellExt plugin(nullptr, {});
        const QTemporaryDir plain;
        const QList<QAction *> actions = plugin.outOfVersionControlActions({item(plain.path())});
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Git Clone..."));
        QCOMPARE(actions.at(0)->data().toStringList(), QStringList{plain.path()});
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Git Create repositor&y here..."));
        QCOMPARE(actions.at(1)->data().toStringList(), QStringList{plain.path()});

        // Not for a file or several items
        const QString file = plain.filePath(QStringLiteral("file.txt"));
        QFile created(file);
        QVERIFY(created.open(QIODevice::WriteOnly));
        QVERIFY(plugin.outOfVersionControlActions({item(file)}).isEmpty());
        QVERIFY(plugin.outOfVersionControlActions({item(plain.path()), item(file)}).isEmpty());

        // A working tree seen from outside gets its own menu
        const TestRepo repo;
        QCOMPARE(plugin.outOfVersionControlActions({item(repo.path())}).at(3)->text(), commit);
        // A bare repository: Fetch, Show log and Browse References (TortoiseGit's Push too, which PushCommand
        // doesn't support yet)
        const QString bare = plain.filePath(QStringLiteral("bare.git"));
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), bare});
        QCOMPARE(menuTexts(plugin, {item(bare)}), (QStringList{fetch, log, refs}));
        // A folder whose .git isn't a repository: nothing
        const QString broken = plain.filePath(QStringLiteral("broken"));
        QVERIFY(QDir().mkpath(broken + QStringLiteral("/.git")));
        QVERIFY(plugin.outOfVersionControlActions({item(broken)}).isEmpty());
    }

    void noPullDuringMergeOrBisect()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const ShellExt plugin(nullptr, {});
        QVERIFY(menuTexts(plugin, {item(repo.path())}).contains(pull));

        // MenuInfo: Pull is hidden while a merge awaits its commit or a bisect runs; Fetch stays
        for (const QString& marker : {QStringLiteral(".git/MERGE_HEAD"), QStringLiteral(".git/BISECT_START")}) {
            repo.write(marker, "x\n");
            const QStringList texts = menuTexts(plugin, {item(repo.path())});
            QVERIFY(!texts.contains(pull));
            QVERIFY(texts.contains(fetch));
            repo.remove(marker);
        }
    }

    void mergeEntries()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff] = MenuTexts();
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.write(QStringLiteral("dir/b.txt"), "b\n");
        repo.commitAll(QStringLiteral("initial"));
        const ShellExt plugin(nullptr, {});
        const auto menu = [&plugin, &repo](const QString& name) {
            return menuTexts(plugin, {item(name.isEmpty() ? repo.path() : repo.filePath(name))});
        };

        // MenuInfo: Merge for one folder, and no Abort Merge while no merge runs
        QVERIFY(menu({}).contains(merge));
        QVERIFY(menu(QStringLiteral("dir")).contains(merge));
        QVERIFY(!menu(QStringLiteral("a.txt")).contains(merge));
        // MenuInfo: Switch/Checkout for exactly one folder in a working tree
        QVERIFY(menu(QStringLiteral("dir")).contains(switchTo));
        QVERIFY(!menu(QStringLiteral("a.txt")).contains(switchTo));
        QVERIFY(!menuTexts(plugin, {item(repo.path()), item(repo.filePath(QStringLiteral("dir")))}).contains(switchTo));
        QVERIFY(!menu({}).contains(mergeAbort));

        // While a merge awaits its commit: Abort Merge, before Revert, for versioned files and folders, and no Merge
        repo.write(QStringLiteral(".git/MERGE_HEAD"), "x\n");
        QCOMPARE(menu({}), (QStringList{fetch, push, commit, log, refs, mergeAbort, revert, switchTo, add}));
        QCOMPARE(menu(QStringLiteral("a.txt")), (QStringList{commit, diff, prevDiff, log, mergeAbort}));
        repo.remove(QStringLiteral(".git/MERGE_HEAD"));

        // And no Merge or Rebase during a bisect; Switch/Checkout stays
        repo.write(QStringLiteral(".git/BISECT_START"), "x\n");
        QVERIFY(!menu({}).contains(merge));
        QVERIFY(!menu({}).contains(rebase));
        QVERIFY(menu({}).contains(switchTo));
        repo.remove(QStringLiteral(".git/BISECT_START"));
    }

    void cloneEntryForIgnoredFolders()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "vendor/\n");
        repo.write(QStringLiteral("src/a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("vendor/lib.txt"), "lib\n");

        ShellExt plugin(nullptr, {});
        QVERIFY(retrieve(plugin, repo.path()));
        // TortoiseGit offers Clone inside a working tree only on an ignored folder, where a nested clone belongs
        const QList<QAction *> actions = plugin.versionControlActions({item(repo.filePath(QStringLiteral("vendor")))});
        QVERIFY(!actions.isEmpty());
        QCOMPARE(actions.constFirst()->text(), QStringLiteral("Git Clone..."));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{repo.filePath(QStringLiteral("vendor"))});
        for (const QString& path : {repo.filePath(QStringLiteral("src")), repo.filePath(QStringLiteral(".gitignore"))}) {
            QVERIFY(!menuTexts(plugin, {item(path)}).contains(QStringLiteral("Git Clone...")));
        }
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
