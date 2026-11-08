// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "git/git.hpp"
#include "git/gitcall.hpp"

#include <KConfigGroup>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace kgt;

/**
 * The AppUtils helpers the dialogs share that don't open a window.
 */
class AppUtilsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void generalSettingsIsTheGeneralGroup()
    {
        QCOMPARE(AppUtils::generalSettings().name(), QStringLiteral("General"));
    }

    void outputOrEmptyReturnsTheOutput()
    {
        const QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::unique_ptr<GitCall> call = Git(dir.path()).runAsync({QStringLiteral("--version")}, RunMode::ReadOnly);
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait());
        QVERIFY(AppUtils::outputOrEmpty(*call).startsWith("git version"));
    }

    void outputOrEmptyIgnoresGitFailing()
    {
        const QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::unique_ptr<GitCall> call = Git(dir.path()).runAsync({QStringLiteral("no-such-command")}, RunMode::ReadOnly);
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait());
        QVERIFY(AppUtils::outputOrEmpty(*call).isEmpty());
    }
};

QTEST_GUILESS_MAIN(AppUtilsTest)

#include "apputilstest.moc"
