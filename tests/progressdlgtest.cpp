// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "progressdlg.hpp"
#include "commands/pushcommand.hpp"
#include "pushdlg.hpp"
#include "testrepo.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <signal.h>

#include <memory>

namespace
{

using namespace kgt;
using namespace std::chrono_literals;

/**
 * A local repository with one commit on main, and an empty bare repository as its remote "origin".
 */
struct PushSetup {
    TestRepo repo; // The local repository
    QTemporaryDir remoteDir; // The bare remote
    Git remote{remoteDir.path()}; // Runs git in the remote

    PushSetup()
    {
        std::ignore = remote.run({QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet")});
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remoteDir.path()});
    }

    /**
     * @param ref A ref.
     * @returns Its commit in the remote, or an empty string if it doesn't exist there.
     */
    [[nodiscard]] QString remoteRef(const QString& ref) const
    {
        try {
            return QString::fromLatin1(remote.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), ref})).trimmed();
        } catch (const RuntimeError&) {
            return {};
        }
    }

    /**
     * @returns The local HEAD commit.
     */
    [[nodiscard]] QString localHead() const
    {
        return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})).trimmed();
    }
};

/**
 * Runs one push in a ProgressDlg and waits for it to end.
 * @param dlg The dialog.
 * @param options What to push.
 * @returns Whether it ended in time.
 */
bool runPush(ProgressDlg& dlg, const PushOptions& options)
{
    dlg.addGitCmd(PushCommand::pushArguments(options));
    QSignalSpy finished(&dlg, &ProgressDlg::commandsFinished);
    dlg.start();
    return finished.wait(30000);
}

/**
 * @param dlg A dialog to save, if KGT_SCREENSHOT_DIR is set.
 * @param name The file name.
 */
void saveScreenshot(QWidget& dlg, const QString& name)
{
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    const QPixmap screenshot = dlg.grab();
    QVERIFY(!screenshot.isNull());
    const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(name)));
    }
}

/**
 * @returns Options that push main to origin's main and track it.
 */
PushOptions pushMain()
{
    return {
        .localBranch = QStringLiteral("main"),
        .remote = QStringLiteral("origin"),
        .remoteBranch = QStringLiteral("main"),
        .forceWithLease = false,
        .tags = false,
        .setUpstream = true,
    };
}

/**
 * Installs a pre-push hook that hangs, writing its process ID to a file first.
 * @param repo The repository.
 * @returns The file the hook writes its process ID to.
 */
