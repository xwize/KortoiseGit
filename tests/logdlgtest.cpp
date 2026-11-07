// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlg.hpp"

#include <QDir>
#include <QPixmap>
#include <QTest>

/**
 * The placeholder log dialog. Set KGT_SCREENSHOT_DIR to save a screenshot of it there.
 */
class LogDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsPaths()
    {
        const QStringList paths{QStringLiteral("/home/user/repo/a"), QStringLiteral("/home/user/repo/b")};
        kgt::LogDlg dlg(paths);
        QCOMPARE(dlg.paths(), paths);

        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());

        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("logdlg.png"))));
        }
    }
};

QTEST_MAIN(LogDlgTest)

#include "logdlgtest.moc"
