// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "stashsave.hpp"
#include "apputils.hpp"

#include <QTest>

using namespace kgt;

/**
 * The command line CAppUtils::StashSave builds from the Stash dialog's options.
 */
class StashSaveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
};

QTEST_GUILESS_MAIN(StashSaveTest)
#include "stashsavetest.moc"
