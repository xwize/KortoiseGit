// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"
#include "pathutils.hpp"
#include "testrepo.hpp"

#include <KFileItem>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>
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
 * @param actions Actions the plugin returned.
 * @returns The "KortoiseGit" submenu's entries, in order, a separator as "-"; nothing if there is no submenu.
 */
QStringList submenuLayout(const QList<QAction *>& actions)
{
    QStringList layout;
    if (actions.isEmpty() || actions.constLast()->menu() == nullptr) {
        return layout;
    }
    for (const QAction *entry : actions.constLast()->menu()->actions()) {
        layout.append(entry->isSeparator() ? QStringLiteral("-") : entry->text());
    }
    return layout;
}

/**
 * @returns The Stash changes entry's text.
 */
QString stashChanges()
{
    return QStringLiteral("Stash changes");
}

/**
 * @param actions Actions the plugin returned.
 * @returns Their texts, with the "KortoiseGit" submenu replaced by its entries and separators left out: the top-level entries
 *          come first, then the submenu's. About, which is on every menu, is left out too, and so is Stash changes,
 *          which stashChangesEntry checks by itself.
 */
QStringList flatTexts(const QList<QAction *>& actions)
{
    QStringList texts;
    for (const QAction *action : actions) {
        if (action->menu() == nullptr) {
            texts.append(action->text());
        }
    }
    texts += submenuLayout(actions);
    texts.removeAll(QStringLiteral("-"));
    texts.removeAll(QStringLiteral("A&bout"));
    texts.removeAll(stashChanges());
    return texts;
}

/**
 * @param plugin The plugin, after a retrieval.
 * @param items The selected items.
 * @returns The texts of the context menu entries the plugin offers for them, as flatTexts() gives them.
 */
QStringList menuTexts(const ShellExt& plugin, const KFileItemList& items)
{
    return flatTexts(plugin.versionControlActions(items));
}

/**
 * @param actions Actions the plugin returned.
 * @returns The texts of the top-level entries, including the submenu's.
 */
