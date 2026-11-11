// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "chooseversion.hpp"
#include "commands/mergecommand.hpp"
#include "mergeabortdlg.hpp"
#include "mergedlg.hpp"
#include "modalwatcher.hpp"
#include "progressdlg.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>

#include <tuple>

namespace
{

using namespace kgt;

/**
 * A repository with "main" (a.txt) and "feature", one commit ahead with b.txt, plus a tag v1 on main's commit.
 */
struct MergeSetup {
    TestRepo repo;

    MergeSetup()
    {
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("feature")});
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
     * @param ref A commit.
     * @returns How many parents it has.
     */
    [[nodiscard]] qsizetype parentCount(const QString& ref) const
    {
        const QString line =
            QString::fromLatin1(repo.git().run({QStringLiteral("rev-list"), QStringLiteral("--parents"), QStringLiteral("-n1"), ref})).trimmed();
        return line.split(QLatin1Char(' ')).size() - 1;
    }

    /**
     * Commits a change to a.txt on main, so that merging feature needs a merge commit.
     * @param content What a.txt then holds.
     */
    void divergeMain(const QByteArray& content = "a\nmain\n") const
    {
        repo.write(QStringLiteral("a.txt"), content);
        repo.commitAll(QStringLiteral("main work"));
    }
};

/**
 * Runs git in a ProgressDlg and waits for it to end.
 * @param workingTree Where.
 * @param arguments The arguments after "git".
 * @returns The exit status, or -2 if it didn't end in time.
 */
int runProgress(const QString& workingTree, const QStringList& arguments)
{
    ProgressDlg dlg(workingTree);
    dlg.addGitCmd(arguments);
    QSignalSpy finished(&dlg, &ProgressDlg::commandsFinished);
    dlg.start();
    if (!finished.wait(30000)) {
        return -2;
    }
    return dlg.gitStatus();
}

/**
 * @param options What to merge.
 * @returns The arguments without a message file.
 */
QStringList arguments(const MergeOptions& options)
{
    return MergeCommand::mergeArguments(options, {});
}

/**
 * @param version What to merge.
 * @returns Options that merge it with git's defaults.
 */
