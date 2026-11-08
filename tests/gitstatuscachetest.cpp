// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitstatuscache.hpp"
#include "cacheinterface.hpp"
#include "foldercrawler.hpp"
#include "gitfolderstatus.hpp"
#include "testrepo.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <optional>

namespace
{

using namespace kgt;

constexpr int crawlTimeoutMs = 10000;
constexpr int settleMs = 700; // Longer than the crawler's and the shell updater's delays together

/**
 * @param status The cache's answer for a folder.
 * @param name One of its items.
 * @returns What the item is, or None if the answer doesn't list it.
 */
GitWcStatus stateOf(const std::optional<CacheFolderStatus>& status, const QString& name)
{
    if (!status) {
        return GitWcStatus::None;
    }
    const qsizetype index = status->names.indexOf(name);
    return index < 0 ? GitWcStatus::None : unpackItemState(status->states.at(index)).status;
}

/**
 * Asks about a folder until the cache has crawled its working tree.
 * @param cache The cache.
 * @param directory The folder.
 * @returns Whether the cache ended up answering.
 */
bool waitUntilKnown(GitStatusCache& cache, const QString& directory)
{
    return QTest::qWaitFor(
        [&] {
            return cache.folderStatus(directory).has_value();
        },
        crawlTimeoutMs);
}

/**
 * @param repo A repository.
 * @returns Its root as the cache spells it.
 */
QString canonicalRoot(const TestRepo& repo)
{
    return QFileInfo(repo.path()).canonicalFilePath();
}

} // namespace

/**
 * GitStatusCache with real git and real inotify: what TGitCache does for a working tree, without the bus.
 */
class GitStatusCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void statusPropagatesToParentFolders()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("src/lib/foo.cpp"), "one\n");
        repo.write(QStringLiteral("top.txt"), "top\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));
        QCOMPARE(stateOf(cache.folderStatus(repo.path()), QStringLiteral("src")), GitWcStatus::Normal);

        QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        repo.write(QStringLiteral("src/lib/foo.cpp"), "two\n");
        QVERIFY(changed.wait(crawlTimeoutMs));

        const QString root = canonicalRoot(repo);
        const QStringList paths = changed.constFirst().constFirst().toStringList();
        QVERIFY(paths.contains(root + QStringLiteral("/src/lib/foo.cpp")));
        QVERIFY(paths.contains(root + QStringLiteral("/src/lib")));
        QVERIFY(paths.contains(root + QStringLiteral("/src")));
        QCOMPARE(stateOf(cache.folderStatus(repo.path()), QStringLiteral("src")), GitWcStatus::ModifiedUnstaged);
        QCOMPARE(stateOf(cache.folderStatus(repo.filePath(QStringLiteral("src"))), QStringLiteral("lib")), GitWcStatus::ModifiedUnstaged);
        QCOMPARE(stateOf(cache.folderStatus(repo.filePath(QStringLiteral("src/lib"))), QStringLiteral("foo.cpp")), GitWcStatus::ModifiedUnstaged);

        // And back down when the change is undone
        changed.clear();
        repo.write(QStringLiteral("src/lib/foo.cpp"), "one\n");
        QVERIFY(changed.wait(crawlTimeoutMs));
        QCOMPARE(stateOf(cache.folderStatus(repo.path()), QStringLiteral("src")), GitWcStatus::Normal);
    }

    void aChangeGivesOneDebouncedNotification()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/one.txt"), "one\n");
        repo.write(QStringLiteral("a/two.txt"), "two\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        repo.write(QStringLiteral("a/one.txt"), "changed\n");
        repo.write(QStringLiteral("a/two.txt"), "changed\n");
        repo.write(QStringLiteral("a/one.txt"), "changed again\n");
        QVERIFY(changed.wait(crawlTimeoutMs));
        QTest::qWait(settleMs);
        QCOMPARE(changed.size(), 1);
    }

    void noNotificationWithoutAStatusChange()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/one.txt"), "one\n");
        repo.commitAll(QStringLiteral("first"));
        repo.write(QStringLiteral("a/one.txt"), "modified\n");

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        // The first crawl is news to nobody, and neither is a file that stays modified
        const QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        QTest::qWait(settleMs);
        repo.write(QStringLiteral("a/one.txt"), "modified further\n");
        QTest::qWait(settleMs);
        QCOMPARE(changed.size(), 0);
    }

    void noRefreshLoop()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/one.txt"), "one\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        repo.write(QStringLiteral("a/one.txt"), "changed\n");
        QVERIFY(changed.wait(crawlTimeoutMs));
        changed.clear();

        // What Dolphin does on being told: ask again, and git status runs for the folder
        for (int i = 0; i < 3; ++i) {
            QVERIFY(cache.folderStatus(repo.path()).has_value());
            std::ignore = GitFolderStatus::fetch(repo.path());
            QTest::qWait(settleMs);
        }
        QCOMPARE(changed.size(), 0);
    }

    void ignoredFoldersAreNotWatched()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "build/\n");
        repo.write(QStringLiteral("src/main.cpp"), "int main();\n");
        repo.write(QStringLiteral("build/sub/out.o"), "object\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        const QString root = canonicalRoot(repo);
        QVERIFY(cache.isPathWatched(root));
        QVERIFY(cache.isPathWatched(root + QStringLiteral("/src")));
        QVERIFY(!cache.isPathWatched(root + QStringLiteral("/build")));
        QVERIFY(!cache.isPathWatched(root + QStringLiteral("/build/sub")));
        QVERIFY(!cache.isPathWatched(root + QStringLiteral("/.git/objects")));
        QCOMPARE(stateOf(cache.folderStatus(repo.path()), QStringLiteral("build")), GitWcStatus::Ignored);

        // A folder inside it is answered from the folder git reported as a whole, and nothing there is rechecked
        const std::optional<CacheFolderStatus> inside = cache.folderStatus(repo.filePath(QStringLiteral("build/sub")));
        if (!inside) {
            QFAIL("The folder inside an ignored one has no answer");
        }
        QCOMPARE(inside->wholeFolder, std::optional(GitWcStatus::Ignored));
        const QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        repo.write(QStringLiteral("build/sub/out.o"), "rebuilt\n");
        QTest::qWait(settleMs);
        QCOMPARE(changed.size(), 0);
    }

    void aNewlyIgnoredFolderStopsBeingWatched()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("gen/out.txt"), "out\n");
        repo.write(QStringLiteral("keep.txt"), "keep\n");
        repo.run({QStringLiteral("add"), QStringLiteral("keep.txt")});
        repo.run({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), QStringLiteral("first")});

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));
        const QString gen = canonicalRoot(repo) + QStringLiteral("/gen");
        QVERIFY(cache.isPathWatched(gen));

        repo.write(QStringLiteral(".gitignore"), "gen/\n");
        QVERIFY(QTest::qWaitFor(
            [&] {
                return !cache.isPathWatched(gen);
            },
            crawlTimeoutMs));
        QCOMPARE(stateOf(cache.folderStatus(repo.path()), QStringLiteral("gen")), GitWcStatus::Ignored);
    }

    void aNewFolderIsWatched()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("keep.txt"), "keep\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        const QSignalSpy changed(&cache, &GitStatusCache::pathsChanged);
        repo.write(QStringLiteral("fresh/deep/new.txt"), "new\n");
        const QString deep = canonicalRoot(repo) + QStringLiteral("/fresh/deep");
        QVERIFY(QTest::qWaitFor(
            [&] {
                return cache.isPathWatched(deep);
            },
            crawlTimeoutMs));
        const std::optional<CacheFolderStatus> status = cache.folderStatus(repo.path());
        if (!status) {
            QFAIL("No answer for the top folder");
        }
        QVERIFY(status->hasUnversioned);
    }

    void aDeletedFolderLeavesTheCache()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("gone/file.txt"), "file\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));
        const QString gone = canonicalRoot(repo) + QStringLiteral("/gone");
        QVERIFY(cache.isPathWatched(gone));

        QVERIFY(QDir(gone).removeRecursively());
        QVERIFY(QTest::qWaitFor(
            [&] {
                return stateOf(cache.folderStatus(repo.path()), QStringLiteral("gone")) == GitWcStatus::ModifiedUnstaged;
            },
            crawlTimeoutMs));
        QVERIFY(!cache.isPathWatched(gone));
    }

    void stagingIsSeenAfterTheLockGoes()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/one.txt"), "one\n");
        repo.commitAll(QStringLiteral("first"));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        repo.write(QStringLiteral("a/one.txt"), "changed\n");
        repo.run({QStringLiteral("add"), QStringLiteral("a/one.txt")});
        QVERIFY(QTest::qWaitFor(
            [&] {
                return stateOf(cache.folderStatus(repo.path()), QStringLiteral("a")) == GitWcStatus::Modified;
            },
            crawlTimeoutMs));
    }

    void watchLimitLeavesTheTreeToThePlugin()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a/b/c/one.txt"), "one\n");
        repo.commitAll(QStringLiteral("first"));

        // Room for the admin directory and the top folder, but not the folders below
        GitStatusCache cache(3);
        QVERIFY(!cache.folderStatus(repo.path()).has_value());
        const QString root = canonicalRoot(repo);
        QVERIFY(QTest::qWaitFor(
            [&] {
                return !cache.isPathWatched(root);
            },
            crawlTimeoutMs));
        QVERIFY(!cache.isTreeReady(root));
        QVERIFY(!cache.folderStatus(repo.path()).has_value());
    }

    void answersMatchGitFolderStatus()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "build/\n*.o\n");
        repo.write(QStringLiteral("clean/file.txt"), "file\n");
        repo.write(QStringLiteral("edited/deep/file.txt"), "file\n");
        repo.write(QStringLiteral("staged/file.txt"), "file\n");
        repo.write(QStringLiteral("removed/file.txt"), "file\n");
        repo.write(QStringLiteral("gone/file.txt"), "file\n");
        repo.write(QStringLiteral("top.txt"), "top\n");
        repo.write(QStringLiteral("renamed.txt"), "a file long enough to be seen as a rename\n");
        repo.commitAll(QStringLiteral("first"));

        repo.write(QStringLiteral("edited/deep/file.txt"), "changed\n");
        repo.write(QStringLiteral("staged/file.txt"), "changed\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged/file.txt")});
        repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("removed/file.txt")});
        repo.remove(QStringLiteral("gone/file.txt"));
        repo.run({QStringLiteral("mv"), QStringLiteral("renamed.txt"), QStringLiteral("moved.txt")});
        repo.write(QStringLiteral("untracked/new.txt"), "new\n");
        repo.write(QStringLiteral("clean/untracked.txt"), "new\n");
        repo.write(QStringLiteral("objects/only.o"), "object\n");
        repo.write(QStringLiteral("build/out.bin"), "out\n");
        QVERIFY(QDir(repo.path()).mkdir(QStringLiteral("empty")));

        GitStatusCache cache;
        QVERIFY(waitUntilKnown(cache, repo.path()));

        compareFolder(cache,
                      repo,
                      Folder{.path = {},
                             .files = {QStringLiteral("top.txt"), QStringLiteral("moved.txt"), QStringLiteral("renamed.txt"), QStringLiteral(".gitignore")},
                             .dirs = {QStringLiteral("clean"),
                                      QStringLiteral("edited"),
                                      QStringLiteral("staged"),
                                      QStringLiteral("removed"),
                                      QStringLiteral("gone"),
                                      QStringLiteral("untracked"),
                                      QStringLiteral("objects"),
                                      QStringLiteral("build"),
                                      QStringLiteral("empty")}});
        compareFolder(cache, repo, Folder{.path = QStringLiteral("edited"), .files = {}, .dirs = {QStringLiteral("deep")}});
        compareFolder(cache, repo, Folder{.path = QStringLiteral("edited/deep"), .files = {QStringLiteral("file.txt")}, .dirs = {}});
        compareFolder(cache, repo, Folder{.path = QStringLiteral("clean"), .files = {QStringLiteral("file.txt"), QStringLiteral("untracked.txt")}, .dirs = {}});
        compareFolder(cache, repo, Folder{.path = QStringLiteral("objects"), .files = {QStringLiteral("only.o")}, .dirs = {}});
    }

    void parsesTrackedFolders()
    {
        using namespace Qt::StringLiterals;
        const QByteArray output =
            "100644 blob aaa\ta.txt\0"
            "040000 tree bbb\tsrc\0"
            "100755 blob ccc\tsrc/run.sh\0"
            "120000 blob ddd\tlink\0"
            "160000 commit eee\tsub module\0"_ba;
        QCOMPARE(FolderCrawler::parseTrackedDirs(output), QStringList({QStringLiteral("src"), QStringLiteral("sub module")}));
    }

