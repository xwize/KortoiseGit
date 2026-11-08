// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/fetchcommand.hpp"
#include "commands/pullcommand.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"
#include "testrepo.hpp"

#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <tuple>

namespace
{

using namespace kgt;

/**
 * A bare remote with one commit on main, "mine" (a repository tracking it as origin/main, the one pulled into)
 * and "theirs" (a clone of it, which pushes the remote's new commits).
 */
struct PullSetup {
    QTemporaryDir remoteDir; // The bare remote
    Git remote{remoteDir.path()};
    TestRepo mine;
    QTemporaryDir theirsDir;
    Git theirs{theirsDir.path()};

    PullSetup()
    {
        std::ignore = remote.run({QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet"), QStringLiteral("--initial-branch=main")});
        mine.write(QStringLiteral("a.txt"), "a\n");
        mine.commitAll(QStringLiteral("initial"));
        mine.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remoteDir.path()});
        mine.run({QStringLiteral("push"), QStringLiteral("--quiet"), QStringLiteral("-u"), QStringLiteral("origin"), QStringLiteral("main")});
        std::ignore = theirs.run({QStringLiteral("clone"), QStringLiteral("--quiet"), remoteDir.path(), QStringLiteral(".")});
    }

    /**
     * Commits a file in "theirs" and pushes it to the remote.
     * @param file The file's name.
     * @param content What it holds.
     */
    void pushFromTheirs(const QString& file, const QByteArray& content) const
    {
        QFile out(theirsDir.filePath(file));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(content);
        out.close();
        std::ignore = theirs.run({QStringLiteral("add"), QStringLiteral("--all")});
        std::ignore = theirs.run({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), file});
        std::ignore = theirs.run({QStringLiteral("push"), QStringLiteral("--quiet"), QStringLiteral("origin"), QStringLiteral("HEAD")});
    }

    /**
     * @param git Where to look.
     * @param ref A ref.
     * @returns Its commit, or an empty string if it doesn't exist.
     */
    [[nodiscard]] static QString rev(const Git& git, const QString& ref)
    {
        try {
            return QString::fromLatin1(git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), ref})).trimmed();
        } catch (const RuntimeError&) {
            return {};
        }
    }
};

/**
 * Runs git in a ProgressDlg and waits for it to end.
 * @param dlg The dialog.
 * @param arguments The arguments after "git".
 * @returns Whether it ended in time.
 */
bool runProgress(ProgressDlg& dlg, const QStringList& arguments)
{
    dlg.addGitCmd(arguments);
    QSignalSpy finished(&dlg, &ProgressDlg::commandsFinished);
    dlg.start();
    return finished.wait(30000);
}

/**
 * @param dlg A dialog.
 * @param text A checkbox's label.
 * @returns The checkbox, or nullptr.
 */
QCheckBox *checkBox(const QWidget& dlg, const QString& text)
{
    for (QCheckBox *box : dlg.findChildren<QCheckBox *>()) {
        if (box->text() == text) {
            return box;
        }
    }
    return nullptr;
}

/**
 * Saves a screenshot of a dialog if KGT_SCREENSHOT_DIR is set.
 * @param dlg The dialog.
 * @param name The file name.
 */
void screenshot(QWidget& dlg, const QString& name)
{
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    const QPixmap pixmap = dlg.grab();
    QVERIFY(!pixmap.isNull());
    if (const QString dir = qEnvironmentVariable("KGT_SCREENSHOT_DIR"); !dir.isEmpty()) {
        QVERIFY(pixmap.save(QDir(dir).absoluteFilePath(name)));
    }
}

} // namespace

/**
 * PullFetchDlg in both modes, and the git pull and git fetch command lines it leads to, run against real remotes.
 */
class PullFetchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
        // The remembered choices go to a throwaway configuration
        QStandardPaths::setTestModeEnabled(true);
        const KSharedConfigPtr config = KSharedConfig::openConfig();
        for (const QString& group : config->groupList()) {
            config->deleteGroup(group);
        }
    }

    void pullFastForward()
    {
        const PullSetup setup;
        setup.pushFromTheirs(QStringLiteral("b.txt"), "b\n");

        const PullFetchOptions options = PullFetchDlg(setup.mine.path(), true).options();
        // Pulling from the tracked branch leaves the branch to git, so the remote-tracking branch is updated
        QCOMPARE(options.remoteUrl, QStringLiteral("origin"));
        QVERIFY(options.remoteBranchName.isEmpty());
        const QStringList arguments = PullCommand::pullArguments(options);
        QCOMPARE(arguments,
                 (QStringList{QStringLiteral("pull"),
                              QStringLiteral("--progress"),
                              QStringLiteral("-v"),
                              QStringLiteral("--no-rebase"),
                              QStringLiteral("--"),
                              QStringLiteral("origin")}));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, arguments));
        QCOMPARE(progress.gitStatus(), 0);
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD")), PullSetup::rev(setup.remote, QStringLiteral("main")));
        QVERIFY(QFile::exists(setup.mine.filePath(QStringLiteral("b.txt"))));
    }

    void pullMerge()
    {
        const PullSetup setup;
        setup.pushFromTheirs(QStringLiteral("b.txt"), "b\n");
        setup.mine.write(QStringLiteral("c.txt"), "c\n");
        setup.mine.commitAll(QStringLiteral("mine"));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, PullCommand::pullArguments(PullFetchDlg(setup.mine.path(), true).options())));
        QCOMPARE(progress.gitStatus(), 0);
        // A merge commit joining both
        QVERIFY(!PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD^2")).isEmpty());
        QVERIFY(QFile::exists(setup.mine.filePath(QStringLiteral("b.txt"))));
        QVERIFY(QFile::exists(setup.mine.filePath(QStringLiteral("c.txt"))));
    }

    void pullAlreadyUpToDate()
    {
        const PullSetup setup;
        const QString head = PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD"));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, PullCommand::pullArguments(PullFetchDlg(setup.mine.path(), true).options())));
        QCOMPARE(progress.gitStatus(), 0);
        QVERIFY2(progress.logText().contains(QStringLiteral("Already up to date")), qPrintable(progress.logText()));
        QCOMPARE(progress.currentWork(), QStringLiteral("Success"));
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD")), head);
    }

    void pullConflict()
    {
        const PullSetup setup;
        setup.pushFromTheirs(QStringLiteral("a.txt"), "theirs\n");
        setup.mine.write(QStringLiteral("a.txt"), "mine\n");
        setup.mine.commitAll(QStringLiteral("mine"));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, PullCommand::pullArguments(PullFetchDlg(setup.mine.path(), true).options())));
        // Reported as a failure, the repository left in git's conflict state
        QVERIFY(progress.gitStatus() != 0);
        QVERIFY(progress.currentWork().contains(QStringLiteral("did not exit cleanly")));
        QVERIFY(progress.logText().contains(QStringLiteral("CONFLICT")));
        QVERIFY(setup.mine.git().hasWorkingTreeConflicts());
        QVERIFY(QFile::exists(setup.mine.filePath(QStringLiteral(".git/MERGE_HEAD"))));
    }

    void pullDefaultsFromUpstream()
    {
        const PullSetup setup;
        setup.mine.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("upstream"), setup.remoteDir.path()});
        setup.mine.run({QStringLiteral("config"), QStringLiteral("branch.main.remote"), QStringLiteral("upstream")});
        setup.mine.run({QStringLiteral("config"), QStringLiteral("branch.main.merge"), QStringLiteral("refs/heads/trunk")});

        const PullFetchDlg dlg(setup.mine.path(), true);
        QComboBox *remoteBranch = nullptr;
        for (QComboBox *combo : dlg.findChildren<QComboBox *>()) {
            if (combo->currentText() == QStringLiteral("trunk")) {
                remoteBranch = combo;
            }
        }
        QVERIFY(remoteBranch);
        PullFetchOptions options = dlg.options();
        QCOMPARE(options.remoteUrl, QStringLiteral("upstream"));
        // The configured branch isn't named
        QVERIFY(options.remoteBranchName.isEmpty());
        // Another one is
        remoteBranch->setEditText(QStringLiteral("other"));
        options = dlg.options();
        QCOMPARE(options.remoteBranchName, QStringLiteral("other"));
        QCOMPARE(PullCommand::pullArguments(options).mid(4), (QStringList{QStringLiteral("--"), QStringLiteral("upstream"), QStringLiteral("other")}));

        // Without an upstream: the branch of the same name
        setup.mine.run({QStringLiteral("config"), QStringLiteral("--unset"), QStringLiteral("branch.main.merge")});
        setup.mine.run({QStringLiteral("config"), QStringLiteral("--unset"), QStringLiteral("branch.main.remote")});
        options = PullFetchDlg(setup.mine.path(), true).options();
        QCOMPARE(options.remoteBranchName, QStringLiteral("main"));
    }

    void pullOptions()
    {
        const PullSetup setup;
        PullFetchDlg dlg(setup.mine.path(), true);
        QCheckBox *noFF = checkBox(dlg, QStringLiteral("No &Fast Forward"));
        QCheckBox *ffOnly = checkBox(dlg, QStringLiteral("Fast Forward O&nly"));
        QCheckBox *tags = checkBox(dlg, QStringLiteral("Tags"));
        QCheckBox *prune = checkBox(dlg, QStringLiteral("Prune"));
        const QCheckBox *rebase = checkBox(dlg, QStringLiteral("&Launch Rebase After Fetch"));
        QVERIFY(noFF && ffOnly && tags && prune && rebase);
        // Disabled until RebaseDlg (KORTOISEGIT-0018)
        QVERIFY(!rebase->isEnabled());
        QCOMPARE(tags->checkState(), Qt::PartiallyChecked);
        QCOMPARE(prune->checkState(), Qt::PartiallyChecked);
        // remote.origin.tagopt isn't set
        bool reachable = false;
        for (const QLabel *label : dlg.findChildren<QLabel *>()) {
            reachable = reachable || label->text() == QStringLiteral("Default: Reachable");
        }
        QVERIFY(reachable);

        ffOnly->setChecked(true);
        QVERIFY(!noFF->isEnabled());
        ffOnly->setChecked(false);
        noFF->setChecked(true);
        QVERIFY(!ffOnly->isEnabled());
        tags->setCheckState(Qt::Unchecked);
        prune->setCheckState(Qt::Checked);
        checkBox(dlg, QStringLiteral("&Squash"))->setChecked(true);
        checkBox(dlg, QStringLiteral("No Co&mmit"))->setChecked(true);
        QCOMPARE(PullCommand::pullArguments(dlg.options()),
                 (QStringList{QStringLiteral("pull"),
                              QStringLiteral("--progress"),
                              QStringLiteral("-v"),
                              QStringLiteral("--no-rebase"),
                              QStringLiteral("--no-tags"),
                              QStringLiteral("--no-ff"),
                              QStringLiteral("--squash"),
                              QStringLiteral("--no-commit"),
                              QStringLiteral("--prune"),
                              QStringLiteral("--"),
                              QStringLiteral("origin")}));
        screenshot(dlg, QStringLiteral("pulldlg.png"));

        // Fast Forward Only is remembered per repository and mode
        noFF->setChecked(false);
        ffOnly->setChecked(true);
        dlg.accept();
        QVERIFY(PullFetchDlg(setup.mine.path(), true).options().ffOnly);
    }

    void fetchUpdatesTrackingBranchOnly()
    {
        const PullSetup setup;
        const QString head = PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD"));
        setup.pushFromTheirs(QStringLiteral("b.txt"), "b\n");

        PullFetchDlg dlg(setup.mine.path(), false);
        // The merge options are pull's
        for (const QString& text :
             {QStringLiteral("&Squash"), QStringLiteral("No Co&mmit"), QStringLiteral("No &Fast Forward"), QStringLiteral("Fast Forward O&nly")}) {
            QVERIFY(!checkBox(dlg, text)->isEnabled());
        }
        const PullFetchOptions options = dlg.options();
        QCOMPARE(options.remoteUrl, QStringLiteral("origin"));
        QVERIFY(!options.allRemotes);
        QVERIFY(options.remoteBranchName.isEmpty());
        const QStringList arguments = FetchCommand::fetchArguments(options);
        QCOMPARE(arguments,
                 (QStringList{QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("-v"), QStringLiteral("--"), QStringLiteral("origin")}));
        screenshot(dlg, QStringLiteral("fetchdlg.png"));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, arguments));
        QCOMPARE(progress.gitStatus(), 0);
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("refs/remotes/origin/main")), PullSetup::rev(setup.remote, QStringLiteral("main")));
        // The working tree is untouched
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("HEAD")), head);
        QVERIFY(!QFile::exists(setup.mine.filePath(QStringLiteral("b.txt"))));
    }

    void fetchPrunes()
    {
        const PullSetup setup;
        std::ignore =
            setup.theirs.run({QStringLiteral("push"), QStringLiteral("--quiet"), QStringLiteral("origin"), QStringLiteral("HEAD:refs/heads/feature")});
        setup.mine.run({QStringLiteral("fetch"), QStringLiteral("--quiet"), QStringLiteral("origin")});
        QVERIFY(!PullSetup::rev(setup.mine.git(), QStringLiteral("refs/remotes/origin/feature")).isEmpty());
        std::ignore = setup.theirs.run(
            {QStringLiteral("push"), QStringLiteral("--quiet"), QStringLiteral("origin"), QStringLiteral("--delete"), QStringLiteral("feature")});

        const PullFetchDlg dlg(setup.mine.path(), false);
        checkBox(dlg, QStringLiteral("Prune"))->setCheckState(Qt::Checked);
        const QStringList arguments = FetchCommand::fetchArguments(dlg.options());
        QVERIFY(arguments.contains(QStringLiteral("--prune")));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, arguments));
        QCOMPARE(progress.gitStatus(), 0);
        QVERIFY(PullSetup::rev(setup.mine.git(), QStringLiteral("refs/remotes/origin/feature")).isEmpty());
    }

    void fetchAllRemotes()
    {
        const PullSetup setup;
        setup.pushFromTheirs(QStringLiteral("b.txt"), "b\n");
        const QTemporaryDir secondDir;
        const Git second(secondDir.path());
        std::ignore = second.run({QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet"), QStringLiteral("--initial-branch=main")});
        std::ignore = setup.theirs.run({QStringLiteral("push"), QStringLiteral("--quiet"), secondDir.path(), QStringLiteral("HEAD:refs/heads/other")});
        setup.mine.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("second"), secondDir.path()});

        const PullFetchDlg dlg(setup.mine.path(), false);
        QComboBox *remote = nullptr;
        for (QComboBox *combo : dlg.findChildren<QComboBox *>()) {
            if (combo->count() > 0 && combo->itemText(0) == QStringLiteral("- all -")) {
                remote = combo;
            }
        }
        QVERIFY(remote);
        // The tracked remote is preselected; "- all -" heads the list
        QCOMPARE(remote->currentText(), QStringLiteral("origin"));
        remote->setCurrentIndex(0);
        const PullFetchOptions options = dlg.options();
        QVERIFY(options.allRemotes);
        const QStringList arguments = FetchCommand::fetchArguments(options);
        QCOMPARE(arguments, (QStringList{QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--progress"), QStringLiteral("-v")}));

        ProgressDlg progress(setup.mine.path());
        QVERIFY(runProgress(progress, arguments));
        QCOMPARE(progress.gitStatus(), 0);
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("refs/remotes/origin/main")), PullSetup::rev(setup.remote, QStringLiteral("main")));
        QCOMPARE(PullSetup::rev(setup.mine.git(), QStringLiteral("refs/remotes/second/other")), PullSetup::rev(second, QStringLiteral("other")));

        // Pull offers no "- all -"
        const PullFetchDlg pullDlg(setup.mine.path(), true);
        for (const QComboBox *combo : pullDlg.findChildren<QComboBox *>()) {
            QVERIFY(combo->findText(QStringLiteral("- all -")) < 0);
        }
    }
};

QTEST_MAIN(PullFetchTest)

#include "pullfetchtest.moc"
