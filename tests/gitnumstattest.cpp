// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitnumstat.hpp"

#include <QTest>

using namespace kgt;

/**
 * Parsing git diff --numstat -z.
 */
class GitNumStatTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesPlainBinaryAndRenamed()
    {
        // QByteArrayLiteral keeps the embedded NULs
        const QByteArray output = QByteArrayLiteral(
            "3\t1\tsrc/a file.txt\0-\t-\timage.png\0"
            "0\t0\t\0old name.txt\0new name.txt\0");
        const Result<QList<NumStatEntry>> entries = GitNumStat::parse(output);
        QVERIFY(entries);
        const QList<NumStatEntry> expected{
            {.path = QStringLiteral("src/a file.txt"), .origPath = {}, .added = QStringLiteral("3"), .removed = QStringLiteral("1")},
            {.path = QStringLiteral("image.png"), .origPath = {}, .added = QStringLiteral("-"), .removed = QStringLiteral("-")},
            {.path = QStringLiteral("new name.txt"), .origPath = QStringLiteral("old name.txt"), .added = QStringLiteral("0"), .removed = QStringLiteral("0")},
        };
        QCOMPARE(*entries, expected);
    }

    void parsesEmptyOutput()
    {
        const Result<QList<NumStatEntry>> entries = GitNumStat::parse({});
        QVERIFY(entries);
        QVERIFY(entries->isEmpty());
    }

    void rejectsTruncatedRecord()
    {
        const Result<QList<NumStatEntry>> entries = GitNumStat::parse(QByteArrayLiteral("3\t1\tno-terminator"));
        QVERIFY(!entries);
        QCOMPARE(entries.error().category, ErrorCategory::Parse);
    }

    void argumentsEndWithPathSpec()
    {
        QCOMPARE(GitNumStat::arguments(QStringLiteral("HEAD"), {QStringLiteral("dir")}),
                 (QStringList{QStringLiteral("diff"),
                              QStringLiteral("--numstat"),
                              QStringLiteral("-z"),
                              QStringLiteral("-M"),
                              QStringLiteral("HEAD"),
                              QStringLiteral("--"),
                              QStringLiteral("dir")}));
    }
};

QTEST_GUILESS_MAIN(GitNumStatTest)

#include "gitnumstattest.moc"
