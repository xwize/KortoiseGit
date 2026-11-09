// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/diffcommand.hpp"
#include "commands/prevdiffcommand.hpp"
#include "gitdiff.hpp"
#include "testrepo.hpp"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{

using namespace kgt;

/**
 * @returns The file the tests compare, relative to the repository's root.
 */
QString file()
{
    return QStringLiteral("dir/a.txt");
}

/**
 * @returns Its committed content, long enough for git to see a rename.
 */
QByteArray fileContent()
{
    return "line one\nline two\nline three\nline four\n";
}

/**
 * @param arguments What comes after "git difftool --no-prompt".
 * @returns The whole command line after "git".
 */
QStringList difftool(const QStringList& arguments)
{
    return QStringList{QStringLiteral("--literal-pathspecs"), QStringLiteral("difftool"), QStringLiteral("--no-prompt")} + arguments;
}

/**
 * @param repo The repository.
 * @param rev A revision.
 * @returns Its full hash.
 */
QString hashOf(const TestRepo& repo, const QString& rev)
{
    return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), rev}).trimmed());
}

/**
 * @param repo A repository with file committed.
 * @param path Where to move it, staged.
 */
void stageRename(const TestRepo& repo, const QString& path)
{
    repo.run({QStringLiteral("mv"), file(), path});
}

} // namespace

/**
 * What --command diff and --command prevdiff compare, without starting a difftool.
 */
class DiffTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void diffArguments()
    {
        const QString other = QStringLiteral("old.txt");
        const QString rev = QStringLiteral("abc");
        const QString rev2 = QStringLiteral("def");
        QCOMPARE(GitDiff::diffArguments(file(), file(), {}, rev), difftool({rev, QStringLiteral("--"), file()}));
        QCOMPARE(GitDiff::diffArguments(file(), file(), rev2, rev), difftool({rev, rev2, QStringLiteral("--"), file()}));
        QCOMPARE(GitDiff::diffArguments(file(), other, {}, rev), difftool({QStringLiteral("abc:old.txt"), QStringLiteral("--"), file()}));
        QCOMPARE(GitDiff::diffArguments(file(), other, rev2, rev), difftool({QStringLiteral("abc:old.txt"), QStringLiteral("def:dir/a.txt")}));
        QCOMPARE(GitDiff::diffNullArguments(file()), difftool({QStringLiteral("--no-index"), QStringLiteral("--"), QStringLiteral("/dev/null"), file()}));
    }

    void modifiedFile()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), "changed\n");

        const DiffInvocation diff = DiffCommand::invocation(repo.filePath(file()), {}, {}, {});
        QCOMPARE(diff.arguments, difftool({QStringLiteral("HEAD"), QStringLiteral("--"), file()}));
        QCOMPARE(QFileInfo(diff.workingDirectory).canonicalFilePath(), QFileInfo(repo.path()).canonicalFilePath());
    }

    void addedFile()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("base.txt"), "something else entirely\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), fileContent());
        repo.run({QStringLiteral("add"), file()});

        QCOMPARE(DiffCommand::invocation(repo.filePath(file()), {}, {}, {}).arguments, GitDiff::diffNullArguments(file()));
    }

    void addedFileBeforeFirstCommit()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.run({QStringLiteral("add"), file()});

        QCOMPARE(DiffCommand::invocation(repo.filePath(file()), {}, {}, {}).arguments, GitDiff::diffNullArguments(file()));
    }

    void addedFileChangedSinceIsNotNew()
    {
        // TortoiseGit's check is for exactly GIT_STATUS_INDEX_NEW
        const TestRepo repo;
        repo.write(QStringLiteral("base.txt"), "something else entirely\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), fileContent());
        repo.run({QStringLiteral("add"), file()});
        repo.write(file(), "changed\n");

        QCOMPARE(DiffCommand::invocation(repo.filePath(file()), {}, {}, {}).arguments, difftool({QStringLiteral("HEAD"), QStringLiteral("--"), file()}));
    }

    void renamedFile()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        const QString renamed = QStringLiteral("renamed.txt");
        stageRename(repo, renamed);

        QCOMPARE(DiffCommand::invocation(repo.filePath(renamed), {}, {}, {}).arguments,
                 difftool({QStringLiteral("HEAD:dir/a.txt"), QStringLiteral("--"), renamed}));
    }

    void revisions()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        const QString start = QStringLiteral("v1");
        const QString end = QStringLiteral("v2");

        QCOMPARE(DiffCommand::invocation(repo.filePath(file()), {}, start, end).arguments, difftool({start, end, QStringLiteral("--"), file()}));
        // Only with both does TortoiseGit use them
        QCOMPARE(DiffCommand::invocation(repo.filePath(file()), {}, start, {}).arguments, difftool({QStringLiteral("HEAD"), QStringLiteral("--"), file()}));
    }

    void dirDiffOfCommits()
    {
        const QString commit = QStringLiteral("abc");
        const QString parent = QStringLiteral("def");
        const QString dirDiff = QStringLiteral("--dir-diff");
        const QString dashes = QStringLiteral("--");
        const QString dir = QStringLiteral("dir");

        // Whole tree against the working tree, then the parent as the base of a commit, limited to a folder
        QCOMPARE(GitDiff::dirArguments({}, {}, commit), difftool({dirDiff, commit}));
        QCOMPARE(GitDiff::dirArguments({dir}, commit, parent), difftool({dirDiff, parent, commit, dashes, dir}));
    }

    void twoFiles()
    {
        const QTemporaryDir notARepo;
        const QString first = notARepo.filePath(QStringLiteral("first"));
        const QString second = notARepo.filePath(QStringLiteral("second"));

        const DiffInvocation diff = DiffCommand::invocation(first, second, {}, {});
        QCOMPARE(diff.arguments, difftool({QStringLiteral("--no-index"), QStringLiteral("--"), second, first}));
        QCOMPARE(diff.workingDirectory, notARepo.path());
    }

    void twoFilesAtRevisions()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        const QString root = QFileInfo(repo.path()).canonicalFilePath();
        const QString first = root + QLatin1Char('/') + file();
        const QString second = root + QStringLiteral("/b.txt");

        QCOMPARE(DiffCommand::invocation(first, second, QStringLiteral("v1"), QStringLiteral("v2")).arguments,
                 difftool({QStringLiteral("v1:dir/a.txt"), QStringLiteral("v2:b.txt")}));
    }

    void prevDiffOneCommit()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("other.txt"), "other\n");
        repo.commitAll(QStringLiteral("does not touch the file"));

        QVERIFY(!PrevDiffCommand::invocation(repo.filePath(file())));
    }

    void prevDiffBeforeFirstCommit()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.run({QStringLiteral("add"), file()});

        QVERIFY(!PrevDiffCommand::invocation(repo.filePath(file())));
    }

    void prevDiffSeveralCommits()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("first"));
        repo.write(file(), "second\n");
        repo.commitAll(QStringLiteral("second"));
        const QString previous = hashOf(repo, QStringLiteral("HEAD"));
        repo.write(QStringLiteral("other.txt"), "other\n");
        repo.commitAll(QStringLiteral("does not touch the file"));
        repo.write(file(), "third\n");
        repo.commitAll(QStringLiteral("third"));
        repo.write(file(), "working tree\n");

        const std::optional<DiffInvocation> diff = PrevDiffCommand::invocation(repo.filePath(file()));
        QCOMPARE(diff.value_or(DiffInvocation{}).arguments, difftool({previous, QStringLiteral("--"), file()}));
    }
};

QTEST_GUILESS_MAIN(DiffTest)

#include "difftest.moc"
