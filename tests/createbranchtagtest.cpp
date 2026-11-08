// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "chooseversion.hpp"
#include "commands/command.hpp"
#include "createbranchtagdlg.hpp"
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>

namespace
{

using namespace kgt;

/**
 * A repository on "main" with a "feature" branch one commit ahead, tag v1 on main's commit, and remote-tracking
 * branches origin/topic and origin/feature/nested.
 */
struct BranchSetup {
    TestRepo repo;

    BranchSetup()
    {
        repo.write(QStringLiteral("a.txt"), "1\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("feature")});
        repo.write(QStringLiteral("a.txt"), "2\n");
        repo.commitAll(QStringLiteral("feature work"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), QStringLiteral("https://example.invalid/repo.git")});
        for (const QString& ref : {QStringLiteral("refs/remotes/origin/topic"), QStringLiteral("refs/remotes/origin/feature/nested")}) {
            repo.run({QStringLiteral("update-ref"), ref, QStringLiteral("feature")});
        }
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
     * @returns The branch HEAD points to.
     */
    [[nodiscard]] QString headBranch() const
    {
        return repo.git().currentBranch();
    }
};

/**
 * The widgets of a CreateBranchTagDlg, found by TortoiseGit's labels.
 */
struct DlgWidgets {
    QLineEdit *name = nullptr; // IDC_BRANCH_TAG
    QCheckBox *track = nullptr; // IDC_CHECK_TRACK
    QCheckBox *force = nullptr; // IDC_CHECK_FORCE
    QCheckBox *switchTo = nullptr; // IDC_CHECK_SWITCH
    QPlainTextEdit *message = nullptr; // IDC_EDIT_MESSAGE
    QPushButton *okButton = nullptr; // IDOK

    explicit DlgWidgets(const CreateBranchTagDlg& dlg)
        : message(dlg.findChild<QPlainTextEdit *>())
        , okButton(dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok))
    {
        for (QCheckBox *box : dlg.findChildren<QCheckBox *>()) {
            if (box->text() == QStringLiteral("Trac&k")) {
                track = box;
            } else if (box->text() == QStringLiteral("&Force")) {
                force = box;
            } else if (box->text() == QStringLiteral("&Switch to new branch")) {
                switchTo = box;
            }
        }
        for (QLineEdit *edit : dlg.findChildren<QLineEdit *>()) {
            // Not the one inside the commit combo box
            if (!qobject_cast<QComboBox *>(edit->parent())) {
                name = edit;
            }
        }
    }

    [[nodiscard]] bool found() const
    {
        return name && track && force && switchTo && message && okButton;
    }
};

/**
 * Waits for a dialog's ChooseVersion to load its branches and tags.
 * @param dlg The dialog.
 * @returns Whether it loaded in time.
 */
bool waitLoaded(const CreateBranchTagDlg& dlg)
{
    const ChooseVersion *chooser = dlg.chooseVersion();
    if (!chooser->isLoading()) {
        return true;
    }
    QSignalSpy loaded(chooser, &ChooseVersion::loaded);
    return loaded.wait(10000);
}

/**
 * @param box A message box.
 * @returns The text of its labels.
 */
QString messageOf(const QWidget& box)
{
    QString message;
    for (const QLabel *label : box.findChildren<QLabel *>()) {
        message += label->text();
    }
    return message;
}

/**
 * Accepts a dialog, answering and recording the message box it may show.
 * @param dlg The dialog.
 * @param answerPrimary Whether to answer the message box with its first button rather than close it.
 * @returns The message box's text, or an empty string if none was shown.
 */
QString acceptCollectingMessage(CreateBranchTagDlg& dlg, bool answerPrimary = false)
{
    QString message;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&poll, &message, answerPrimary] {
        QWidget *box = QApplication::activeModalWidget();
        if (!box) {
            return;
        }
        message = messageOf(*box);
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
 * What a run of AppUtils::createBranchTag showed.
 */
struct CreateRun {
    bool created = false; // createBranchTag's result
    QString message; // The error box's text, if one was shown
    bool sawProgress = false; // Whether the switch's progress dialog was shown
};

/**
 * Runs AppUtils::createBranchTag: fills in the dialog once it has loaded, accepts it, and closes what follows.
 * @param workingTree The working tree.
 * @param params What the dialog starts with.
 * @param fill Fills in the dialog.
 * @returns What happened.
 */
CreateRun runCreate(const QString& workingTree, const CreateBranchTagParams& params, const std::function<void(CreateBranchTagDlg&)>& fill)
{
    CreateRun result;
    bool accepted = false;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            return;
        }
        if (auto *dlg = qobject_cast<CreateBranchTagDlg *>(modal)) {
            if (accepted) {
                // The dialog refused its input: nothing more will happen
                dlg->reject();
            } else if (!dlg->chooseVersion()->isLoading()) {
                accepted = true;
                fill(*dlg);
                // Queued: this slot must return for the timer to see what the dialog shows next
                QMetaObject::invokeMethod(dlg, &CreateBranchTagDlg::accept, Qt::QueuedConnection);
            }
        } else if (auto *progress = qobject_cast<ProgressDlg *>(modal)) {
            if (progress->isDone()) {
                result.sawProgress = true;
                progress->accept();
            }
        } else {
            result.message = messageOf(*modal);
            modal->close();
        }
    });
    poll.start(20);
    result.created = AppUtils::createBranchTag(workingTree, params, nullptr);
    return result;
}