private:
    /**
     * One folder to compare, and the items to compare in it.
     */
    struct Folder {
        QString path; // Relative to the root; empty for the root
        QStringList files; // Files directly inside it
        QStringList dirs; // Folders directly inside it
    };

    /**
     * @param fromCache The state built from the cache's answer.
     * @param fromGit The state read from git.
     * @param folder What to compare.
     * @returns The names of the items that differ.
     */
    static QStringList mismatches(const GitFolderStatus& fromCache, const GitFolderStatus& fromGit, const Folder& folder)
    {
        QStringList wrong;
        for (const QString& name : folder.files) {
            if (fromCache.itemVersion(name, false) != fromGit.itemVersion(name, false)) {
                wrong.append(name);
            }
        }
        for (const QString& name : folder.dirs) {
            if (fromCache.itemVersion(name, true) != fromGit.itemVersion(name, true) || fromCache.hasUnversioned(name) != fromGit.hasUnversioned(name)) {
                wrong.append(name);
            }
        }
        return wrong;
    }

    /**
     * Checks that the cache gives every item the state today's retrieval does.
     * @param cache The cache, which has crawled @\p repo.
     * @param repo The repository.
     * @param folder What to compare.
     */
    static void compareFolder(GitStatusCache& cache, const TestRepo& repo, const Folder& folder)
    {
        const QString directory = folder.path.isEmpty() ? repo.path() : repo.filePath(folder.path);
        const GitFolderStatus fromGit = GitFolderStatus::fetch(directory);
        const std::optional<CacheFolderStatus> cached = cache.folderStatus(directory);
        if (!cached) {
            QFAIL(qPrintable(directory));
        }
        const GitFolderStatus fromCache = GitFolderStatus::fromCache(*cached, {});
        const QStringList wrong = mismatches(fromCache, fromGit, folder);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1Char(' '))));
        QCOMPARE(fromCache.hasUnversioned(), fromGit.hasUnversioned());
    }
};

QTEST_GUILESS_MAIN(GitStatusCacheTest)

#include "gitstatuscachetest.moc"
