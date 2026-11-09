// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "aboutdlg.hpp"
#include "commands/command.hpp"
#include "utils/version.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>

using namespace kgt;

namespace
{

/**
 * @param dlg The dialog.
 * @returns The Version Information box's text.
 */
QString versionText(const AboutDlg& dlg)
{
    return dlg.findChild<QPlainTextEdit *>()->toPlainText();
}

/**
 * @param dlg The dialog.
 * @param part Text to look for.
 * @returns The first label whose text contains it, or nullptr.
 */
QLabel *labelWith(const AboutDlg& dlg, const QString& part)
{
    for (QLabel *label : dlg.findChildren<QLabel *>()) {
        if (label->text().contains(part)) {
            return label;
        }
    }
    return nullptr;
}

} // namespace

class AboutDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void commandNeedsNoPath()
    {
        const auto command = CommandServer::command(QStringLiteral("about"));
        QVERIFY(command.has_value());
        QCOMPARE((*command)->pathRequirement(), PathRequirement::NoPathRequired);
        QVERIFY(!(*command)->mayTakePath());
    }

    void showsLikeIddAbout()
    {
        const AboutDlg dlg;
        QCOMPARE(dlg.windowTitle(), QStringLiteral("About KortoiseGit"));

        const QLabel *basedOn = labelWith(dlg, QStringLiteral("Based on TortoiseGit"));
        QVERIFY(basedOn != nullptr);
        QVERIFY(basedOn->text().contains(QStringLiteral("https://tortoisegit.org/")));
        QVERIFY(basedOn->text().contains(QStringLiteral("not affiliated")));

        // Plain text: the address in angle brackets must not vanish as a tag
        const QLabel *contributors = labelWith(dlg, QStringLiteral("James Jones"));
        QVERIFY(contributors != nullptr);
        QCOMPARE(contributors->textFormat(), Qt::PlainText);

        const QLabel *webLink = labelWith(dlg, QStringLiteral("Visit our website"));
        QVERIFY(webLink != nullptr);
        QVERIFY(webLink->text().contains(QStringLiteral("href=\"https://github.com/xwize/KortoiseGit\"")));
        QVERIFY(webLink->openExternalLinks());

        const auto boxes = dlg.findChildren<QGroupBox *>();
        QCOMPARE(boxes.size(), 1);
        QCOMPARE(boxes.constFirst()->title(), QStringLiteral("Version Information"));

        // The logo is drawn
        bool hasLogo = false;
        for (const QLabel *label : dlg.findChildren<QLabel *>()) {
            hasLogo = hasLogo || !label->pixmap().isNull();
        }
        QVERIFY(hasLogo);
    }

    void versionFollowsIdsAboutVersion()
    {
        const AboutDlg dlg;
        const QStringList lines = versionText(dlg).split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0), QStringLiteral("KortoiseGit %1 (%2)").arg(versionString(), QCoreApplication::applicationDirPath()));
        QVERIFY2(lines.at(1).startsWith(QStringLiteral("git version ")), qPrintable(lines.at(1)));
        QVERIFY(lines.at(2).isEmpty());
    }

    void reportsGitThatCannotRun()
    {
        const QTemporaryDir noGit;
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(noGit.path()));
        const AboutDlg dlg;
        qputenv("PATH", path);
        const QStringList lines = versionText(dlg).split(QLatin1Char('\n'));
        const QString& gitLine = lines.at(1);
        QVERIFY2(gitLine.startsWith(QStringLiteral("Git not found (")), qPrintable(gitLine));
        QVERIFY2(gitLine.endsWith(QLatin1Char(')')), qPrintable(gitLine));
    }

    void versionTextCanBeSelectedButNotEdited()
    {
        const AboutDlg dlg;
        const auto *version = dlg.findChild<QPlainTextEdit *>();
        QVERIFY(version->isReadOnly());
        QVERIFY(version->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(version->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
    }

    void rendersForAScreenshot()
    {
        AboutDlg dlg;
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QImage shot = dlg.grab().toImage();
        QVERIFY(!shot.isNull());
        const QString target = qEnvironmentVariable("KGT_SCREENSHOT");
        if (!target.isEmpty()) {
            QVERIFY(shot.save(target));
        }
    }
};

QTEST_MAIN(AboutDlgTest)
#include "aboutdlgtest.moc"