/**
 * @param name The branch's name.
 * @param base What to base it on.
 * @returns What the dialog collects for them, with git's defaults otherwise.
 */
CreateBranchTagOptions optionsOf(const QString& name, const QString& base)
{
    CreateBranchTagOptions options;
    options.branchTagName = name;
    options.versionName = base;
    return options;
}

/**
 * @returns The value "Switch to new branch" is remembered in.
 */
bool rememberedSwitch()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("General")).readEntry("NewBranchSwitchTo", false);
}

/**
 * @param value What to remember for "Switch to new branch".
 */
void rememberSwitch(bool value)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("General"));
    group.writeEntry("NewBranchSwitchTo", value);
    group.sync();
}

} // namespace

/**
 * Create Branch: CreateBranchTagDlg's defaults and rules, and CAppUtils::CreateBranchTag's git branch run in
 * temporary repositories.
 */
class CreateBranchTagTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
        // The remembered choice goes to a throwaway configuration
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        rememberSwitch(false);
    }

    void commandExists()
    {
        const auto command = CommandServer::command(QStringLiteral("branch"));
        QVERIFY(command);
        QCOMPARE((*command)->pathRequirement(), PathRequirement::WorkingTreeOrBareRepoRequired);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void argumentsFollowCreateBranchTag_data()
    {
        QTest::addColumn<bool>("force");
        QTest::addColumn<Qt::CheckState>("track");
        QTest::addColumn<QStringList>("flags");

        QTest::newRow("defaults") << false << Qt::PartiallyChecked << QStringList{};
        QTest::newRow("force") << true << Qt::PartiallyChecked << QStringList{QStringLiteral("-f")};
        QTest::newRow("track") << false << Qt::Checked << QStringList{QStringLiteral("--track")};
        QTest::newRow("no track") << false << Qt::Unchecked << QStringList{QStringLiteral("--no-track")};
        QTest::newRow("force and track") << true << Qt::Checked << QStringList{QStringLiteral("-f"), QStringLiteral("--track")};
    }

    void argumentsFollowCreateBranchTag()
    {
        QFETCH(const bool, force);
        QFETCH(const Qt::CheckState, track);
        QFETCH(const QStringList, flags);

        CreateBranchTagOptions options = optionsOf(QStringLiteral("topic"), QStringLiteral("HEAD"));
        options.force = force;
        options.track = track;
        QStringList expected{QStringLiteral("branch")};
        expected << flags << QStringLiteral("--") << QStringLiteral("topic") << QStringLiteral("HEAD");
        QCOMPARE(AppUtils::createBranchArguments(options), expected);
    }

    void dialogDefaults()
    {
        const BranchSetup setup;
        const CreateBranchTagDlg dlg(setup.repo.git());
        const DlgWidgets widgets(dlg);
        QVERIFY(widgets.found());

        QVERIFY(dlg.windowTitle().endsWith(QStringLiteral(" - Create Branch")));
        QCOMPARE(dlg.chooseVersion()->title(), QStringLiteral("Base On"));
        QVERIFY(!widgets.okButton->isEnabled());
        QVERIFY(waitLoaded(dlg));
        QVERIFY(widgets.okButton->isEnabled());

        QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Head);
        QCOMPARE(dlg.chooseVersion()->headRadio()->text(), QStringLiteral("&HEAD (main)"));
        QVERIFY(!dlg.chooseVersion()->headRadio()->isHidden());
        QCOMPARE(dlg.chooseVersion()->versionName(), QStringLiteral("HEAD"));

        QVERIFY(widgets.name->text().isEmpty());
        QCOMPARE(widgets.track->checkState(), Qt::PartiallyChecked);
        QVERIFY(!widgets.track->isEnabled());
        QVERIFY(!widgets.force->isChecked());
        QVERIFY(!widgets.switchTo->isHidden());
        QVERIFY(!widgets.switchTo->isChecked());
    }

    void otherHostsHaveNoHeadChoice()
    {
        const BranchSetup setup;
        const ChooseVersion chooser(setup.repo.git(), QStringLiteral("From"));
        QVERIFY(chooser.headRadio()->isHidden());
        QCOMPARE(chooser.choice(), ChooseVersion::Choice::Branch);
    }

    void startsFromParams()
    {
        const BranchSetup setup;
        const CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = QStringLiteral("v1"), .name = QStringLiteral("wanted"), .switchNewBranch = true});
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));
        QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Tag);
        QCOMPARE(dlg.chooseVersion()->versionName(), QStringLiteral("v1"));
        QCOMPARE(widgets.name->text(), QStringLiteral("wanted"));
        QVERIFY(widgets.switchTo->isChecked());
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void createsFromBase_data()
    {
        QTest::addColumn<QString>("initialRef");
        QTest::addColumn<QString>("expectedBase");

        QTest::newRow("HEAD") << QString() << QStringLiteral("main");
        QTest::newRow("branch") << QStringLiteral("feature") << QStringLiteral("feature");
        QTest::newRow("tag") << QStringLiteral("v1") << QStringLiteral("v1");
    }

    void createsFromBase()
    {
        QFETCH(const QString, initialRef);
        QFETCH(const QString, expectedBase);

        const BranchSetup setup;
        const CreateRun run = runCreate(setup.repo.path(),
                                        {.initialRefName = initialRef, .name = QStringLiteral("created"), .switchNewBranch = false},
                                        [](CreateBranchTagDlg&) { });
        QVERIFY2(run.created, qPrintable(run.message));
        QCOMPARE(setup.rev(QStringLiteral("refs/heads/created")), setup.rev(expectedBase));
        QCOMPARE(setup.headBranch(), QStringLiteral("main"));
        QVERIFY(!run.sawProgress);
    }

    void createsFromCommitHash()
    {
        const BranchSetup setup;
        const QString hash = setup.rev(QStringLiteral("feature"));
        const CreateRun run =
            runCreate(setup.repo.path(), {.initialRefName = hash, .name = QStringLiteral("created"), .switchNewBranch = false}, [](CreateBranchTagDlg& dlg) {
                QCOMPARE(dlg.chooseVersion()->choice(), ChooseVersion::Choice::Version);
            });
        QVERIFY2(run.created, qPrintable(run.message));
        QCOMPARE(setup.rev(QStringLiteral("refs/heads/created")), hash);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void trackStates_data()
    {
        QTest::addColumn<Qt::CheckState>("track");
        QTest::addColumn<QString>("expectedRemote");

        // Neither flag: git's branch.autoSetupMerge default tracks a remote-tracking base
        QTest::newRow("indeterminate") << Qt::PartiallyChecked << QStringLiteral("origin");
        QTest::newRow("on") << Qt::Checked << QStringLiteral("origin");
        QTest::newRow("off") << Qt::Unchecked << QString();
    }

    void trackStates()
    {
        QFETCH(const Qt::CheckState, track);
        QFETCH(const QString, expectedRemote);

        const BranchSetup setup;
        const CreateRun run = runCreate(setup.repo.path(),
                                        {.initialRefName = QStringLiteral("remotes/origin/topic"), .name = {}, .switchNewBranch = false},
                                        [track](CreateBranchTagDlg& dlg) {
                                            const DlgWidgets widgets(dlg);
                                            QVERIFY(widgets.track->isEnabled());
                                            widgets.track->setCheckState(track);
                                        });
        QVERIFY2(run.created, qPrintable(run.message));
        QCOMPARE(setup.rev(QStringLiteral("refs/heads/topic")), setup.rev(QStringLiteral("feature")));
        QCOMPARE(setup.repo.git().configValue(QStringLiteral("branch.topic.remote")), expectedRemote);
    }

    void remoteBranchFillsTheName()
    {
        const BranchSetup setup;
        const CreateBranchTagDlg dlg(setup.repo.git());
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));

        // Empty: filled in
        dlg.chooseVersion()->selectRef(QStringLiteral("remotes/origin/topic"), false);
        QCOMPARE(widgets.name->text(), QStringLiteral("topic"));
        QVERIFY(widgets.track->isEnabled());
        QCOMPARE(widgets.track->checkState(), Qt::PartiallyChecked);

        // Still the default name: replaced, and everything after the remote's name is kept
        dlg.chooseVersion()->selectRef(QStringLiteral("remotes/origin/feature/nested"), false);
        QCOMPARE(widgets.name->text(), QStringLiteral("feature/nested"));

        // A local branch: the name stays, Track goes
        dlg.chooseVersion()->selectRef(QStringLiteral("refs/heads/feature"), false);
        QCOMPARE(widgets.name->text(), QStringLiteral("feature/nested"));
        QVERIFY(!widgets.track->isEnabled());
    }

    void typedNameIsKeptAndStopsTracking()
    {
        const BranchSetup setup;
        const CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("mine"), .switchNewBranch = false});
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));

        dlg.chooseVersion()->selectRef(QStringLiteral("remotes/origin/topic"), false);
        QCOMPARE(widgets.name->text(), QStringLiteral("mine"));
        QCOMPARE(widgets.track->checkState(), Qt::PartiallyChecked);

        // OnEnChangeBranchTag: the name no longer matches the remote branch's
        widgets.name->setText(QStringLiteral("other"));
        QCOMPARE(widgets.track->checkState(), Qt::Unchecked);
    }

    void refusesAnExistingName()
    {
        const BranchSetup setup;
        const QString before = setup.rev(QStringLiteral("feature"));
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("feature"), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        const QString message = acceptCollectingMessage(dlg);
        QVERIFY2(message.contains(QStringLiteral("A branch with this name already exists.")), qPrintable(message));
        QVERIFY(message.contains(QStringLiteral("\"Force\"")));
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
        QCOMPARE(setup.rev(QStringLiteral("feature")), before);
    }

    void forceMovesAnExistingBranch()
    {
        const BranchSetup setup;
        const CreateRun run =
            runCreate(setup.repo.path(), {.initialRefName = {}, .name = QStringLiteral("feature"), .switchNewBranch = false}, [](CreateBranchTagDlg& dlg) {
                DlgWidgets(dlg).force->setChecked(true);
            });
        QVERIFY2(run.created, qPrintable(run.message));
        QCOMPARE(setup.rev(QStringLiteral("feature")), setup.rev(QStringLiteral("main")));
    }

    void refusesTheCurrentBranch()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("main"), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        const QString message = acceptCollectingMessage(dlg);
        QVERIFY2(message.contains(QStringLiteral("Cannot force updating current branch")), qPrintable(message));
    }

    void showsGitsErrorWhenItFails()
    {
        const BranchSetup setup;
        const QString before = setup.rev(QStringLiteral("main"));
        const CreateRun run = runCreate(setup.repo.path(),
                                        {.initialRefName = QStringLiteral("feature"), .name = QStringLiteral("main"), .switchNewBranch = false},
                                        [](CreateBranchTagDlg& dlg) {
                                            DlgWidgets(dlg).force->setChecked(true);
                                        });
        QVERIFY(!run.created);
        QVERIFY2(run.message.contains(QStringLiteral("cannot force update the branch")), qPrintable(run.message));
        QCOMPARE(setup.rev(QStringLiteral("main")), before);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void refusesInvalidNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << QStringLiteral("   ");
        QTest::newRow("double dot") << QStringLiteral("bad..name");
        QTest::newRow("leading dash") << QStringLiteral("-bad");
        QTest::newRow("HEAD") << QStringLiteral("HEAD");
    }

    void refusesInvalidNames()
    {
        QFETCH(const QString, name);
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = name, .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        const QString message = acceptCollectingMessage(dlg);
        QVERIFY2(message.contains(QStringLiteral("Branch/Tag name must not be empty or is invalid.")), qPrintable(message));
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
    }

    void trimsTheName()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("  spaced  "), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        dlg.accept();
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dlg.options().branchTagName, QStringLiteral("spaced"));
    }

    void warnsAboutASameNamedTag()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("v1"), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        const QString message = acceptCollectingMessage(dlg);
        QVERIFY2(message.contains(QStringLiteral("A tag with the same name")), qPrintable(message));
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
    }

    void continuesPastASameNamedTag()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("v1"), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        std::ignore = acceptCollectingMessage(dlg, true);
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
    }

    void switchesToTheNewBranch()
    {
        const BranchSetup setup;
        const CreateRun run = runCreate(setup.repo.path(),
                                        {.initialRefName = QStringLiteral("v1"), .name = QStringLiteral("created"), .switchNewBranch = false},
                                        [](CreateBranchTagDlg& dlg) {
                                            DlgWidgets(dlg).switchTo->setChecked(true);
                                        });
        QVERIFY2(run.created, qPrintable(run.message));
        QVERIFY(run.sawProgress);
        QCOMPARE(setup.headBranch(), QStringLiteral("created"));
        QVERIFY(rememberedSwitch());
    }

    void remembersSwitchToNewBranch()
    {
        const BranchSetup setup;
        rememberSwitch(true);
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("created"), .switchNewBranch = false});
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));
        QVERIFY(widgets.switchTo->isChecked());

        widgets.switchTo->setChecked(false);
        dlg.accept();
        QVERIFY(!dlg.options().switchToNewBranch);
        QVERIFY(!rememberedSwitch());
    }

    void cancelLeavesTheSettingAlone()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git());
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));
        widgets.switchTo->setChecked(true);
        dlg.reject();
        QVERIFY(!rememberedSwitch());
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void storesTheDescription_data()
    {
        QTest::addColumn<QString>("description");
        QTest::addColumn<QString>("expected");

        QTest::newRow("plain") << QStringLiteral("What it is for") << QStringLiteral("What it is for");
        QTest::newRow("multiple lines") << QStringLiteral("  one\r\ntwo\n") << QStringLiteral("one\ntwo");
        QTest::newRow("leading dash") << QStringLiteral("-not an option") << QStringLiteral("-not an option");
        QTest::newRow("blank") << QStringLiteral("   ") << QString();
    }

    void storesTheDescription()
    {
        QFETCH(const QString, description);
        QFETCH(const QString, expected);

        const BranchSetup setup;
        const CreateRun run = runCreate(setup.repo.path(),
                                        {.initialRefName = {}, .name = QStringLiteral("created"), .switchNewBranch = false},
                                        [&description](CreateBranchTagDlg& dlg) {
                                            DlgWidgets(dlg).message->setPlainText(description);
                                        });
        QVERIFY2(run.created, qPrintable(run.message));
        QCOMPARE(setup.repo.git().configValue(QStringLiteral("branch.created.description")), expected);
    }

    void aBlankDescriptionRemovesTheOldOne()
    {
        const BranchSetup setup;
        QVERIFY(AppUtils::updateBranchDescription(setup.repo.git(), QStringLiteral("feature"), QStringLiteral("old")));
        QCOMPARE(setup.repo.git().configValue(QStringLiteral("branch.feature.description")), QStringLiteral("old"));
        QVERIFY(AppUtils::updateBranchDescription(setup.repo.git(), QStringLiteral("feature"), QStringLiteral(" \n")));
        QVERIFY(setup.repo.git().configValue(QStringLiteral("branch.feature.description")).isEmpty());
        QVERIFY(!AppUtils::updateBranchDescription(setup.repo.git(), QString(), QStringLiteral("old")));
    }

    void bareRepositoryCannotSwitch()
    {
        const BranchSetup setup;
        const QTemporaryDir bareDir;
        QVERIFY(bareDir.isValid());
        setup.repo.run({QStringLiteral("clone"), QStringLiteral("--quiet"), QStringLiteral("--bare"), setup.repo.path(), bareDir.path()});
        rememberSwitch(true);

        CreateBranchTagDlg dlg{Git(bareDir.path())};
        const DlgWidgets widgets(dlg);
        QVERIFY(waitLoaded(dlg));
        QVERIFY(widgets.switchTo->isHidden());
        widgets.name->setText(QStringLiteral("created"));
        dlg.accept();
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
        QVERIFY(!dlg.options().switchToNewBranch);
        QVERIFY(rememberedSwitch());
    }

    void screenshots()
    {
        const BranchSetup setup;
        CreateBranchTagDlg dlg(setup.repo.git(), {.initialRefName = {}, .name = QStringLiteral("new-feature"), .switchNewBranch = false});
        QVERIFY(waitLoaded(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap pixmap = dlg.grab();
        QVERIFY(!pixmap.isNull());
        if (const QString dir = qEnvironmentVariable("KGT_SCREENSHOT_DIR"); !dir.isEmpty()) {
            QVERIFY(pixmap.save(QDir(dir).absoluteFilePath(QStringLiteral("createbranchtagdlg.png"))));
        }
    }
};

QTEST_MAIN(CreateBranchTagTest)

#include "createbranchtagtest.moc"
