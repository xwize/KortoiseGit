// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitloglist.hpp"
#include "logdlg.hpp"
#include "testrepo.hpp"

#include <QDir>
#include <QPixmap>
#include <QTest>

/**
 * The log dialog on a throwaway repository. Set KGT_SCREENSHOT_DIR to save a screenshot of it there.
 */
class LogDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        kgt::TestRepo::isolateGit();
    }

    void loadsHistory()
    {
        kgt::TestRepo repo;
        constexpr int commitCount = 150;
        for (int i = 0; i < commitCount; ++i) {
            repo.write(QStringLiteral("dir/f.txt"), QByteArray::number(i));
            repo.commitAll(QStringLiteral("Commit %1").arg(i));
        }
        repo.write(QStringLiteral("other.txt"), "x");
        repo.commitAll(QStringLiteral("Other"));

        kgt::LogDlg dlg({repo.path()});
        QVERIFY(dlg.pathFilter().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!dlg.logList()->isBusy(), 10000);
        QCOMPARE(dlg.logList()->logModel()->rowCount(), commitCount + 1);

        dlg.logList()->setCurrentIndex(dlg.logList()->logModel()->index(0, 0));
        QVERIFY(dlg.messageText().contains(QStringLiteral("Other")));

        kgt::LogDlg filtered({repo.filePath(QStringLiteral("dir"))});
        QCOMPARE(filtered.pathFilter(), QStringList{QStringLiteral("dir")});
        QTRY_VERIFY_WITH_TIMEOUT(!filtered.logList()->isBusy(), 10000);
        QCOMPARE(filtered.logList()->logModel()->rowCount(), commitCount);

        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QTest::qWait(500);
        const QPixmap screenshot = dlg.grab();
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("logdlg.png"))));
        }
    }
};

QTEST_MAIN(LogDlgTest)

#include "logdlgtest.moc"
