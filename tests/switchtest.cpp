// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "chooseversion.hpp"
#include "commands/command.hpp"
#include "gitswitchdlg.hpp"
#include "progressdlg.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{

using namespace kgt;

/**
 * A repository with "main" and "feature". a.txt holds five lines; feature changes the first one and adds b.txt.
 * Tag v1 is on main's commit.
 */
struct SwitchSetup {
    TestRepo repo;

    SwitchSetup()
    {
        repo.write(QStringLiteral("a.txt"), "1\n2\n3\n4\n5\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("feature")});
        repo.write(QStringLiteral("a.txt"), "feature\n2\n3\n4\n5\n");
        repo.write(QStringLiteral("b.txt"), "b\n");
        repo.commitAll(QStringLiteral("feature work"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
    }

    /**
     * @param ref A ref.
     * @returns Its commit, or an empty string if it doesn't exist.
     */
    [[nodiscard]] QString rev(const QString& ref) const
    {
        try {
            return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), ref})).trimmed();
        } catch (const RuntimeError&) {
            return {};
        }
    }

    /**
     * @returns The branch HEAD points to, or an empty string for a detached HEAD.
     */
    [[nodiscard]] QString headBranch() const
    {
        try {
            const QString ref = QString::fromUtf8(repo.git().run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("HEAD")})).trimmed();
            return Git::stripRefName(ref);
        } catch (const RuntimeError&) {
            return {};
        }
    }

    /**
     * @param path Relative to the root.
     * @returns The file's content.
     */
    [[nodiscard]] QByteArray read(const QString& path) const
    {
        QFile file(repo.filePath(path));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }
};

/**
 * What a switch run through AppUtils::performSwitch showed.
 */
struct SwitchRun {
    bool succeeded = false; // performSwitch's result
    int status = -2; // The progress dialog's gitStatus(), -2 if it was never seen
    QString log; // Its log
};

/**
 * Runs AppUtils::performSwitch, closing its progress dialog with Close once git has ended.
 * @param workingTree The working tree.
 * @param options What to switch to.
 * @returns What happened.
 */
SwitchRun runSwitch(const QString& workingTree, const SwitchOptions& options)
{
    SwitchRun result;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&poll, &result] {
        auto *progress = qobject_cast<ProgressDlg *>(QApplication::activeModalWidget());
        if (progress && progress->isDone()) {
            result.status = progress->gitStatus();
            result.log = progress->logText();
            poll.stop();
            progress->accept();
        }
    });
    poll.start(50);
    result.succeeded = AppUtils::performSwitch(workingTree, options);
    return result;
}

/**
 * @param ref What to switch to.
 * @returns Options that switch to it with git's defaults.
 */
SwitchOptions switchOf(const QString& ref)
{
    SwitchOptions options;
    options.ref = ref;
    return options;
}

/**
 * Waits for a ChooseVersion to load its branches and tags.
 * @param chooser The chooser.
 * @returns Whether it loaded in time.
 */
bool waitLoaded(const ChooseVersion& chooser)
{
    if (!chooser.isLoading()) {
        return true;
    }
    QSignalSpy loaded(&chooser, &ChooseVersion::loaded);
    return loaded.wait(10000);
}

/**
 * @param parent A widget.
 * @param text A checkbox's label.
 * @returns The checkbox, or nullptr.
 */
QCheckBox *checkBox(const QWidget& parent, const QString& text)
{
    for (QCheckBox *box : parent.findChildren<QCheckBox *>()) {
        if (box->text() == text) {
            return box;
        }
    }
    return nullptr;
}

/**
 * The option widgets of a GitSwitchDlg, found by TortoiseGit's labels.
 */
struct SwitchWidgets {
    QCheckBox *branch = nullptr; // IDC_CHECK_BRANCH
    QLineEdit *newBranch = nullptr; // IDC_EDIT_BRANCH
    QCheckBox *force = nullptr; // IDC_CHECK_FORCE
    QCheckBox *merge = nullptr; // IDC_CHECK_MERGE
    QCheckBox *track = nullptr; // IDC_CHECK_TRACK
    QCheckBox *branchOverride = nullptr; // IDC_CHECK_BRANCHOVERRIDE

