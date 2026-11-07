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
    }
};

QTEST_GUILESS_MAIN(GitAdminDirTest)

#include "gitadmindirtest.moc"