QStringList topLevelTexts(const QList<QAction *>& actions)
{
    QStringList texts;
    for (const QAction *action : actions) {
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
    QString branch = QStringLiteral("Create &Branch...");
    QString tag = QStringLiteral("Create &Tag...");
};

/**
 * @returns The "KortoiseGit" submenu's text.
 */
QString gitMenu()
{
    return QStringLiteral("&KortoiseGit");
}

/**
 * @returns The About entry's text.
 */
QString about()
{
    return QStringLiteral("A&bout");
}

/**
 * @param plugin The plugin.
 * @param path A local file or folder.
 * @returns Whether the submenu ends with About, whichever of Dolphin's two calls asks.
 */
bool endsWithAbout(const ShellExt& plugin, const QString& path)
{
    const QStringList layout = submenuLayout(plugin.versionControlActions({item(path)}));
    return !layout.isEmpty() && layout.constLast() == about() && submenuLayout(plugin.outOfVersionControlActions({item(path)})) == layout;
}

/**
 * @returns How submenuLayout() shows a separator.
 */
QString separator()
{
    return QStringLiteral("-");
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

    void menuForItemKinds()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
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
        const QStringList versionedFolder{commit, pull, fetch, push, log, refs, rebase, revert, switchTo, merge, branch, tag, add};
        QCOMPARE(menu({QString()}), versionedFolder);
        QCOMPARE(menu({QStringLiteral("clean")}), versionedFolder);
        QCOMPARE(menu({QStringLiteral("untracked")}), versionedFolder);
        // An ignored folder is where a nested repository belongs
        QCOMPARE(menu({QStringLiteral("vendor")}),
                 (QStringList{clone, commit, createRepo, pull, fetch, push, log, refs, rebase, revert, switchTo, merge, branch, tag, add}));

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
        QCOMPARE(menu({QString(), QStringLiteral("clean")}), (QStringList{commit, pull, fetch, push, revert, add}));

        // The state is read afresh: once added, the file is no longer offered for Add
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});
        QCOMPARE(menu({QStringLiteral("new.txt")}), (QStringList{commit, diff, revert}));
    }

    void menuFollowsRepositoryMovedIntoNewFolder()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
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
        QCOMPARE(menuTexts(plugin, {item(repo.path())}),
                 (QStringList{commit, pull, fetch, push, log, refs, rebase, revert, switchTo, merge, branch, tag, add}));

        for (const QString& name : {QStringLiteral(".git"), QStringLiteral("a.txt"), QStringLiteral("dir")}) {
            QVERIFY(QDir(repo.path()).rename(name, QStringLiteral("moved/") + name));
        }

        // Shown, or seen from the old root, which Dolphin then asks through outOfVersionControlActions()
        const QStringList root{commit, pull, fetch, push, log, refs, rebase, revert, switchTo, merge, branch, tag, add};
        QCOMPARE(menuTexts(plugin, {item(moved)}), root);
        QCOMPARE(flatTexts(plugin.outOfVersionControlActions({item(moved)})), root);
        QCOMPARE(menuTexts(plugin, {item(QDir(moved).filePath(QStringLiteral("a.txt")))}), (QStringList{commit, diff, prevDiff, log}));
        QCOMPARE(menuTexts(plugin, {item(QDir(moved).filePath(QStringLiteral("dir")))}),
                 (QStringList{commit, pull, fetch, push, log, refs, rebase, revert, switchTo, merge, branch, tag, add}));
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
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
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
        QCOMPARE(topLevelTexts(actions), (QStringList{commit, gitMenu()}));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{repo.filePath(QStringLiteral("a.txt"))});

        // An unversioned file can't be committed from its menu, as in TortoiseGit
        actions = plugin.versionControlActions({item(repo.filePath(QStringLiteral("new.txt")))});
        QCOMPARE(flatTexts(actions), QStringList{add});

        // The folder shown, when nothing is selected
        actions = plugin.versionControlActions({item(repo.path())});
        QCOMPARE(topLevelTexts(actions), (QStringList{commit, gitMenu()}));
        QCOMPARE(actions.constFirst()->data().toStringList(), QStringList{repo.path()});
        const QAction *firstInSubmenu = actions.constLast()->menu()->actions().constFirst();
        QCOMPARE(firstInSubmenu->text(), pull);
        QCOMPARE(firstInSubmenu->data().toStringList(), QStringList{repo.path()});
    }

    void commitEntryLabels()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const ShellExt plugin(nullptr, {});
        const auto commitText = [&plugin, &repo] {
            return plugin.versionControlActions({item(repo.path())}).constFirst()->text();
        };
        const QString accelerated = QStringLiteral("Git &Commit -> \"%1\"...");

        // CStringUtils::EscapeAccellerators: '&' in a branch name is no shortcut
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("a&b")});
        QCOMPARE(commitText(), accelerated.arg(QStringLiteral("a&&b")));

        // Sanity check: at most 64 characters of a branch name
        const QString longName(70, QLatin1Char('x'));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), longName});
        QCOMPARE(commitText(), accelerated.arg(longName.left(64) + QStringLiteral("...")));

        // A detached HEAD: the first 8 characters of its hash
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("--detach")});
        QFile head(repo.filePath(QStringLiteral(".git/HEAD")));
        QVERIFY(head.open(QIODevice::ReadOnly));
        const QString hash = QString::fromLatin1(head.readAll().trimmed());
        QCOMPARE(hash.size(), 40);
        QCOMPARE(commitText(), accelerated.arg(hash.left(8) + QStringLiteral("...")));
    }

    void menuIsSplitLikeTortoiseGit()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "vendor/\n");
        repo.write(QStringLiteral("tracked.txt"), "a\n");
        repo.write(QStringLiteral("modified.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("modified.txt"), "changed\n");
        repo.write(QStringLiteral("new.txt"), "new\n");
        repo.write(QStringLiteral("vendor/lib.txt"), "lib\n");
        const ShellExt plugin(nullptr, {});
        const auto actionsFor = [&plugin, &repo](const QString& name) {
            return plugin.versionControlActions({item(name.isEmpty() ? repo.path() : repo.filePath(name))});
        };

        const QString stash = stashChanges();

        // Commit stays at the top level; the separators around it are one in the submenu
        QList<QAction *> actions = actionsFor({});
        QCOMPARE(topLevelTexts(actions), (QStringList{commit, gitMenu()}));
        QCOMPARE(submenuLayout(actions),
                 (QStringList{pull,
                              fetch,
                              push,
                              separator(),
                              log,
                              refs,
                              rebase,
                              stash,
                              separator(),
                              revert,
                              separator(),
                              switchTo,
                              merge,
                              branch,
                              tag,
                              separator(),
                              add,
                              separator(),
                              about()}));

        // The separator after Commit's group comes first among the submenu's, and is dropped; none trails either
        actions = actionsFor(QStringLiteral("tracked.txt"));
        QCOMPARE(topLevelTexts(actions), (QStringList{commit, gitMenu()}));
        QCOMPARE(submenuLayout(actions), (QStringList{diff, prevDiff, separator(), log, stash, separator(), about()}));
        QCOMPARE(submenuLayout(actionsFor(QStringLiteral("modified.txt"))),
                 (QStringList{diff, prevDiff, separator(), log, stash, separator(), revert, separator(), about()}));

        // Only submenu entries
        actions = actionsFor(QStringLiteral("new.txt"));
        QCOMPARE(topLevelTexts(actions), QStringList{gitMenu()});
        QCOMPARE(submenuLayout(actions), (QStringList{add, separator(), about()}));

        // All three top-level entries that exist, in MenuInfo's order, the submenu after them
        actions = actionsFor(QStringLiteral("vendor"));
        QCOMPARE(topLevelTexts(actions), (QStringList{clone, commit, createRepo, gitMenu()}));
        QCOMPARE(submenuLayout(actions),
                 (QStringList{pull,
                              fetch,
                              push,
                              separator(),
                              log,
                              refs,
                              rebase,
                              separator(),
                              revert,
                              separator(),
                              switchTo,
                              merge,
                              branch,
                              tag,
                              separator(),
                              add,
                              separator(),
                              about()}));

        // Only About for the submenu: no separator comes first
        const QTemporaryDir plain;
        actions = plugin.outOfVersionControlActions({item(plain.path())});
        QCOMPARE(topLevelTexts(actions), (QStringList{clone, createRepo, gitMenu()}));
        QCOMPARE(submenuLayout(actions), QStringList{about()});
    }

    void submenuCarriesTheAppIcon()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const ShellExt plugin(nullptr, {});

        const QIcon icon = plugin.versionControlActions({item(repo.path())}).constLast()->icon();

        // Each size is the PNG itself, not another one scaled, and needs no icon theme
        for (const int size : {16, 22, 32, 48, 64, 128, 256}) {
            const QImage expected(QStringLiteral(":/kortoisegitplugin/kortoisegit-%1.png").arg(size));
            QVERIFY(!expected.isNull());
            QCOMPARE(icon.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
                     expected.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        }
    }

    void submenuIsRebuiltAndFreed()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        auto plugin = std::make_unique<ShellExt>(nullptr, QVariantList{});

        const QList<QAction *> first = plugin->versionControlActions({item(repo.path())});
        const QPointer<QMenu> submenu = first.constLast()->menu();
        QVERIFY(submenu);
        const qsizetype entries = submenu->actions().size();
        const qsizetype children = submenu->findChildren<QAction *>(Qt::FindDirectChildrenOnly).size();

        // Asking again replaces the entries; the separators of the last menu don't pile up
        const QList<QAction *> second = plugin->versionControlActions({item(repo.path())});
        QCOMPARE(second.constLast()->menu(), submenu.data());
        QCOMPARE(submenu->actions().size(), entries);
        QCOMPARE(submenu->findChildren<QAction *>(Qt::FindDirectChildrenOnly).size(), children);

        plugin.reset();
        QVERIFY(submenu.isNull());
    }

    void createRepositoryEntryOutsideRepositories()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
        const ShellExt plugin(nullptr, {});
        const QTemporaryDir plain;
        const QList<QAction *> actions = plugin.outOfVersionControlActions({item(plain.path())});
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Git Clone..."));
        QCOMPARE(actions.at(0)->data().toStringList(), QStringList{plain.path()});
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Git Create repositor&y here..."));
        QCOMPARE(actions.at(1)->data().toStringList(), QStringList{plain.path()});

        // Not for a file or several items
        const QString file = plain.filePath(QStringLiteral("file.txt"));
        QFile created(file);
        QVERIFY(created.open(QIODevice::WriteOnly));
        QVERIFY(flatTexts(plugin.outOfVersionControlActions({item(file)})).isEmpty());
        QVERIFY(flatTexts(plugin.outOfVersionControlActions({item(plain.path()), item(file)})).isEmpty());

        // A working tree seen from outside gets its own menu
        const TestRepo repo;
        QCOMPARE(plugin.outOfVersionControlActions({item(repo.path())}).constFirst()->text(), commit);
        // A bare repository: Fetch, Show log and Browse References (TortoiseGit's Push too, which PushCommand
        // doesn't support yet)
        const QString bare = plain.filePath(QStringLiteral("bare.git"));
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), bare});
        QCOMPARE(menuTexts(plugin, {item(bare)}), (QStringList{fetch, log, refs}));
        // A folder whose .git isn't a repository: nothing but About
        const QString broken = plain.filePath(QStringLiteral("broken"));
        QVERIFY(QDir().mkpath(broken + QStringLiteral("/.git")));
        QVERIFY(flatTexts(plugin.outOfVersionControlActions({item(broken)})).isEmpty());
    }

    void noPullDuringMergeOrBisect()
    {
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
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
        const auto [commit, log, push, revert, add, clone, createRepo, refs, pull, fetch, merge, mergeAbort, switchTo, rebase, diff, prevDiff, branch, tag] =
            MenuTexts();
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
        // MenuInfo: Create Branch likewise
        QVERIFY(menu(QStringLiteral("dir")).contains(branch));
        QVERIFY(!menu(QStringLiteral("a.txt")).contains(branch));
        QVERIFY(!menuTexts(plugin, {item(repo.path()), item(repo.filePath(QStringLiteral("dir")))}).contains(branch));
        // MenuInfo: Create Tag likewise
        QVERIFY(menu(QStringLiteral("dir")).contains(tag));
        QVERIFY(!menu(QStringLiteral("a.txt")).contains(tag));
        QVERIFY(!menuTexts(plugin, {item(repo.path()), item(repo.filePath(QStringLiteral("dir")))}).contains(tag));
        QVERIFY(!menu({}).contains(mergeAbort));

        // While a merge awaits its commit: Abort Merge, before Revert, for versioned files and folders, and no Merge
        repo.write(QStringLiteral(".git/MERGE_HEAD"), "x\n");
        QCOMPARE(menu({}), (QStringList{commit, fetch, push, log, refs, mergeAbort, revert, switchTo, branch, tag, add}));
        QCOMPARE(menu(QStringLiteral("a.txt")), (QStringList{commit, diff, prevDiff, log, mergeAbort}));
        repo.remove(QStringLiteral(".git/MERGE_HEAD"));

        // And no Merge or Rebase during a bisect; Switch/Checkout stays
        repo.write(QStringLiteral(".git/BISECT_START"), "x\n");
        QVERIFY(!menu({}).contains(merge));
        QVERIFY(!menu({}).contains(rebase));
        QVERIFY(menu({}).contains(switchTo));
        repo.remove(QStringLiteral(".git/BISECT_START"));
    }

    void stashChangesEntry()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.write(QStringLiteral("dir/b.txt"), "b\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("new.txt"), "new\n");

        const ShellExt plugin(nullptr, {});
        const auto layout = [&plugin](const KFileItemList& items) {
            return submenuLayout(plugin.versionControlActions(items));
        };
        const KFileItem file = item(repo.filePath(QStringLiteral("a.txt")));
        const KFileItem root = item(repo.path());

        // MenuInfo's order: directly after Rebase
        const QStringList atRoot = layout({root});
        QVERIFY(atRoot.contains(stashChanges()));
        QCOMPARE(atRoot.indexOf(stashChanges()), atRoot.indexOf(QStringLiteral("Rebase...")) + 1);

        // ITEMIS_INGIT|ITEMIS_ONLYONE: one versioned file or folder
        QVERIFY(layout({file}).contains(stashChanges()));
        QVERIFY(layout({item(repo.filePath(QStringLiteral("dir")))}).contains(stashChanges()));
        QVERIFY(!layout({file, root}).contains(stashChanges()));
        QVERIFY(!layout({item(repo.filePath(QStringLiteral("new.txt")))}).contains(stashChanges()));
        const QTemporaryDir notARepo;
        QVERIFY(!layout({item(notARepo.path())}).contains(stashChanges()));

        // Not ITEMIS_MERGEACTIVE; a bisect doesn't matter
        repo.write(QStringLiteral(".git/MERGE_HEAD"), "x\n");
        QVERIFY(!layout({root}).contains(stashChanges()));
        repo.remove(QStringLiteral(".git/MERGE_HEAD"));
        repo.write(QStringLiteral(".git/BISECT_START"), "x\n");
        QVERIFY(layout({root}).contains(stashChanges()));
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

    void aboutEndsEveryMenu()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QTemporaryDir plain;
        QFile outside(plain.filePath(QStringLiteral("file.txt")));
        QVERIFY(outside.open(QIODevice::WriteOnly));

        const ShellExt plugin(nullptr, {});
        for (const QString& path : {repo.path(), repo.filePath(QStringLiteral("a.txt")), plain.path(), outside.fileName()}) {
            QVERIFY(endsWithAbout(plugin, path));
        }
        // CShellExt::Initialize still gives .git and what is in it no menu at all
        QVERIFY(plugin.versionControlActions({item(repo.filePath(QStringLiteral(".git")))}).isEmpty());

        const QList<QAction *> actions = plugin.versionControlActions({item(repo.path())});
        const QAction *entry = actions.constLast()->menu()->actions().constLast();
        QCOMPARE(entry->toolTip(), QStringLiteral("Shows information about KortoiseGit"));
    }

    void aboutStartsTheCommandWithoutPaths()
    {
        // A stand-in for kortoisegitproc that records its arguments: the real one would open a dialog nobody closes
        const QTemporaryDir bin;
        const QString record = bin.filePath(QStringLiteral("arguments"));
        const QString script = bin.filePath(QStringLiteral("kortoisegitproc"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > '%1.tmp'\nmv '%1.tmp' '%1'\n").arg(record).toUtf8());
        file.close();
        QVERIFY(file.setPermissions(file.permissions() | QFile::ExeOwner));

        const QByteArray path = qgetenv("PATH");
        const auto restorePath = qScopeGuard([&path] {
            qputenv("PATH", path);
        });
        qputenv("PATH", QByteArray(QFile::encodeName(bin.path()) + ':' + path));
        if (findSiblingExecutable(QStringLiteral("kortoisegitproc")) != QFileInfo(script).canonicalFilePath()) {
            QSKIP("An installed kortoisegitproc would be started instead of the stand-in");
        }

        const TestRepo repo;
        const ShellExt plugin(nullptr, {});
        const QList<QAction *> actions = plugin.versionControlActions({item(repo.path())});
        actions.constLast()->menu()->actions().constLast()->trigger();

        QVERIFY(QTest::qWaitFor([&record] {
            return QFile::exists(record);
        }));
        QFile arguments(record);
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        QCOMPARE(arguments.readAll(), QByteArray("--command\nabout\n"));
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

    void aShortSelectionGoesOnTheCommandLine()
    {
        QCOMPARE(ShellExt::pathArguments({QStringLiteral("/a"), QStringLiteral("/b c")}),
                 (QStringList{QStringLiteral("--path"), QStringLiteral("/a"), QStringLiteral("--path"), QStringLiteral("/b c")}));
    }

    void aLongSelectionGoesInAFile()
    {
        constexpr int pathCount = 20000;
        QStringList paths;
        QByteArray expected;
        for (int i = 0; i < pathCount; ++i) {
            paths.append(QStringLiteral("/some/rather/long/folder/name\nwith a newline/file%1.txt").arg(i));
            expected.append(paths.constLast().toUtf8());
            expected.append('\0');
        }
        const QStringList arguments = ShellExt::pathArguments(paths);
        QCOMPARE(arguments.size(), 3);
        QCOMPARE(arguments.at(0), QStringLiteral("--pathfile"));
        QCOMPARE(arguments.at(2), QStringLiteral("--deletepathfile"));
        QFile file(arguments.at(1));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), expected);
        QVERIFY(file.remove());
    }
};

QTEST_MAIN(ShellExtTest)

#include "shellexttest.moc"