QString installHangingHook(const TestRepo& repo)
{
    const QString pidFile = repo.filePath(QStringLiteral("hook.pid"));
    repo.write(QStringLiteral(".git/hooks/pre-push"), QStringLiteral("#!/bin/sh\necho $$ > '%1'\nexec sleep 60\n").arg(pidFile).toUtf8());
    const bool executable = QFile::setPermissions(repo.filePath(QStringLiteral(".git/hooks/pre-push")), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    Q_ASSERT(executable);
    return pidFile;
}

/**
 * @param pidFile The file installHangingHook() named, once the hook has written it.
 * @returns The hook's process ID, or 0.
 */
pid_t readPid(const QString& pidFile)
{
    QFile file(pidFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    return static_cast<pid_t>(file.readAll().trimmed().toInt());
}

} // namespace

/**
 * ProgressDlg running git push against a bare repository in a temporary directory.
 */
class ProgressDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void pushArguments()
    {
        QCOMPARE(PushCommand::pushArguments(pushMain()),
                 (QStringList{QStringLiteral("push"),
                              QStringLiteral("--set-upstream"),
                              QStringLiteral("--progress"),
                              QStringLiteral("--"),
                              QStringLiteral("origin"),
                              QStringLiteral("main:main")}));
        QCOMPARE(PushCommand::pushArguments({.localBranch = QStringLiteral("main"),
                                             .remote = QStringLiteral("origin"),
                                             .remoteBranch = QString(),
                                             .forceWithLease = true,
                                             .tags = true,
                                             .setUpstream = false}),
                 (QStringList{QStringLiteral("push"),
                              QStringLiteral("--tags"),
                              QStringLiteral("--force-with-lease"),
                              QStringLiteral("--progress"),
                              QStringLiteral("--"),
                              QStringLiteral("origin"),
                              QStringLiteral("main")}));
        // An empty local branch deletes the remote one
        QCOMPARE(PushCommand::pushArguments({.localBranch = QString(),
                                             .remote = QStringLiteral("origin"),
                                             .remoteBranch = QStringLiteral("old"),
                                             .forceWithLease = false,
                                             .tags = false,
                                             .setUpstream = false}),
                 (QStringList{QStringLiteral("push"), QStringLiteral("--progress"), QStringLiteral("--"), QStringLiteral("origin"), QStringLiteral(":old")}));
    }

    void pushMovesRemoteRef()
    {
        const PushSetup setup;
        QVERIFY(setup.remoteRef(QStringLiteral("refs/heads/main")).isEmpty());

        ProgressDlg dlg(setup.repo.path());
        QVERIFY(runPush(dlg, pushMain()));
        QCOMPARE(dlg.gitStatus(), 0);
        QVERIFY(dlg.isDone());
        QCOMPARE(dlg.currentWork(), QStringLiteral("Success"));
        QCOMPARE(setup.remoteRef(QStringLiteral("refs/heads/main")), setup.localHead());

        const QString log = dlg.logText();
        QVERIFY2(log.startsWith(QStringLiteral("git push --set-upstream --progress -- origin main:main\n")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("[new branch]")), qPrintable(log));
        // Each progress counter is redrawn in place, leaving one line
        QCOMPARE(log.count(QStringLiteral("Writing objects:")), 1);
        QVERIFY(log.contains(QStringLiteral("\nSuccess (")));
        // --set-upstream took effect
        QCOMPARE(QString::fromUtf8(setup.repo.git().run({QStringLiteral("config"), QStringLiteral("branch.main.remote")})).trimmed(), QStringLiteral("origin"));

        saveScreenshot(dlg, QStringLiteral("progressdlg.png"));
    }

    void rejectedPushFails()
    {
        const PushSetup setup;
        {
            ProgressDlg first(setup.repo.path());
            QVERIFY(runPush(first, pushMain()));
            QCOMPARE(first.gitStatus(), 0);
        }
        const QString pushed = setup.remoteRef(QStringLiteral("refs/heads/main"));
        // Rewrite the pushed commit: the next push is not a fast-forward
        setup.repo.run({QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--quiet"), QStringLiteral("-m"), QStringLiteral("rewritten")});

        ProgressDlg dlg(setup.repo.path());
        QVERIFY(runPush(dlg, pushMain()));
        QVERIFY(dlg.gitStatus() != 0);
        QVERIFY2(dlg.currentWork().startsWith(QStringLiteral("git did not exit cleanly (exit code ")), qPrintable(dlg.currentWork()));
        QVERIFY2(dlg.logText().contains(QStringLiteral("[rejected]")), qPrintable(dlg.logText()));
        QCOMPARE(setup.remoteRef(QStringLiteral("refs/heads/main")), pushed);

        // Force with lease goes through: the remote is where origin/main says
        PushOptions force = pushMain();
        force.forceWithLease = true;
        ProgressDlg forced(setup.repo.path());
        QVERIFY(runPush(forced, force));
        QCOMPARE(forced.gitStatus(), 0);
        QCOMPARE(setup.remoteRef(QStringLiteral("refs/heads/main")), setup.localHead());
    }

    void abortKillsProcess()
    {
        const PushSetup setup;
        const QString pidFile = installHangingHook(setup.repo);

        ProgressDlg dlg(setup.repo.path());
        dlg.addGitCmd(PushCommand::pushArguments(pushMain()));
        dlg.start();
        QVERIFY(dlg.isRunning());
        QVERIFY(QTest::qWaitFor(
            [&pidFile] {
                return readPid(pidFile) > 0;
            },
            10000));
        const pid_t hookPid = readPid(pidFile);

        QElapsedTimer timer;
        timer.start();
        dlg.reject();
        QVERIFY(timer.elapsed() < 5000);
        QVERIFY(!dlg.isRunning());
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        QVERIFY(!dlg.isDone());
        // The hook git started went with it
        QVERIFY(QTest::qWaitFor(
            [hookPid] {
                return ::kill(hookPid, 0) != 0;
            },
            5000));
        QVERIFY(setup.remoteRef(QStringLiteral("refs/heads/main")).isEmpty());
    }
};

QTEST_MAIN(ProgressDlgTest)

#include "progressdlgtest.moc"
