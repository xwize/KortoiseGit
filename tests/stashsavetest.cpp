// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "stashsave.hpp"
#include "apputils.hpp"
#include "modalwatcher.hpp"
#include "testrepo.hpp"

#include <QStandardPaths>
#include <QTest>

using namespace kgt;

namespace
{

/**
 * @param modal A modal window.
 * @returns Whether it was StashSaveDlg, now accepted with what it starts with.
 */
bool acceptStashSaveDlg(QWidget *modal)
{
    auto *dlg = qobject_cast<StashSaveDlg *>(modal);
    if (dlg) {
        dlg->accept();
    }
    return dlg;
}

} // namespace

/**
 * The command line CAppUtils::StashSave builds from the Stash dialog's options, and the follow-ups it offers.
 */
class StashSaveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        TestRepo::isolateGit();
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void arguments_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("includeUntracked");
        QTest::addColumn<bool>("all");
        QTest::addColumn<QStringList>("expected");

        const QStringList push{QStringLiteral("stash"), QStringLiteral("push")};
        const QString untracked = QStringLiteral("--include-untracked");
        const QString all = QStringLiteral("--all");
        const QString messageSwitch = QStringLiteral("-m");
        const QString dashed = QStringLiteral("--not-an-option");
        const QString wip = QStringLiteral("wip");

        QTest::newRow("plain") << QString() << false << false << push;
        QTest::newRow("untracked") << QString() << true << false << push + QStringList{untracked};
        QTest::newRow("all") << QString() << false << true << push + QStringList{all};
        // The dialog prevents both, but StashSave's "else if" is what decides
        QTest::newRow("both") << QString() << true << true << push + QStringList{untracked};
        QTest::newRow("message") << wip << false << false << push + QStringList{messageSwitch, wip};
        QTest::newRow("dashed message") << dashed << false << true << push + QStringList{all, messageSwitch, dashed};
    }

    void arguments()
    {
        QFETCH(const QString, message);
        QFETCH(const bool, includeUntracked);
        QFETCH(const bool, all);
        QFETCH(const QStringList, expected);
        const StashSaveOptions options{.message = message, .includeUntracked = includeUntracked, .all = all};
        QCOMPARE(AppUtils::stashSaveArguments(options), expected);
    }

    void offersToRestoreTheStash()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QString pop = QStringLiteral("Stash Pop");
        const QString apply = QStringLiteral("Stash Apply");

        // Nothing to stash: refs/stash stays, so nothing to restore
        {
            const CommandDriver driver({}, acceptStashSaveDlg);
            QVERIFY(AppUtils::stashSave(repo.git()));
            QCOMPARE(driver.offered(), QList<QStringList>{QStringList()});
        }

        // A stash: Stash Pop and Stash Apply, after Pull and Merge when it was for one; Stash Pop restores it
        repo.write(QStringLiteral("a.txt"), "changed\n");
        {
            const CommandDriver driver({pop}, acceptStashSaveDlg);
            const StashSaveFollowUps followUps{.showPull = true, .pullShowPush = false, .showMerge = true, .mergeRev = QStringLiteral("feature")};
            QVERIFY(AppUtils::stashSave(repo.git(), QString(), nullptr, followUps));
            const QStringList expected{QStringLiteral("&Pull..."), QStringLiteral("&Merge..."), pop, apply};
            QCOMPARE(driver.offered(), QList<QStringList>{expected});
        }
        QFile file(repo.filePath(QStringLiteral("a.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed\n"));
    }
};

QTEST_MAIN(StashSaveTest)
#include "stashsavetest.moc"
