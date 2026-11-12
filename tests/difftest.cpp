// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/diffcommand.hpp"
#include "commands/prevdiffcommand.hpp"
#include "gitdiff.hpp"
#include "testrepo.hpp"
#include "toolprocess.hpp"
#include "utils/runtimeerror.hpp"

#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
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

/**
 * Leaves the repository in a merge with file in conflict.
 * @param repo An empty repository.
 */
void startConflictedMerge(const TestRepo& repo)
{
    repo.write(file(), "base\n");
    repo.commitAll(QStringLiteral("base"));
    repo.run({QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("other")});
    repo.write(file(), "other\n");
    repo.commitAll(QStringLiteral("other"));
    repo.run({QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-")});
    repo.write(file(), "ours\n");
    repo.commitAll(QStringLiteral("ours"));
    try {
        repo.run({QStringLiteral("merge"), QStringLiteral("other")});
    } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch): The conflict is the point
    }
}

/**
 * Runs git difftool or git mergetool to its end.
 * @param mode Which one.
 * @param repo Where.
 * @param arguments The arguments after "git".
 * @returns The messages ToolProcess::failed gave.
 */
QStringList runTool(ToolMode mode, const TestRepo& repo, const QStringList& arguments)
{
    ToolProcess tool(mode, repo.path(), arguments);
    QSignalSpy failed(&tool, &ToolProcess::failed);
    QSignalSpy finished(&tool, &ToolProcess::finished);
    if (!tool.start()) {
        return {QStringLiteral("not started")};
    }
    if (!finished.wait(10000)) {
        return {QStringLiteral("still running")};
    }
    QStringList messages;
    for (const QList<QVariant>& signal : std::as_const(failed)) {
        messages.append(signal.constFirst().toString());
    }
    return messages;
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

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void withDefaultToolChoosesTheTool_data()
    {
        QTest::addColumn<QString>("diffTool");
        QTest::addColumn<QString>("mergeTool");
        QTest::addColumn<QStringList>("installed");
        QTest::addColumn<QString>("toolOption");
        const QString vimdiff = QStringLiteral("vimdiff");
        const QStringList both{QStringLiteral("kdiff3"), QStringLiteral("meld")};
        const QStringList kdiff3{QStringLiteral("kdiff3")};
        const QStringList meld{QStringLiteral("meld")};
        QTest::newRow("meld before kdiff3") << QString() << QString() << both << QStringLiteral("--tool=meld");
        QTest::newRow("meld alone") << QString() << QString() << meld << QStringLiteral("--tool=meld");
        QTest::newRow("kdiff3 alone") << QString() << QString() << kdiff3 << QStringLiteral("--tool=kdiff3");
        QTest::newRow("neither installed") << QString() << QString() << QStringList() << QString();
        QTest::newRow("diff.tool wins") << vimdiff << QString() << both << QString();
        QTest::newRow("merge.tool is left to git") << QString() << vimdiff << both << QString();
    }

    void withDefaultToolChoosesTheTool()
    {
        QFETCH(const QString, diffTool);
        QFETCH(const QString, mergeTool);
        QFETCH(const QStringList, installed);
        QFETCH(const QString, toolOption);
        const auto isInstalled = [&installed](const QString& tool) {
            return installed.contains(tool);
        };
        const QString rev = QStringLiteral("abc");
        const QStringList arguments = GitDiff::diffArguments(file(), file(), {}, rev);
        const QStringList expected = toolOption.isEmpty() ? arguments : difftool({toolOption, rev, QStringLiteral("--"), file()});
        QCOMPARE(GitDiff::withDefaultTool(arguments, diffTool, mergeTool, isInstalled), expected);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void configErrorChecksTheTool_data()
    {
        QTest::addColumn<QString>("configuredTool");
        QTest::addColumn<QStringList>("installed");
        QTest::addColumn<bool>("usable");
        const QStringList meld{QStringLiteral("meld")};
        QTest::newRow("no tool at all") << QString() << QStringList() << false;
        QTest::newRow("meld by default") << QString() << meld << true;
        QTest::newRow("kdiff3 by default") << QString() << QStringList{QStringLiteral("kdiff3")} << true;
        QTest::newRow("vimdiff") << QStringLiteral("vimdiff") << meld << false;
        QTest::newRow("nvimdiff2") << QStringLiteral("nvimdiff2") << meld << false;
        QTest::newRow("gvimdiff has a window") << QStringLiteral("gvimdiff") << QStringList() << true;
        QTest::newRow("a missing tool is left to git") << QStringLiteral("kompare") << QStringList() << true;
    }

    void configErrorChecksTheTool()
    {
        QFETCH(const QString, configuredTool);
        QFETCH(const QStringList, installed);
        QFETCH(const bool, usable);
        const auto isInstalled = [&installed](const QString& tool) {
            return installed.contains(tool);
        };
        for (const ToolMode mode : {ToolMode::Diff, ToolMode::Merge}) {
            QCOMPARE(ToolProcess::configError(mode, configuredTool, isInstalled).isEmpty(), usable);
        }
    }

    void toolFailed()
    {
        const QString unresolved = QStringLiteral("Normal merge conflict for 'f':\nf seems unchanged.\nmerge of f failed");
        const QString unavailable = QStringLiteral("error: 'kompare' can not be used to resolve merges");
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Diff, {}, 0, {}));
        QVERIFY(ToolProcess::toolFailed(ToolMode::Diff, {}, 128, unavailable));
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Merge, {}, 0, {}));
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Merge, {}, 1, unresolved));
        QVERIFY(ToolProcess::toolFailed(ToolMode::Merge, {}, 1, unavailable));
        const QString guessed = QStringLiteral("Normal merge conflict for 'f':\nHit return to start merge resolution tool (kdiff3):");
        QVERIFY(ToolProcess::toolFailed(ToolMode::Merge, {}, 1, guessed));

        // git difftool --no-index exits 1 when the files differ and the tool ran, and 128 when the tool can't run
        const QStringList noIndex = GitDiff::diffNullArguments(QStringLiteral("f"));
        const QString died = QStringLiteral("fatal: external diff died, stopping at f");
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Diff, noIndex, 0, {}));
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Diff, noIndex, 1, {}));
        QVERIFY(ToolProcess::toolFailed(ToolMode::Diff, noIndex, 128, died));
        QVERIFY(!ToolProcess::toolFailed(ToolMode::Diff, GitDiff::extDiffArguments(QStringLiteral("a"), QStringLiteral("b")), 1, {}));
        // Exit 1 is still a failure without --no-index, or when "--no-index" is a path
        QVERIFY(ToolProcess::toolFailed(ToolMode::Diff,
                                        GitDiff::diffArguments(QStringLiteral("f"), QStringLiteral("f"), QStringLiteral("HEAD"), QStringLiteral("HEAD~1")),
                                        1,
                                        {}));
        QVERIFY(ToolProcess::toolFailed(ToolMode::Diff, {QStringLiteral("difftool"), QStringLiteral("--"), QStringLiteral("--no-index")}, 1, {}));
        QVERIFY(ToolProcess::toolFailed(ToolMode::Merge, noIndex, 1, {}));
    }

    void unavailableDiffToolIsReported()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), "changed\n");

        QStringList arguments = GitDiff::diffArguments(file(), file(), {}, QStringLiteral("HEAD"));
        arguments.insert(arguments.indexOf(QStringLiteral("--no-prompt")) + 1, QStringLiteral("--tool=nosuch"));
        const QStringList messages = runTool(ToolMode::Diff, repo, arguments);
        QCOMPARE(messages.size(), 1);
        QVERIFY2(messages.constFirst().contains(QStringLiteral("nosuch")), qPrintable(messages.constFirst()));
    }

    void diffToolThatRanIsNotReported()
    {
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), "changed\n");
        repo.run({QStringLiteral("config"), QStringLiteral("difftool.quiet.cmd"), QStringLiteral("true")});

        QStringList arguments = GitDiff::diffArguments(file(), file(), {}, QStringLiteral("HEAD"));
        arguments.insert(arguments.indexOf(QStringLiteral("--no-prompt")) + 1, QStringLiteral("--tool=quiet"));
        QCOMPARE(runTool(ToolMode::Diff, repo, arguments), QStringList());
    }

    void terminalToolEndsWithoutATerminal()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("vim")).isEmpty()) {
            QSKIP("Vim is not installed");
        }
        const TestRepo repo;
        repo.write(file(), fileContent());
        repo.commitAll(QStringLiteral("initial"));
        repo.write(file(), "changed\n");

        QStringList arguments = GitDiff::diffArguments(file(), file(), {}, QStringLiteral("HEAD"));
        arguments.insert(arguments.indexOf(QStringLiteral("--no-prompt")) + 1, QStringLiteral("--tool=vimdiff"));
        QVERIFY(!runTool(ToolMode::Diff, repo, arguments).contains(QStringLiteral("still running")));
    }

    void unavailableMergeToolIsReported()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        repo.run({QStringLiteral("config"), QStringLiteral("merge.tool"), QStringLiteral("nosuch")});

        const QStringList arguments = AppUtils::mergeToolArguments(QStringLiteral("nosuch"), {}) + QStringList{QStringLiteral("--"), file()};
        const QStringList messages = runTool(ToolMode::Merge, repo, arguments);
        QCOMPARE(messages.size(), 1);
        QVERIFY2(messages.constFirst().contains(QStringLiteral("nosuch")), qPrintable(messages.constFirst()));
    }

    void unresolvedMergeIsNotReported()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        repo.run({QStringLiteral("config"), QStringLiteral("merge.tool"), QStringLiteral("quiet")});
        repo.run({QStringLiteral("config"), QStringLiteral("mergetool.quiet.cmd"), QStringLiteral("true")});

        const QStringList arguments = AppUtils::mergeToolArguments(QStringLiteral("quiet"), {}) + QStringList{QStringLiteral("--"), file()};
        QCOMPARE(runTool(ToolMode::Merge, repo, arguments), QStringList());
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
