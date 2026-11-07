// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <tuple>

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

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(GitTest)

#include "gittest.moc"
