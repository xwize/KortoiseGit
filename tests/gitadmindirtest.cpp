// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitadmindir.hpp"
#include "testrepo.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace kgt;

/**
 * Finding .git and reading the current branch without running git.
 */
class GitAdminDirTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void findsAdminDirFromInside()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("dir/a.txt"), "a\n");
        const QString expected = repo.filePath(QStringLiteral(".git"));
        QCOMPARE(GitAdminDir::worktreeAdminDirPath(repo.path()), expected);
        QCOMPARE(GitAdminDir::worktreeAdminDirPath(repo.filePath(QStringLiteral("dir/a.txt"))), expected);
        QCOMPARE(GitAdminDir::worktreeAdminDirPath(repo.filePath(QStringLiteral("dir"))), expected);

        const QTemporaryDir notARepo;
        QVERIFY(GitAdminDir::worktreeAdminDirPath(notARepo.path()).isEmpty());
    }

    void findsStash()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QString adminDir = GitAdminDir::worktreeAdminDirPath(repo.path());
        // Names that only look like it, packed
        repo.run({QStringLiteral("update-ref"), QStringLiteral("refs/stashes"), QStringLiteral("HEAD")});
        repo.run({QStringLiteral("branch"), QStringLiteral("refs/stash")});
        repo.run({QStringLiteral("pack-refs"), QStringLiteral("--all")});
        QVERIFY(!GitAdminDir::hasStashDir(adminDir));
        // git itself can't create the stash beside that branch
        repo.run({QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("refs/stash")});

        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.run({QStringLiteral("stash"), QStringLiteral("push")});
        QVERIFY(QFileInfo::exists(repo.filePath(QStringLiteral(".git/refs/stash"))));
        QVERIFY(GitAdminDir::hasStashDir(adminDir));

        repo.run({QStringLiteral("pack-refs"), QStringLiteral("--all")});
        QVERIFY(!QFileInfo::exists(repo.filePath(QStringLiteral(".git/refs/stash"))));
        QVERIFY(GitAdminDir::hasStashDir(adminDir));

        // A linked worktree shares the main repository's refs/stash
        const QTemporaryDir parent;
        const QString worktree = QDir(parent.path()).filePath(QStringLiteral("wt"));
        repo.run({QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("--detach"), worktree});
        QVERIFY(GitAdminDir::hasStashDir(GitAdminDir::worktreeAdminDirPath(worktree)));

        repo.run({QStringLiteral("stash"), QStringLiteral("drop")});
        QVERIFY(!GitAdminDir::hasStashDir(adminDir));
        QVERIFY(!GitAdminDir::hasStashDir(GitAdminDir::worktreeAdminDirPath(worktree)));
    }

    void findsProjectTopDir()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("dir/a.txt"), "a\n");
        QCOMPARE(GitAdminDir::projectTopDir(repo.path()), repo.path());
        QCOMPARE(GitAdminDir::projectTopDir(repo.filePath(QStringLiteral("dir"))), repo.path());
        QCOMPARE(GitAdminDir::projectTopDir(repo.filePath(QStringLiteral("dir/a.txt"))), repo.path());

        // Nothing for .git and what is in it
        QVERIFY(GitAdminDir::projectTopDir(repo.filePath(QStringLiteral(".git"))).isEmpty());
        QVERIFY(GitAdminDir::projectTopDir(repo.filePath(QStringLiteral(".git/config"))).isEmpty());
        QVERIFY(GitAdminDir::isAdminDirPath(repo.filePath(QStringLiteral(".git"))));
        QVERIFY(GitAdminDir::isAdminDirPath(repo.filePath(QStringLiteral(".git/refs"))));
        QVERIFY(!GitAdminDir::isAdminDirPath(repo.filePath(QStringLiteral(".gitignore"))));

        // A bare repository inside the working tree isn't part of it
        const QString bare = repo.filePath(QStringLiteral("bare.git"));
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), bare});
        QVERIFY(GitAdminDir::projectTopDir(bare).isEmpty());
        QVERIFY(GitAdminDir::projectTopDir(bare + QStringLiteral("/refs")).isEmpty());

        // A .git that isn't a repository, or none at all
        const QTemporaryDir other;
        QVERIFY(GitAdminDir::projectTopDir(other.path()).isEmpty());
        QVERIFY(QDir().mkpath(other.filePath(QStringLiteral(".git"))));
        QVERIFY(GitAdminDir::projectTopDir(other.path()).isEmpty());
    }

    void readsBranchAndDetachedHead()
    {
        const TestRepo repo;
        const QString adminDir = GitAdminDir::worktreeAdminDirPath(repo.path());
        // A branch without commits yet
        QCOMPARE(GitAdminDir::currentBranchFromFile(adminDir), QStringLiteral("main"));

        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("feature/x&y")});
        QCOMPARE(GitAdminDir::currentBranchFromFile(adminDir), QStringLiteral("feature/x&y"));

        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("--detach")});
        QCOMPARE(GitAdminDir::currentBranchFromFile(adminDir), QStringLiteral("HEAD"));
        // CGit::GetCurrentBranchFromFile's fallback: the commit instead
        const QString hash = GitAdminDir::currentBranchFromFile(adminDir, true);
        QCOMPARE(hash.size(), 40);
        QVERIFY(hash != QStringLiteral("HEAD"));

        QVERIFY(GitAdminDir::currentBranchFromFile({}).isEmpty());
    }

    void followsLinkedWorktree()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QTemporaryDir parent;
        const QString worktree = parent.filePath(QStringLiteral("wt"));
        repo.run({QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("other"), worktree});

        const QString adminDir = GitAdminDir::worktreeAdminDirPath(QDir(worktree).filePath(QStringLiteral("a.txt")));
        QCOMPARE(QFileInfo(adminDir).canonicalFilePath(), QFileInfo(repo.filePath(QStringLiteral(".git/worktrees/wt"))).canonicalFilePath());
        QCOMPARE(GitAdminDir::currentBranchFromFile(adminDir), QStringLiteral("other"));
        // Its admin directory has no objects of its own: they are found through commondir
        QCOMPARE(GitAdminDir::projectTopDir(QDir(worktree).filePath(QStringLiteral("a.txt"))), worktree);
    }

    void aBareRepositoryIsItsOwnAdminDir()
    {
        // Inside a working tree, so a walk up the folders would find the wrong repository
        const TestRepo repo;
        const QString bare = repo.filePath(QStringLiteral("inner.git"));
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), bare});
        QCOMPARE(GitAdminDir::worktreeAdminDirPath(bare), bare);
    }
};

QTEST_GUILESS_MAIN(GitAdminDirTest)

#include "gitadmindirtest.moc"
