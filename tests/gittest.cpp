// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/git.hpp"
#include "apputils.hpp"
#include "git/gitcall.hpp"
#include "git/tgitpath.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>
#include <tuple>

Q_DECLARE_METATYPE(kgt::ConflictKind)

namespace
{

using namespace kgt;

/**
 * @param git Where to run.
 * @param arguments The arguments after "git".
 * @returns What git printed, or the RuntimeError it threw (whose category must be Git).
 */
QString runOrError(const Git& git, const QStringList& arguments)
{
    try {
        return QString::fromUtf8(git.run(arguments));
    } catch (const RuntimeError& e) {
        return e.error().category == ErrorCategory::Git ? e.error().message : QStringLiteral("wrong category");
    }
}

} // namespace

/**
 * Git and GitCall: running git blocking and asynchronously, its environment, and its failures.
 */
class GitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the user's own git configuration out of the tests
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        QVERIFY(m_dir.isValid());
    }

    void runReturnsOutput()
    {
        const Git git(m_dir.path());
        QVERIFY(git.run({QStringLiteral("--version")}).startsWith("git version "));
        QCOMPARE(git.workingDirectory(), m_dir.path());
    }

    void setsLocaleAndLocks()
    {
        const Git git(m_dir.path());
        // An alias starting with ! runs a command, here env, with git's own environment
        const QStringList arguments{QStringLiteral("-c"), QStringLiteral("alias.showenv=!env"), QStringLiteral("showenv")};
        const QByteArray readOnly = git.run(arguments, RunMode::ReadOnly);
        QVERIFY(readOnly.contains("\nLC_ALL=C\n"));
        QVERIFY(readOnly.contains("\nGIT_OPTIONAL_LOCKS=0\n"));
        const QByteArray readWrite = git.run(arguments, RunMode::ReadWrite);
        QVERIFY(readWrite.contains("\nLC_ALL=C\n"));
        QVERIFY(!readWrite.contains("GIT_OPTIONAL_LOCKS"));
    }

    void failureCarriesCommandAndStderr()
    {
        // m_dir is not a repository
        const QString message = runOrError(Git(m_dir.path()), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
        QVERIFY2(message.startsWith(QStringLiteral("git rev-parse --show-toplevel failed with exit code 128")), qPrintable(message));
        // LC_ALL=C keeps git's messages in English whatever the user's locale
        QVERIFY2(message.contains(QStringLiteral("not a git repository")), qPrintable(message));
    }

    void commandLineQuotesSpaces()
    {
        const QString message = runOrError(Git(m_dir.path()), {QStringLiteral("no such command"), QString()});
        QVERIFY2(message.startsWith(QStringLiteral("git \"no such command\" \"\" failed")), qPrintable(message));
    }

    void failureToStartThrows()
    {
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(m_dir.path()));
        const QString message = runOrError(Git(m_dir.path()), {QStringLiteral("--version")});
        qputenv("PATH", path);
        QVERIFY2(message.startsWith(QStringLiteral("Could not start git --version")), qPrintable(message));
    }

    void runsAsynchronously()
    {
        const std::unique_ptr<GitCall> call = Git(m_dir.path()).runAsync({QStringLiteral("--version")});
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait());
        QVERIFY(call->isFinished());
        QVERIFY(call->output().startsWith("git version "));
        QCOMPARE(finished.count(), 1);
    }

    void asyncFailureFinishesThenThrows()
    {
        const std::unique_ptr<GitCall> call = Git(m_dir.path()).runAsync({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait());
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = call->output());
    }

    void asyncFailureToStartStillFinishes()
    {
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(m_dir.path()));
        const std::unique_ptr<GitCall> call = Git(m_dir.path()).runAsync({QStringLiteral("--version")});
        qputenv("PATH", path);
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait());
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = call->output());
    }

    void destroyingARunningCallStopsIt()
    {
        // A call destroyed while git still runs must neither crash nor hang
        const auto call = Git(m_dir.path()).runAsync({QStringLiteral("--version")});
        QVERIFY(!call->isFinished());
    }

    void findsWorkingTreeRoot()
    {
        const QTemporaryDir repo;
        QVERIFY(repo.isValid());
        std::ignore = Git(repo.path()).run({QStringLiteral("init"), QStringLiteral("--quiet")});
        QVERIFY(QDir(repo.path()).mkpath(QStringLiteral("sub dir/deeper")));
        QFile file(repo.filePath(QStringLiteral("sub dir/file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const QString root = QFileInfo(repo.path()).canonicalFilePath();
        QCOMPARE(Git::workingTreeRoot(repo.path()), root);
        QCOMPARE(Git::workingTreeRoot(repo.filePath(QStringLiteral("sub dir/deeper"))), root);
        QCOMPARE(Git::workingTreeRoot(repo.filePath(QStringLiteral("sub dir/file.txt"))), root);
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = Git::workingTreeRoot(m_dir.path()));
    }

    void parseConflictHashesReadsEachStage()
    {
        const QByteArray base(40, 'a');
        const QByteArray local(40, 'b');
        const QByteArray remote(64, 'c');
        QByteArray output = "M 100644 " + base + " 1\tf.txt" + '\0';
        output += "M 100755 " + local + " 2\tf.txt" + '\0';
        output += "M 160000 " + remote + " 3\tf.txt" + '\0';
        const Result<ConflictHashes> hashes = Git::parseConflictHashesFromLsFile(output);
        QVERIFY(hashes.has_value());
        QCOMPARE(hashes->baseHash, QString::fromLatin1(base));
        QCOMPARE(hashes->localHash, QString::fromLatin1(local));
        QCOMPARE(hashes->remoteHash, QString::fromLatin1(remote));
        QVERIFY(hashes->baseIsFile);
        QVERIFY(hashes->localIsFile);
        QVERIFY(!hashes->remoteIsFile);
    }

    void parseConflictHashesLeavesMissingStagesEmpty()
    {
        const QByteArray output = "M 100644 " + QByteArray(40, 'a') + " 1\tname with spaces.txt" + '\0';
        const Result<ConflictHashes> hashes = Git::parseConflictHashesFromLsFile(output);
        QVERIFY(hashes.has_value());
        QVERIFY(!hashes->baseHash.isEmpty());
        QVERIFY(hashes->localHash.isEmpty());
        QVERIFY(hashes->remoteHash.isEmpty());
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void parseConflictHashesRejectsBadOutput_data()
    {
        QTest::addColumn<QByteArray>("output");
        const QByteArray hash(40, 'a');
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("no terminator") << "M 100644 " + hash + " 1\tf";
        QTest::newRow("cached entry") << "H 100644 " + hash + " 1\tf" + '\0';
        QTest::newRow("stage 0") << "M 100644 " + hash + " 0\tf" + '\0';
        QTest::newRow("short hash") << QByteArray("M 100644 abc 1\tf") + '\0';
        QTest::newRow("no name") << "M 100644 " + hash + " 1\t" + '\0';
    }

    void parseConflictHashesRejectsBadOutput()
    {
        QFETCH(const QByteArray, output);
        QVERIFY(!Git::parseConflictHashesFromLsFile(output).has_value());
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void conflictKindFollowsConflictEdit_data()
    {
        QTest::addColumn<bool>("hasLocal");
        QTest::addColumn<bool>("hasRemote");
        QTest::addColumn<bool>("isSubmodule");
        QTest::addColumn<ConflictKind>("expected");
        QTest::newRow("both sides") << true << true << false << ConflictKind::Content;
        QTest::newRow("deleted by us") << false << true << false << ConflictKind::DeleteModify;
        QTest::newRow("deleted by them") << true << false << false << ConflictKind::DeleteModify;
        QTest::newRow("submodule with both sides") << true << true << true << ConflictKind::Submodule;
        QTest::newRow("submodule deleted by them") << true << false << true << ConflictKind::Submodule;
    }

    void conflictKindFollowsConflictEdit()
    {
        QFETCH(const bool, hasLocal);
        QFETCH(const bool, hasRemote);
        QFETCH(const bool, isSubmodule);
        QFETCH(const ConflictKind, expected);
        const QString hash(40, QLatin1Char('a'));
        ConflictHashes hashes;
        hashes.baseHash = hash;
        hashes.localHash = hasLocal ? hash : QString();
        hashes.remoteHash = hasRemote ? hash : QString();
        hashes.localIsFile = !isSubmodule;
        QCOMPARE(AppUtils::conflictKind(hashes), expected);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void mergeToolArgumentsChoosesTheTool_data()
    {
        QTest::addColumn<QString>("configured");
        QTest::addColumn<QStringList>("installed");
        QTest::addColumn<QString>("toolOption");
        const QStringList both{QStringLiteral("meld"), QStringLiteral("kdiff3")};
        QTest::newRow("configured wins") << QStringLiteral("vimdiff") << both << QString();
        QTest::newRow("kdiff3 before meld") << QString() << both << QStringLiteral("--tool=kdiff3");
        const QStringList meld{QStringLiteral("meld")};
        QTest::newRow("meld alone") << QString() << meld << QStringLiteral("--tool=meld");
    }

    void mergeToolArgumentsChoosesTheTool()
    {
        QFETCH(const QString, configured);
        QFETCH(const QStringList, installed);
        QFETCH(const QString, toolOption);
        const auto isInstalled = [&installed](const QString& tool) {
            return installed.contains(tool);
        };
        const QStringList arguments = AppUtils::mergeToolArguments(configured, isInstalled);
        QStringList expected{
            QStringLiteral("--literal-pathspecs"),
            QStringLiteral("mergetool"),
            QStringLiteral("--no-prompt"),
        };
        if (!toolOption.isEmpty()) {
            expected.append(toolOption);
        }
        QCOMPARE(arguments, expected);
    }

    void mergeToolArgumentsWithNoToolInstalledGivesNothing()
    {
        const auto isInstalled = [](const QString&) {
            return false;
        };
        QVERIFY(AppUtils::mergeToolArguments(QString(), isInstalled).isEmpty());
    }

private:
    QTemporaryDir m_dir;

    void aStuckGitIsKilledAtTheTimeout()
    {
        // A git that never answers: the plugin must not wait for it on Dolphin's UI thread
        const QTemporaryDir bin;
        QFile script(QDir(bin.path()).filePath(QStringLiteral("git")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nexec sleep 30\n");
        script.close();
        QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QByteArray(bin.path().toLocal8Bit() + ':' + path));

        Git git(QDir::tempPath());
        git.setRunTimeout(200);
        QElapsedTimer elapsed;
        elapsed.start();
        bool threw = false;
        try {
            std::ignore = git.run({QStringLiteral("status")});
        } catch (const RuntimeError& e) {
            threw = e.error().category == ErrorCategory::Git && e.error().message.contains(QLatin1String("timed out"));
        }
        qputenv("PATH", path);
        QVERIFY(threw);
        QVERIFY(elapsed.elapsed() < 10000);
    }

    void repositoryRootFallsBackForABareRepository()
    {
        const QTemporaryDir bare;
        QVERIFY(bare.isValid());
        std::ignore = Git(bare.path()).run({QStringLiteral("init"), QStringLiteral("--bare")});
        QCOMPARE(Git::repositoryRoot(bare.path()), bare.path());
    }

    void takesDelimitedFields()
    {
        const QByteArray output("one\ttwo\0\0", 9);
        qsizetype pos = 0;
        QCOMPARE(Git::takeField(output, pos, '\t'), std::optional(QStringLiteral("one")));
        QCOMPARE(Git::takeField(output, pos, '\0'), std::optional(QStringLiteral("two")));
        QCOMPARE(Git::takeField(output, pos, '\0'), std::nullopt); // An empty field
        QCOMPARE(pos, 8);
    }

    void relativePathsResolveLinksAndTheRoot()
    {
        const QTemporaryDir dir;
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        const QString tree = root + QStringLiteral("/tree");
        QVERIFY(QDir(root).mkpath(QStringLiteral("tree/sub")));
        QVERIFY(QFile::link(tree, root + QStringLiteral("/link")));
        QCOMPARE(TGitPath::relativePaths(tree, {root + QStringLiteral("/link/a.txt"), tree + QStringLiteral("/sub")}),
                 (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub")}));
        QVERIFY(TGitPath::relativePaths(tree, {tree + QStringLiteral("/sub"), tree}).isEmpty());
    }

    void outputOrEmptyIgnoresGitFailing()
    {
        const auto outputOf = [this](const QString& argument) {
            const std::unique_ptr<GitCall> call = Git(m_dir.path()).runAsync({argument}, RunMode::ReadOnly);
            QSignalSpy finished(call.get(), &GitCall::finished);
            finished.wait();
            return AppUtils::outputOrEmpty(*call);
        };
        QVERIFY(outputOf(QStringLiteral("--version")).startsWith("git version"));
        QVERIFY(outputOf(QStringLiteral("no-such-command")).isEmpty());
    }

    void knowsAnUnbornBranch()
    {
        const TestRepo repo;
        QVERIFY(repo.git().isUnbornBranch());
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("One"));
        QVERIFY(!repo.git().isUnbornBranch());
    }
};

QTEST_GUILESS_MAIN(GitTest)

#include "gittest.moc"