    explicit SwitchWidgets(const GitSwitchDlg& dlg)
        : branch(checkBox(dlg, QStringLiteral("Create &New Branch")))
        , force(checkBox(dlg, QStringLiteral("Overwrite working tree changes (&force)")))
        , merge(checkBox(dlg, QStringLiteral("&Merge")))
        , track(checkBox(dlg, QStringLiteral("T&rack")))
        , branchOverride(checkBox(dlg, QStringLiteral("&Override branch if exists")))
    {
        for (QLineEdit *edit : dlg.findChildren<QLineEdit *>()) {
            // Not the one inside the commit combo box
            if (!qobject_cast<QComboBox *>(edit->parent())) {
                newBranch = edit;
            }
        }
    }

    [[nodiscard]] bool found() const
    {
        return branch && newBranch && force && merge && track && branchOverride;
    }
};

/**
 * Accepts a dialog, answering and recording the message box it may show.
 * @param dlg The dialog.
 * @param answerPrimary Whether to answer the message box with its first button rather than close it.
 * @returns The message box's text, or an empty string if none was shown.
 */
QString acceptCollectingMessage(GitSwitchDlg& dlg, bool answerPrimary = false)
{
    QString message;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&poll, &message, answerPrimary] {
        QWidget *box = QApplication::activeModalWidget();
        if (!box) {
            return;
        }
        for (const QLabel *label : box->findChildren<QLabel *>()) {
            message += label->text();
        }
        poll.stop();
        if (answerPrimary) {
            box->findChild<QDialogButtonBox *>()->buttons().constFirst()->click();
        } else {
            box->close();
        }
    });
    poll.start(50);
    dlg.accept();
    return message;
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
 * Switch/Checkout: GitSwitchDlg's defaults and rules, CAppUtils::Switch's ref handling, and PerformSwitch's git
 * checkout run in temporary repositories.
 */