MergeOptions mergeOf(const QString& version)
{
    MergeOptions options;
    options.version = version;
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
 * Merge: ChooseVersion, MergeDlg, MergeCommand's git merge command line run in temporary repositories, and
 * MergeAbortDlg with the resets that abort a merge.
 */
class MergeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void parsesBranchList()
    {
        const QByteArray output =
            "  feature10\n* main\n+ other-worktree\n  feature9\n  remotes/origin/HEAD -> origin/main\n  remotes/origin/main\n  remotes/origin/a\n";
        BranchList list = Git::parseBranchList(output, BranchAllF, false, true);
        // GetBranchList's order: local first, numbers by value, then FETCH_HEAD among them
        QCOMPARE(list.branches,
                 (QStringList{QStringLiteral("feature9"),
                              QStringLiteral("feature10"),
                              QStringLiteral("FETCH_HEAD"),
                              QStringLiteral("main"),
                              QStringLiteral("other-worktree"),
                              QStringLiteral("remotes/origin/a"),
                              QStringLiteral("remotes/origin/main")}));
        QCOMPARE(list.current, 3);

        list = Git::parseBranchList(output, BranchAll, true, true);
        QVERIFY(!list.branches.contains(QStringLiteral("main")));
        QVERIFY(!list.branches.contains(QStringLiteral("FETCH_HEAD")));
        QCOMPARE(list.current, -1);

        // A detached HEAD is no branch
        list = Git::parseBranchList("* (HEAD detached at 1234567)\n  main\n", BranchAll, false, false);
        QCOMPARE(list.branches, QStringList{QStringLiteral("main")});
        QCOMPARE(list.current, -1);

        // Remote branches alone get the remotes/ prefix git branch -r leaves out
        QCOMPARE(Git::branchListArguments(BranchRemote), (QStringList{QStringLiteral("branch"), QStringLiteral("--no-color"), QStringLiteral("-r")}));
        QCOMPARE(Git::parseBranchList("  origin/main\n", BranchRemote, false, false).branches, QStringList{QStringLiteral("remotes/origin/main")});
        QCOMPARE(Git::parseTagList("v10\nv9\nV1\n"), (QStringList{QStringLiteral("V1"), QStringLiteral("v9"), QStringLiteral("v10")}));
    }

    void chooseVersionReturnsChoices()
    {
        const MergeSetup setup;
        ChooseVersion chooser(setup.repo.git(), QStringLiteral("From"));
        chooser.init();
        QVERIFY(chooser.isLoading());
        QVERIFY(waitLoaded(chooser));
        QCOMPARE(chooser.currentBranch(), QStringLiteral("main"));

        // Loaded with the current branch selected, as m_bSkipCurrentBranch is off by default
        QCOMPARE(chooser.choice(), ChooseVersion::Choice::Branch);
        QCOMPARE(chooser.versionName(), QStringLiteral("main"));
        QVERIFY(chooser.isBranch());

        chooser.selectRef(QStringLiteral("feature"));
        QCOMPARE(chooser.choice(), ChooseVersion::Choice::Branch);
        QCOMPARE(chooser.versionName(), QStringLiteral("feature"));
        QVERIFY(chooser.branchCombo()->isEnabled());
        QVERIFY(!chooser.tagCombo()->isEnabled());

        chooser.selectRef(QStringLiteral("v1"));
        QCOMPARE(chooser.choice(), ChooseVersion::Choice::Tag);
        QCOMPARE(chooser.versionName(), QStringLiteral("v1"));
        QVERIFY(!chooser.isBranch());
        QVERIFY(chooser.tagCombo()->isEnabled());
        QVERIFY(!chooser.browseRefButton()->isEnabled());

        const QString hash = setup.rev(QStringLiteral("feature"));
        chooser.selectRef(hash);
        QCOMPARE(chooser.choice(), ChooseVersion::Choice::Version);
        QCOMPARE(chooser.versionName(), hash);
        QVERIFY(chooser.versionCombo()->isEnabled());

        // A hash typed into the commit field
        chooser.versionCombo()->setEditText(QStringLiteral(" abc123 "));
        QCOMPARE(chooser.versionName(), QStringLiteral("abc123"));
    }

    void chooseVersionQualifiesAmbiguousNames()
    {
        const MergeSetup setup;
        setup.repo.run({QStringLiteral("branch"), QStringLiteral("dup")});
        setup.repo.run({QStringLiteral("tag"), QStringLiteral("dup")});
        ChooseVersion chooser(setup.repo.git(), QStringLiteral("From"));
        chooser.setInitialRefName(QStringLiteral("refs/heads/dup"));
        chooser.init();
        QVERIFY(waitLoaded(chooser));
        // UpdateRevsionName: a name both a branch and a tag have gets its prefix
        QCOMPARE(chooser.versionName(), QStringLiteral("refs/heads/dup"));
        chooser.setDefaultChoose(ChooseVersion::Choice::Tag);
        chooser.tagCombo()->setCurrentIndex(chooser.tagCombo()->findText(QStringLiteral("dup")));
        QCOMPARE(chooser.versionName(), QStringLiteral("refs/tags/dup"));
    }

    void mergeDlgDefaults()
    {
        const MergeSetup setup;
        setup.repo.run({QStringLiteral("config"), QStringLiteral("merge.log"), QStringLiteral("7")});
        MergeDlg dlg(setup.repo.git());
        const QPushButton *okButton = dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        // OK waits for the branches
        QVERIFY(!okButton->isEnabled());
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QVERIFY(okButton->isEnabled());
        QVERIFY(dlg.windowTitle().endsWith(QStringLiteral(" - Merge")));
        QCOMPARE(dlg.findChild<QLineEdit *>()->text(), QStringLiteral("main"));

        // The current branch is skipped, and with no push branch nothing is selected
        QComboBox *branches = dlg.chooseVersion()->branchCombo();
        QCOMPARE(branches->findText(QStringLiteral("main")), -1);
        QCOMPARE(branches->currentIndex(), -1);
        QCOMPARE(dlg.findChild<QSpinBox *>()->value(), 7);
        QCOMPARE(dlg.findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("<Auto Generated by Git>"));

        // OnBnClickedOk refuses an empty branch
        QString refusal;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &poll, [&poll, &refusal] {
            if (QWidget *box = QApplication::activeModalWidget()) {
                for (const QLabel *label : box->findChildren<QLabel *>()) {
                    refusal += label->text();
                }
                poll.stop();
                box->close();
            }
        });
        poll.start(50);
        dlg.accept();
        QVERIFY(refusal.contains(QStringLiteral("Branch/Tag name must not be empty or is invalid.")));
        QCOMPARE(dlg.result(), int(QDialog::Rejected));

        branches->setCurrentIndex(branches->findText(QStringLiteral("feature")));
        dlg.accept();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        const MergeOptions& options = dlg.options();
        QCOMPARE(options.version, QStringLiteral("feature"));
        QVERIFY(options.isBranch);
        // The auto-generated text leaves the message to git
        QVERIFY(options.message.isEmpty());
        QVERIFY(!options.log);
        QVERIFY(!options.noFF && !options.ffOnly && !options.squash && !options.noCommit);
        QVERIFY(options.strategy.isEmpty());
        QCOMPARE(arguments(options), (QStringList{QStringLiteral("merge"), QStringLiteral("--"), QStringLiteral("feature")}));
    }

    void mergeDlgSelectsPushBranch()
    {
        const MergeSetup setup;
        setup.repo.run({QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("feature")});
        setup.repo.run({QStringLiteral("config"), QStringLiteral("branch.main.remote"), QStringLiteral("origin")});
        setup.repo.run({QStringLiteral("config"), QStringLiteral("branch.main.merge"), QStringLiteral("refs/heads/main")});
        const MergeDlg dlg(setup.repo.git());
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QCOMPARE(dlg.chooseVersion()->versionName(), QStringLiteral("remotes/origin/main"));

        // An explicit ref wins
        const MergeDlg tagged(setup.repo.git(), QStringLiteral("v1"));
        QVERIFY(waitLoaded(*tagged.chooseVersion()));
        QCOMPARE(tagged.chooseVersion()->choice(), ChooseVersion::Choice::Tag);
    }

    void mergeDlgEnablingRules()
    {
        const MergeSetup setup;
        MergeDlg dlg(setup.repo.git(), QStringLiteral("feature"));
        QVERIFY(waitLoaded(*dlg.chooseVersion()));
        QCheckBox *squash = checkBox(dlg, QStringLiteral("&Squash"));
        QCheckBox *noFF = checkBox(dlg, QStringLiteral("No &Fast Forward"));
        QCheckBox *ffOnly = checkBox(dlg, QStringLiteral("Fast Forward O&nly"));
        QCheckBox *log = checkBox(dlg, QStringLiteral("Messages"));
        QVERIFY(squash && noFF && ffOnly && log);
        auto *message = dlg.findChild<QPlainTextEdit *>();
        const auto *logNum = dlg.findChild<QSpinBox *>();
        QCOMPARE(logNum->value(), 20);
        QVERIFY(!logNum->isEnabled());
        log->setChecked(true);
        QVERIFY(logNum->isEnabled());

        // Squash: no message and no --no-ff
        squash->setChecked(true);
        QVERIFY(!message->isEnabled());
        QVERIFY(!noFF->isEnabled());
        squash->setChecked(false);
        QVERIFY(message->isEnabled());

        // No Fast Forward and Fast Forward Only exclude each other, and No Fast Forward excludes Squash
        noFF->setChecked(true);
        QVERIFY(!ffOnly->isEnabled());
        QVERIFY(!squash->isEnabled());
        noFF->setChecked(false);
        ffOnly->setChecked(true);
        QVERIFY(!noFF->isEnabled());
        QVERIFY(squash->isEnabled());

        // The strategy option needs "recursive", and only some options take a parameter
        const QList<QComboBox *> combos = dlg.findChildren<QComboBox *>();
        QComboBox *strategy = nullptr;
        QComboBox *strategyOption = nullptr;
        for (QComboBox *combo : combos) {
            if (combo->findText(QStringLiteral("resolve")) >= 0) {
                strategy = combo;
            } else if (combo->findText(QStringLiteral("rename-threshold")) >= 0) {
                strategyOption = combo;
            }
        }
        QVERIFY(strategy && strategyOption);
        QCOMPARE(strategy->currentIndex(), -1);
        QVERIFY(!strategyOption->isEnabled());
        strategy->setCurrentText(QStringLiteral("recursive"));
        QVERIFY(strategyOption->isEnabled());
        strategyOption->setCurrentText(QStringLiteral("rename-threshold"));
        QLineEdit *param = nullptr;
        for (QLineEdit *edit : dlg.findChildren<QLineEdit *>()) {
            // Not the ones inside the combo boxes and the spin box
            if (!edit->isReadOnly() && !qobject_cast<QComboBox *>(edit->parent()) && !qobject_cast<QAbstractSpinBox *>(edit->parent())) {
                param = edit;
            }
        }
        QVERIFY(param);
        QVERIFY(param->isEnabled());
        param->setText(QStringLiteral("50%"));
        message->setPlainText(QStringLiteral("Merge it"));

        dlg.accept();
        const MergeOptions& options = dlg.options();
        QVERIFY(options.ffOnly);
        QVERIFY(options.log == 20);
        QCOMPARE(options.message, QStringLiteral("Merge it"));
        QCOMPARE(arguments(options),
                 (QStringList{QStringLiteral("merge"),
                              QStringLiteral("--ff-only"),
                              QStringLiteral("--log=20"),
                              QStringLiteral("--strategy=recursive"),
                              QStringLiteral("--strategy-option=rename-threshold=50%"),
                              QStringLiteral("-F"),
                              QString(),
                              QStringLiteral("--"),
                              QStringLiteral("feature")}));

        // OnBnClickedOk drops an option the strategy doesn't take
        strategy->setCurrentText(QStringLiteral("ours"));
        dlg.accept();
        QCOMPARE(dlg.options().strategy, QStringLiteral("ours"));
        QVERIFY(dlg.options().strategyOption.isEmpty());
        QVERIFY(dlg.options().strategyParam.isEmpty());
    }

    void mergeArgumentsFollowDoMerge()
    {
        MergeOptions options = mergeOf(QStringLiteral("feature"));
        options.noFF = true;
        options.ffOnly = true; // Ignored with --no-ff
        options.squash = true;
        options.noCommit = true;
        options.unrelated = true;
        options.strategy = QStringLiteral("resolve");
        options.strategyOption = QStringLiteral("ours"); // Only with a strategy
        options.message = QStringLiteral("msg");
        QCOMPARE(MergeCommand::mergeArguments(options, QStringLiteral("/tmp/msg")),
                 (QStringList{QStringLiteral("merge"),
                              QStringLiteral("--no-ff"),
                              QStringLiteral("--squash"),
                              QStringLiteral("--no-commit"),
                              QStringLiteral("--allow-unrelated-histories"),
                              QStringLiteral("--strategy=resolve"),
                              QStringLiteral("--strategy-option=ours"),
                              QStringLiteral("-F"),
                              QStringLiteral("/tmp/msg"),
                              QStringLiteral("--"),
                              QStringLiteral("feature")}));
    }

    void mergeFastForward()
    {
        const MergeSetup setup;
        QCOMPARE(runProgress(setup.repo.path(), arguments(mergeOf(QStringLiteral("feature")))), 0);
        QCOMPARE(setup.rev(QStringLiteral("main")), setup.rev(QStringLiteral("feature")));

        // Merging again: already up to date, and still a success
        ProgressDlg again(setup.repo.path());
        again.addGitCmd(arguments(mergeOf(QStringLiteral("feature"))));
        QSignalSpy finished(&again, &ProgressDlg::commandsFinished);
        again.start();
        QVERIFY(finished.wait(30000));
        QCOMPARE(again.gitStatus(), 0);
        QVERIFY(again.logText().contains(QStringLiteral("Already up to date")));
    }

    void mergeCommit()
    {
        const MergeSetup setup;
        setup.divergeMain();
        MergeOptions options = mergeOf(QStringLiteral("feature"));
        options.message = QStringLiteral("Merge feature into main\n\nWith a body");
        QTemporaryFile messageFile;
        QVERIFY(messageFile.open());
        messageFile.write(options.message.toUtf8());
        QVERIFY(messageFile.flush());
        QCOMPARE(runProgress(setup.repo.path(), MergeCommand::mergeArguments(options, messageFile.fileName())), 0);
        QCOMPARE(setup.parentCount(QStringLiteral("HEAD")), 2);
        QCOMPARE(QString::fromUtf8(setup.repo.git().run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")})).trimmed(),
                 options.message);

        // --no-ff makes a merge commit even where a fast-forward would do
        const MergeSetup fastForward;
        options = mergeOf(QStringLiteral("feature"));
        options.noFF = true;
        QCOMPARE(runProgress(fastForward.repo.path(), arguments(options)), 0);
        QCOMPARE(fastForward.parentCount(QStringLiteral("HEAD")), 2);
    }

    void mergeSquash()
    {
        const MergeSetup setup;
        const QString before = setup.rev(QStringLiteral("HEAD"));
        MergeOptions options = mergeOf(QStringLiteral("feature"));
        options.squash = true;
        QCOMPARE(runProgress(setup.repo.path(), arguments(options)), 0);
        // No commit: the changes are staged for one
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), before);
        QCOMPARE(QString::fromUtf8(setup.repo.git().run({QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only")})).trimmed(),
                 QStringLiteral("b.txt"));
        QVERIFY(setup.rev(QStringLiteral("MERGE_HEAD")).isEmpty());
    }

    void mergeFastForwardOnlyRefuses()
    {
        const MergeSetup setup;
        setup.divergeMain();
        const QString before = setup.rev(QStringLiteral("HEAD"));
        MergeOptions options = mergeOf(QStringLiteral("feature"));
        options.ffOnly = true;
        QVERIFY(runProgress(setup.repo.path(), arguments(options)) != 0);
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), before);
        QVERIFY(!setup.repo.git().hasWorkingTreeConflicts());
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void mergeConflictAndAbort_data()
    {
        QTest::addColumn<int>("resetType");
        QTest::newRow("merge") << int(ResetType::Merge);
        QTest::newRow("mixed") << int(ResetType::Mixed);
        QTest::newRow("hard") << int(ResetType::Hard);
    }

    void mergeConflictAndAbort()
    {
        QFETCH(const int, resetType);
        const MergeSetup setup;
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("feature")});
        setup.repo.write(QStringLiteral("a.txt"), "feature\n");
        setup.repo.commitAll(QStringLiteral("feature changes a"));
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        setup.divergeMain("main\n");
        const QString before = setup.rev(QStringLiteral("HEAD"));

        QVERIFY(runProgress(setup.repo.path(), arguments(mergeOf(QStringLiteral("feature")))) != 0);
        // Left in git's merge state, which the plugin's Abort Merge entry looks for
        QVERIFY(setup.repo.git().hasWorkingTreeConflicts());
        QVERIFY(QFileInfo::exists(setup.repo.filePath(QStringLiteral(".git/MERGE_HEAD"))));

        // MergeAbortDlg's choice, run as CAppUtils::MergeAbort does
        MergeAbortDlg dlg(setup.repo.path());
        QCOMPARE(dlg.resetType(), ResetType::Merge);
        dlg.setResetType(static_cast<ResetType>(resetType));
        QCOMPARE(dlg.resetType(), static_cast<ResetType>(resetType));
        QCOMPARE(runProgress(setup.repo.path(), AppUtils::resetArguments(dlg.resetType(), QStringLiteral("HEAD"))), 0);

        QCOMPARE(setup.rev(QStringLiteral("HEAD")), before);
        QVERIFY(!QFileInfo::exists(setup.repo.filePath(QStringLiteral(".git/MERGE_HEAD"))));
        QVERIFY(!setup.repo.git().hasWorkingTreeConflicts());
        // Merge and hard restore the file; mixed leaves the working tree as the merge left it
        const QString status = QString::fromUtf8(setup.repo.git().run({QStringLiteral("status"), QStringLiteral("--porcelain")}));
        if (static_cast<ResetType>(resetType) == ResetType::Mixed) {
            QVERIFY(status.contains(QStringLiteral(" M a.txt")));
        } else {
            QVERIFY(status.isEmpty());
        }
    }

    void resetArgumentsFollowReset()
    {
        QCOMPARE(AppUtils::resetArguments(ResetType::Merge, QStringLiteral("HEAD")), (QStringList{QStringLiteral("reset"), QStringLiteral("--merge")}));
        QCOMPARE(
            AppUtils::resetArguments(ResetType::Hard, QStringLiteral("HEAD")),
            (QStringList{QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("--end-of-options"), QStringLiteral("HEAD"), QStringLiteral("--")}));
    }

    void fixesFetchHead()
    {
        const MergeSetup setup;
        const QString feature = setup.rev(QStringLiteral("feature"));
        const QString main = setup.rev(QStringLiteral("main"));
        setup.repo.write(
            QStringLiteral(".git/FETCH_HEAD"),
            (feature + QStringLiteral("\t\tbranch 'feature' of /x\n") + main + QStringLiteral("\tnot-for-merge\tbranch 'main' of /x\n")).toLatin1());
        QCOMPARE(setup.repo.git().fixBranchName(QStringLiteral("FETCH_HEAD")), feature);
        QCOMPARE(setup.repo.git().fixBranchName(QStringLiteral("feature")), QStringLiteral("feature"));
        // FETCH_HEAD is offered as a branch while it names one commit
        ChooseVersion chooser(setup.repo.git(), QStringLiteral("From"));
        chooser.init();
        QVERIFY(waitLoaded(chooser));
        QVERIFY(chooser.branchCombo()->findText(QStringLiteral("FETCH_HEAD")) >= 0);

        // Two commits to merge: an octopus, none to pick
        setup.repo.write(QStringLiteral(".git/FETCH_HEAD"), (feature + QStringLiteral("\t\ta\n") + main + QStringLiteral("\t\tb\n")).toLatin1());
        QVERIFY(setup.repo.git().derefFetchHead().isEmpty());
    }

    void mergeOffersDoMergesFollowUps()
    {
        const auto acceptMergeDlg = [](bool squash) {
            return [squash](QWidget *modal) {
                auto *dlg = qobject_cast<MergeDlg *>(modal);
                if (!dlg) {
                    return false;
                }
                if (!dlg->chooseVersion()->isLoading()) {
                    checkBox(*dlg, QStringLiteral("&Squash"))->setChecked(squash);
                    dlg->accept();
                }
                return true;
            };
        };
        const QString removeBranch = QStringLiteral("Remove &branch");
        const QString push = QStringLiteral("Pus&h...");
        const QString commit = QStringLiteral("&Commit...");
        const QString stash = QStringLiteral("Stash changes");

        // A branch merged: Remove branch, which deletes it once confirmed, and Push
        const MergeSetup setup;
        {
            const CommandDriver driver({removeBranch}, acceptMergeDlg(false));
            QVERIFY(MergeCommand::merge(setup.repo.path(), QStringLiteral("feature")));
            QCOMPARE(driver.offered(), QList<QStringList>{(QStringList{removeBranch, push})});
        }
        QVERIFY(setup.rev(QStringLiteral("refs/heads/feature")).isEmpty());

        // A squash leaves a commit to make; a merge after stashing offers the stash back first
        const MergeSetup squashed;
        {
            const CommandDriver driver({}, acceptMergeDlg(true));
            QVERIFY(MergeCommand::merge(squashed.repo.path(), QStringLiteral("feature"), nullptr, true));
            QCOMPARE(driver.offered(), QList<QStringList>{(QStringList{QStringLiteral("Stash Pop"), commit})});
        }

        // Conflicts: Resolve and Commit, then Stash changes
        const MergeSetup conflicting;
        conflicting.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("feature")});
        conflicting.repo.write(QStringLiteral("a.txt"), "feature\n");
        conflicting.repo.commitAll(QStringLiteral("feature changes a"));
        conflicting.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        conflicting.divergeMain("main\n");
        {
            const CommandDriver driver({}, acceptMergeDlg(false));
            QVERIFY(!MergeCommand::merge(conflicting.repo.path(), QStringLiteral("feature")));
            QCOMPARE(driver.offered(), QList<QStringList>{(QStringList{QStringLiteral("Resolve"), commit, stash})});
        }
        QVERIFY(conflicting.repo.git().hasWorkingTreeConflicts());

        // Nothing in common: Merge unrelated history retries with --allow-unrelated-histories
        const MergeSetup unrelated;
        unrelated.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("--orphan"), QStringLiteral("other")});
        unrelated.repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("-rf"), QStringLiteral(".")});
        unrelated.repo.write(QStringLiteral("other.txt"), "other\n");
        unrelated.repo.commitAll(QStringLiteral("unrelated"));
        unrelated.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        {
            const QString retry = QStringLiteral("&Merge unrelated history");
            const CommandDriver driver({retry}, acceptMergeDlg(false));
            QVERIFY(!MergeCommand::merge(unrelated.repo.path(), QStringLiteral("other")));
            QCOMPARE(driver.offered(), (QList<QStringList>{QStringList{retry, stash}, QStringList{removeBranch, push}}));
        }
        QCOMPARE(unrelated.parentCount(QStringLiteral("HEAD")), 2);
    }

    void screenshots()
    {
        const MergeSetup setup;
        MergeDlg merge(setup.repo.git(), QStringLiteral("feature"));
        QVERIFY(waitLoaded(*merge.chooseVersion()));
        screenshot(merge, QStringLiteral("mergedlg.png"));
        MergeAbortDlg abort(setup.repo.path());
        screenshot(abort, QStringLiteral("mergeabortdlg.png"));
    }
};

QTEST_MAIN(MergeTest)

#include "mergetest.moc"
