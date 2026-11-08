// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pushdlg.hpp"
#include "testrepo.hpp"

#include <QCheckBox>
#include <QDir>
#include <QPixmap>
#include <QTest>

namespace
{

using namespace kgt;

/**
 * @param repo A repository with commits on main.
 * @param name A remote's name; it points at the repository itself, which is never pushed to here.
 */
void addRemote(const TestRepo& repo, const QString& name)
{
    repo.run({QStringLiteral("remote"), QStringLiteral("add"), name, repo.path()});
}

} // namespace

/**
 * PushDlg's defaults: branch, remote, remote branch and options.
 */
class PushDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void preselectedRefIsShown()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        addRemote(repo, QStringLiteral("origin"));

        const PushOptions options = PushDlg(repo.path(), nullptr, QStringLiteral("refs/tags/v1")).options();
        QCOMPARE(options.localBranch, QStringLiteral("refs/tags/v1"));
        QCOMPARE(options.remote, QStringLiteral("origin"));
        QCOMPARE(options.remoteBranch, QStringLiteral("refs/tags/v1"));
    }

    void untrackedBranchOnlyRemote()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("branch"), QStringLiteral("other")});
        addRemote(repo, QStringLiteral("origin"));

        PushDlg dlg(repo.path());
        const PushOptions options = dlg.options();
        QCOMPARE(options.localBranch, QStringLiteral("main"));
        QCOMPARE(options.remote, QStringLiteral("origin"));
        QCOMPARE(options.remoteBranch, QStringLiteral("main"));
        // Not tracked yet: tracking is the default
        QVERIFY(options.setUpstream);
        QVERIFY(!options.forceWithLease);
        QVERIFY(!options.tags);

        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("pushdlg.png"))));
        }
    }

    void trackedBranchUsesItsUpstream()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        addRemote(repo, QStringLiteral("origin"));
        addRemote(repo, QStringLiteral("upstream"));
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.remote"), QStringLiteral("upstream")});
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.merge"), QStringLiteral("refs/heads/trunk")});

        const PushDlg dlg(repo.path());
        const PushOptions options = dlg.options();
        QCOMPARE(options.localBranch, QStringLiteral("main"));
        QCOMPARE(options.remote, QStringLiteral("upstream"));
        QCOMPARE(options.remoteBranch, QStringLiteral("trunk"));
        QVERIFY(!options.setUpstream);
    }

    void pushRemoteAndBranchWin()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        addRemote(repo, QStringLiteral("origin"));
        addRemote(repo, QStringLiteral("mirror"));
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.remote"), QStringLiteral("origin")});
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.merge"), QStringLiteral("refs/heads/main")});
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.pushremote"), QStringLiteral("mirror")});
        repo.run({QStringLiteral("config"), QStringLiteral("branch.main.pushbranch"), QStringLiteral("refs/for/main")});

        const PushOptions options = PushDlg(repo.path()).options();
        QCOMPARE(options.remote, QStringLiteral("mirror"));
        // Not stripped, for gerrit
        QCOMPARE(options.remoteBranch, QStringLiteral("refs/for/main"));
    }

    void severalRemotesNoneChosen()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        addRemote(repo, QStringLiteral("origin"));
        addRemote(repo, QStringLiteral("other"));

        // As TortoiseGit, no remote is guessed among several
        QVERIFY(PushDlg(repo.path()).options().remote.isEmpty());

        repo.run({QStringLiteral("config"), QStringLiteral("remote.pushdefault"), QStringLiteral("other")});
        QCOMPARE(PushDlg(repo.path()).options().remote, QStringLiteral("other"));
    }

    void detachedHeadPushesHead()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("--detach")});
        addRemote(repo, QStringLiteral("origin"));

        const PushOptions options = PushDlg(repo.path()).options();
        QCOMPARE(options.localBranch, QStringLiteral("HEAD"));
        QVERIFY(options.remoteBranch.isEmpty());
    }

    void forceWithLeaseExcludesTags()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        addRemote(repo, QStringLiteral("origin"));

        const PushDlg dlg(repo.path());
        QCheckBox *forceWithLease = nullptr;
        QCheckBox *tags = nullptr;
        for (QCheckBox *box : dlg.findChildren<QCheckBox *>()) {
            if (box->text() == QStringLiteral("Force &with lease")) {
                forceWithLease = box;
            } else if (box->text() == QStringLiteral("Include &Tags")) {
                tags = box;
            }
        }
        QVERIFY(forceWithLease && tags);
        forceWithLease->setChecked(true);
        QVERIFY(!tags->isEnabled());
        QVERIFY(dlg.options().forceWithLease);
        forceWithLease->setChecked(false);
        tags->setChecked(true);
        QVERIFY(!forceWithLease->isEnabled());
        QVERIFY(dlg.options().tags);
    }

    void f5Refreshes()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});

        PushDlg dlg(repo.path(), nullptr, QStringLiteral("refs/tags/v1"));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));
        QVERIFY(dlg.options().remote.isEmpty());

        addRemote(repo, QStringLiteral("origin"));
        QTest::keyClick(&dlg, Qt::Key_F5);
        QCOMPARE(dlg.options().remote, QStringLiteral("origin"));
        QCOMPARE(dlg.options().localBranch, QStringLiteral("refs/tags/v1"));
    }
};

QTEST_MAIN(PushDlgTest)

#include "pushdlgtest.moc"