class SwitchTest : public QObject
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
        config->sync();
    }

    void commandExists()
    {
        QVERIFY(CommandServer::command(QStringLiteral("switch")));
    }

    void argumentsFollowPerformSwitch()
    {
        QCOMPARE(AppUtils::switchArguments(switchOf(QStringLiteral("feature"))),
                 (QStringList{QStringLiteral("checkout"), QStringLiteral("--end-of-options"), QStringLiteral("feature"), QStringLiteral("--")}));

        SwitchOptions options = switchOf(QStringLiteral("remotes/origin/x"));
        options.force = true;
        options.newBranch = QStringLiteral("x");
        options.merge = true;
        // Neither --track nor --no-track while Track is partially checked
        QCOMPARE(AppUtils::switchArguments(options),
                 (QStringList{QStringLiteral("checkout"),
                              QStringLiteral("-f"),
                              QStringLiteral("-b"),
                              QStringLiteral("x"),
                              QStringLiteral("--merge"),
                              QStringLiteral("--end-of-options"),
                              QStringLiteral("remotes/origin/x"),
                              QStringLiteral("--")}));
        options.track = Qt::Checked;
        options.branchOverride = true;
        options.force = false;
        options.merge = false;
        QCOMPARE(AppUtils::switchArguments(options),
                 (QStringList{QStringLiteral("checkout"),
                              QStringLiteral("--track"),
                              QStringLiteral("-B"),
                              QStringLiteral("x"),
                              QStringLiteral("--end-of-options"),
                              QStringLiteral("remotes/origin/x"),
                              QStringLiteral("--")}));
        options.track = Qt::Unchecked;
        QVERIFY(AppUtils::switchArguments(options).contains(QStringLiteral("--no-track")));
        // Track only matters with a new branch
        options.newBranch.clear();
        QCOMPARE(AppUtils::switchArguments(options),
                 (QStringList{QStringLiteral("checkout"), QStringLiteral("--end-of-options"), QStringLiteral("remotes/origin/x"), QStringLiteral("--")}));
    }

    void switchStripsHeadsPrefix()
    {
        GitSwitchOptions chosen;
        chosen.versionName = QStringLiteral("refs/heads/dup");
        chosen.newBranch = QStringLiteral("ignored");
        SwitchOptions options = AppUtils::switchOptions(chosen);
        QCOMPARE(options.ref, QStringLiteral("dup"));
        // No new branch unless asked for
        QVERIFY(options.newBranch.isEmpty());

        chosen.branchOverride = true;
        chosen.isNewBranch = true;
        chosen.track = Qt::Unchecked;
        options = AppUtils::switchOptions(chosen);
        QCOMPARE(options.ref, QStringLiteral("refs/heads/dup"));
        QCOMPARE(options.newBranch, QStringLiteral("ignored"));
        QVERIFY(options.branchOverride);
        QCOMPARE(options.track, Qt::Unchecked);

        // Tags keep their prefix
        GitSwitchOptions tag;
        tag.versionName = QStringLiteral("refs/tags/dup");
        QCOMPARE(AppUtils::switchOptions(tag).ref, QStringLiteral("refs/tags/dup"));
    }

    void dialogDefaults()
    {
        const SwitchSetup setup;
        GitSwitchDlg dlg(setup.repo.git());
        const SwitchWidgets widgets(dlg);
        QVERIFY(widgets.found());
        const QPushButton *okButton = dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        // OK waits for the branches
        QVERIFY(!okButton->isEnabled());
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QVERIFY(okButton->isEnabled());
        QVERIFY(dlg.windowTitle().endsWith(QStringLiteral(" - Switch/Checkout")));
        QCOMPARE(dlg.chooseVersion()->title(), QStringLiteral("Switch To"));

        // The current branch is offered and selected
        QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Branch);
        QCOMPARE(dlg.chooseVersion()->versionName(), QStringLiteral("main"));
        // SetDefaultName for a local branch: no new branch, no tracking, no override
        QVERIFY(!widgets.branch->isChecked());
        QCOMPARE(widgets.newBranch->text(), QStringLiteral("Branch_main"));
        QVERIFY(!widgets.newBranch->isEnabled());
        QVERIFY(!widgets.track->isEnabled());
        QCOMPARE(widgets.track->checkState(), Qt::Unchecked);
        QVERIFY(!widgets.branchOverride->isEnabled());
        QVERIFY(!widgets.force->isChecked());
        QVERIFY(!widgets.merge->isChecked());

        // OnBnClickedCheckBranch enables the name only
        widgets.branch->setChecked(true);
        QVERIFY(widgets.newBranch->isEnabled());
        widgets.branch->setChecked(false);
        QVERIFY(!widgets.newBranch->isEnabled());

        QComboBox *branches = dlg.chooseVersion()->branchCombo();
        branches->setCurrentIndex(branches->findText(QStringLiteral("feature")));
        QCOMPARE(widgets.newBranch->text(), QStringLiteral("Branch_feature"));
        widgets.force->setChecked(true);
        dlg.accept();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        const GitSwitchOptions& options = dlg.options();
        QCOMPARE(options.versionName, QStringLiteral("feature"));
        QVERIFY(!options.isNewBranch);
        QVERIFY(options.force);
        QVERIFY(!options.merge);
        QCOMPARE(AppUtils::switchArguments(AppUtils::switchOptions(options)),
                 (QStringList{QStringLiteral("checkout"),
                              QStringLiteral("-f"),
                              QStringLiteral("--end-of-options"),
                              QStringLiteral("feature"),
                              QStringLiteral("--")}));
    }

    void dialogRemoteBranchTracks()
    {
        const SwitchSetup setup;
        setup.repo.run({QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/topic/x"), QStringLiteral("feature")});
        const GitSwitchDlg dlg(setup.repo.git(), QStringLiteral("origin/topic/x"));
        const SwitchWidgets widgets(dlg);
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QCOMPARE(dlg.chooseVersion()->versionName(), QStringLiteral("remotes/origin/topic/x"));

        // A remote branch: a new local branch of the same name, tracked by default (neither flag)
        QVERIFY(widgets.branch->isChecked());
        QCOMPARE(widgets.newBranch->text(), QStringLiteral("topic/x"));
        QVERIFY(widgets.newBranch->isEnabled());
        QVERIFY(widgets.track->isEnabled());
        QCOMPARE(widgets.track->checkState(), Qt::PartiallyChecked);
        QVERIFY(widgets.branchOverride->isEnabled());

        // OnEnChangeEditBranch: another name stops tracking
        widgets.newBranch->setText(QStringLiteral("other"));
        QCOMPARE(widgets.track->checkState(), Qt::Unchecked);
        // A name the remote branch's ends with keeps it (Right(), not a whole path component)
        widgets.track->setCheckState(Qt::PartiallyChecked);
        widgets.newBranch->setText(QStringLiteral("x"));
        QCOMPARE(widgets.track->checkState(), Qt::PartiallyChecked);
    }

    void dialogTagAndCommitRememberNewBranch()
    {
        const SwitchSetup setup;
        const QString hash = setup.rev(QStringLiteral("feature"));
        {
            GitSwitchDlg dlg(setup.repo.git(), QStringLiteral("v1"));
            const SwitchWidgets widgets(dlg);
            QVERIFY(waitLoaded(*dlg.chooseVersion()));
            QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Tag);
            // SwitchToTagNewBranch is on by default
            QVERIFY(widgets.branch->isChecked());
            QCOMPARE(widgets.newBranch->text(), QStringLiteral("Branch_v1"));
            QVERIFY(widgets.branchOverride->isEnabled());
            QVERIFY(!widgets.track->isEnabled());

            // A commit gets its short hash
            dlg.chooseVersion()->selectRef(hash);
            QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Version);
            QVERIFY(widgets.branch->isChecked());
            QCOMPARE(widgets.newBranch->text(), QStringLiteral("Branch_") + hash.left(8));

            // Switching to the tag without a new branch is remembered for tags only
            dlg.chooseVersion()->selectRef(QStringLiteral("v1"));
            widgets.branch->setChecked(false);
            dlg.accept();
            QCOMPARE(dlg.result(), int(QDialog::Accepted));
            QVERIFY(!dlg.options().isNewBranch);
            QCOMPARE(dlg.options().versionName, QStringLiteral("v1"));
        }
        const GitSwitchDlg again(setup.repo.git(), QStringLiteral("v1"));
        const SwitchWidgets widgets(again);
        QVERIFY(waitLoaded(*again.chooseVersion()));
        QVERIFY(!widgets.branch->isChecked());
        QVERIFY(!widgets.branchOverride->isEnabled());
        again.chooseVersion()->selectRef(hash);
        QVERIFY(widgets.branch->isChecked());

        KSharedConfig::openConfig()->group(QStringLiteral("General")).deleteEntry("SwitchToTagNewBranch");
    }

    void dialogChecksNewBranchName()
    {
        const SwitchSetup setup;
        setup.repo.run({QStringLiteral("tag"), QStringLiteral("clash")});
        GitSwitchDlg dlg(setup.repo.git(), QStringLiteral("v1"));
        const SwitchWidgets widgets(dlg);
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QVERIFY(widgets.branch->isChecked());

        widgets.newBranch->setText(QStringLiteral("bad name"));
        QVERIFY(acceptCollectingMessage(dlg).contains(QStringLiteral("Branch/Tag name must not be empty or is invalid.")));
        QCOMPARE(dlg.result(), int(QDialog::Rejected));

        widgets.newBranch->setText(QStringLiteral(" feature "));
        QVERIFY(acceptCollectingMessage(dlg).contains(QStringLiteral("A branch with this name already exists.")));
        QCOMPARE(dlg.result(), int(QDialog::Rejected));

        // A tag of the same name asks first: Abort stays, Continue goes on
        widgets.newBranch->setText(QStringLiteral("clash"));
        QVERIFY(acceptCollectingMessage(dlg).contains(QStringLiteral("A tag with the same name")));
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        QVERIFY(acceptCollectingMessage(dlg, true).contains(QStringLiteral("A tag with the same name")));
        QCOMPARE(dlg.result(), int(QDialog::Accepted));

        // Override accepts an existing branch, and the name is trimmed
        widgets.branchOverride->setChecked(true);
        widgets.newBranch->setText(QStringLiteral(" feature "));
        QVERIFY(acceptCollectingMessage(dlg).isEmpty());
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.options().newBranch, QStringLiteral("feature"));
        QVERIFY(dlg.options().branchOverride);
    }

    void switchToBranch()
    {
        const SwitchSetup setup;
        const SwitchRun run = runSwitch(setup.repo.path(), switchOf(QStringLiteral("feature")));
        QVERIFY(run.succeeded);
        QCOMPARE(run.status, 0);
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QCOMPARE(setup.read(QStringLiteral("b.txt")), QByteArray("b\n"));
    }

    void switchToTagAndHashDetaches()
    {
        const SwitchSetup setup;
        QVERIFY(runSwitch(setup.repo.path(), switchOf(QStringLiteral("v1"))).succeeded);
        QVERIFY(setup.headBranch().isEmpty());
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), setup.rev(QStringLiteral("v1")));

        const QString hash = setup.rev(QStringLiteral("feature"));
        QVERIFY(runSwitch(setup.repo.path(), switchOf(hash)).succeeded);
        QVERIFY(setup.headBranch().isEmpty());
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), hash);
    }

    void createNewBranch()
    {
        const SwitchSetup setup;
        setup.repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), setup.repo.path()});
        setup.repo.run({QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/feature"), QStringLiteral("feature")});

        // With --no-track: no upstream
        SwitchOptions options = switchOf(QStringLiteral("remotes/origin/feature"));
        options.newBranch = QStringLiteral("local");
        options.track = Qt::Unchecked;
        QVERIFY(runSwitch(setup.repo.path(), options).succeeded);
        QCOMPARE(setup.headBranch(), QStringLiteral("local"));
        QCOMPARE(setup.rev(QStringLiteral("local")), setup.rev(QStringLiteral("feature")));
        QVERIFY(setup.repo.git().configValue(QStringLiteral("branch.local.remote")).isEmpty());

        // With --track: origin/feature upstream
        options.newBranch = QStringLiteral("tracking");
        options.track = Qt::Checked;
        QVERIFY(runSwitch(setup.repo.path(), options).succeeded);
        QCOMPARE(setup.headBranch(), QStringLiteral("tracking"));
        QCOMPARE(setup.repo.git().configValue(QStringLiteral("branch.tracking.remote")), QStringLiteral("origin"));
        QCOMPARE(setup.repo.git().configValue(QStringLiteral("branch.tracking.merge")), QStringLiteral("refs/heads/feature"));

        // -b refuses an existing branch
        options.ref = QStringLiteral("main");
        const SwitchRun refused = runSwitch(setup.repo.path(), options);
        QVERIFY(!refused.succeeded);
        QVERIFY(refused.status != 0);
        QCOMPARE(setup.headBranch(), QStringLiteral("tracking"));
    }

    void overrideExistingBranch()
    {
        const SwitchSetup setup;
        setup.repo.run({QStringLiteral("branch"), QStringLiteral("other")});
        SwitchOptions options = switchOf(QStringLiteral("feature"));
        options.newBranch = QStringLiteral("other");
        options.branchOverride = true;
        QVERIFY(runSwitch(setup.repo.path(), options).succeeded);
        QCOMPARE(setup.headBranch(), QStringLiteral("other"));
        QCOMPARE(setup.rev(QStringLiteral("other")), setup.rev(QStringLiteral("feature")));
    }

    void blockedByLocalChangesAndForce()
    {
        const SwitchSetup setup;
        setup.repo.write(QStringLiteral("a.txt"), "local\n2\n3\n4\n5\n");
        const SwitchRun blocked = runSwitch(setup.repo.path(), switchOf(QStringLiteral("feature")));
        QVERIFY(!blocked.succeeded);
        QVERIFY(blocked.status != 0);
        QVERIFY(blocked.log.contains(QStringLiteral("would be overwritten")));
        // The working tree is untouched
        QCOMPARE(setup.headBranch(), QStringLiteral("main"));
        QCOMPARE(setup.read(QStringLiteral("a.txt")), QByteArray("local\n2\n3\n4\n5\n"));
        QVERIFY(!QFileInfo::exists(setup.repo.filePath(QStringLiteral("b.txt"))));

        // Force throws the local change away
        SwitchOptions options = switchOf(QStringLiteral("feature"));
        options.force = true;
        QVERIFY(runSwitch(setup.repo.path(), options).succeeded);
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QCOMPARE(setup.read(QStringLiteral("a.txt")), QByteArray("feature\n2\n3\n4\n5\n"));
    }

    void mergeCarriesLocalChanges()
    {
        const SwitchSetup setup;
        // feature changes the first line, the local change the last: merged cleanly
        setup.repo.write(QStringLiteral("a.txt"), "1\n2\n3\n4\nlocal\n");
        SwitchOptions options = switchOf(QStringLiteral("feature"));
        options.merge = true;
        const SwitchRun run = runSwitch(setup.repo.path(), options);
        QVERIFY(run.succeeded);
        QCOMPARE(run.status, 0);
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QCOMPARE(setup.read(QStringLiteral("a.txt")), QByteArray("feature\n2\n3\n4\nlocal\n"));
        QVERIFY(!setup.repo.git().hasWorkingTreeConflicts());
    }

    void mergeConflictIsFailure()
    {
        const SwitchSetup setup;
        setup.repo.write(QStringLiteral("a.txt"), "local\n2\n3\n4\n5\n");
        SwitchOptions options = switchOf(QStringLiteral("feature"));
        options.merge = true;
        const SwitchRun run = runSwitch(setup.repo.path(), options);
        // git checkout --merge exits 0 here; PostExecCallback makes it a failure
        QVERIFY(!run.succeeded);
        QCOMPARE(run.status, 1);
        QVERIFY(run.log.contains(QStringLiteral("Has merge conflict")));
        QVERIFY(run.log.contains(QStringLiteral("git did not exit cleanly (exit code 1)")));
        QVERIFY(setup.repo.git().hasWorkingTreeConflicts());
    }

    void pickedBranchDoesNotDetach()
    {
        const SwitchSetup setup;
        // A branch and a tag named alike, on different commits: the dialog gives refs/heads/dup
        setup.repo.run({QStringLiteral("branch"), QStringLiteral("dup"), QStringLiteral("feature")});
        setup.repo.run({QStringLiteral("tag"), QStringLiteral("dup"), QStringLiteral("main")});
        GitSwitchDlg dlg(setup.repo.git(), QStringLiteral("refs/heads/dup"));
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        dlg.accept();
        QCOMPARE(dlg.options().versionName, QStringLiteral("refs/heads/dup"));

        QVERIFY(runSwitch(setup.repo.path(), AppUtils::switchOptions(dlg.options())).succeeded);
        QCOMPARE(setup.headBranch(), QStringLiteral("dup"));
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), setup.rev(QStringLiteral("feature")));
    }

    void screenshots()
    {
        const SwitchSetup setup;
        GitSwitchDlg dlg(setup.repo.git(), QStringLiteral("feature"));
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        screenshot(dlg, QStringLiteral("gitswitchdlg.png"));
    }
};

QTEST_MAIN(SwitchTest)

#include "switchtest.moc"
