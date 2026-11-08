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
};

QTEST_GUILESS_MAIN(GitAdminDirTest)

#include "gitadmindirtest.moc"
