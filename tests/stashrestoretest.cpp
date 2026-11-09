// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "git/gitcall.hpp"
#include "testrepo.hpp"

#include <QTest>

#include <memory>

using namespace kgt;

Q_DECLARE_METATYPE(kgt::StashOutcome)

/**
 * How CAppUtils::StashApply and StashPop tell success, conflicts and failure apart.
 */
class StashRestoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void outcome_data()
    {
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<QString>("output");
        QTest::addColumn<StashOutcome>("expected");

        const QString conflict = QStringLiteral("CONFLICT (content): Merge conflict in a.txt");
        QTest::newRow("clean") << 0 << QStringLiteral("On branch main") << StashOutcome::Success;
        QTest::newRow("exit 0 with conflicts") << 0 << conflict << StashOutcome::Conflicts;
        QTest::newRow("exit 1 with conflicts") << 1 << conflict << StashOutcome::Conflicts;
        QTest::newRow("exit 1 without") << 1 << QStringLiteral("No stash entries found.") << StashOutcome::Failed;
        QTest::newRow("other exit with conflicts") << 2 << conflict << StashOutcome::Failed;
        QTest::newRow("could not run") << -1 << conflict << StashOutcome::Failed;
    }

    void outcome()
    {
        QFETCH(const int, exitCode);
        QFETCH(const QString, output);
        QFETCH(const StashOutcome, expected);
        QCOMPARE(AppUtils::stashOutcome(exitCode, output), expected);
    }

    void realConflictIsNotAFailure()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("a.txt"), "stashed\n");
        repo.run({QStringLiteral("stash"), QStringLiteral("push")});
        repo.write(QStringLiteral("a.txt"), "committed\n");
        repo.commitAll(QStringLiteral("clashes"));

        const std::unique_ptr<GitCall> call = repo.git().runAsync({QStringLiteral("stash"), QStringLiteral("pop")});
        call->waitForFinished();
        QCOMPARE(AppUtils::stashOutcome(call->exitCode(), call->combinedOutput()), StashOutcome::Conflicts);
    }

    void gitsErrorIsInTheOutput()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));

        const std::unique_ptr<GitCall> call = repo.git().runAsync({QStringLiteral("stash"), QStringLiteral("pop")});
        call->waitForFinished();
        const QString output = call->combinedOutput();
        QCOMPARE(AppUtils::stashOutcome(call->exitCode(), output), StashOutcome::Failed);
        QVERIFY2(output.contains(QStringLiteral("No stash entries")), qPrintable(output));
    }
};

QTEST_GUILESS_MAIN(StashRestoreTest)
#include "stashrestoretest.moc"
