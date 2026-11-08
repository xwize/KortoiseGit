// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/command.hpp"
#include "git/gitrevloglist.hpp"
#include "gitloglist.hpp"
#include "gitloglistmodel.hpp"
#include "gitstatuslistctrl.hpp"
#include "rebasedlg.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDir>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include <memory>

namespace
{

using namespace kgt;

constexpr int timeout = 30000;

/**
 * Answers every message box that opens: a question with the given button, anything else with its escape button.
 * The texts are kept, so a test can check that nothing unexpected was asked.
 */
class BoxAnswerer : public QObject
{
public:
    /**
     * @param answer The button for questions.
     */
    explicit BoxAnswerer(QMessageBox::StandardButton answer = QMessageBox::Yes)
        : m_answer(answer)
    {
        connect(&m_timer, &QTimer::timeout, this, [this] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) {
                return;
            }
            m_texts.append(box->text());
            if (QAbstractButton *button = box->button(m_answer)) {
                button->click();
            } else if (QAbstractButton *escape = box->escapeButton()) {
                escape->click();
            } else {
                box->reject();
            }
        });
        m_timer.start(10);
    }

    /**
     * @returns The texts of the boxes answered.
     */
    [[nodiscard]] const QStringList& texts() const
    {
        return m_texts;
    }

private:
    QTimer m_timer;
    QMessageBox::StandardButton m_answer;
    QStringList m_texts;
};

/**
 * A repository with main (a.txt, then "main work" adding m.txt) and feature, which branched off main's first
 * commit and adds f1.txt, f2.txt and f3.txt in commits c1, c2 and c3. feature is checked out.
 */
struct RebaseSetup {
    TestRepo repo;

    RebaseSetup()
    {
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("branch"), QStringLiteral("old")});
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("feature")});
        for (const int number : {1, 2, 3}) {
            repo.write(QStringLiteral("f%1.txt").arg(number), QStringLiteral("c%1\n").arg(number).toUtf8());
            repo.commitAll(QStringLiteral("c%1").arg(number));
        }
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(QStringLiteral("m.txt"), "m\n");
        repo.commitAll(QStringLiteral("main work"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("feature")});
    }

    /**
     * @param ref A ref.
     * @returns Its commit, or an empty string.
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
     * @param ref A ref.
     * @returns The subjects of its history, newest first.
     */
    [[nodiscard]] QStringList subjects(const QString& ref = QStringLiteral("HEAD")) const
    {
        const QString log = QString::fromUtf8(repo.git().run({QStringLiteral("log"), QStringLiteral("--format=%s"), ref}));
        return log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    /**
     * @returns The branch HEAD points to, or empty when detached.
     */
    [[nodiscard]] QString headBranch() const
    {
        try {
            return QString::fromUtf8(repo.git().run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD")}))
                .trimmed();
        } catch (const RuntimeError&) {
            return {};
        }
    }

    /**
     * @returns git status --porcelain: empty when the working tree is clean.
     */
    [[nodiscard]] QString status() const
    {
        return QString::fromUtf8(repo.git().run({QStringLiteral("status"), QStringLiteral("--porcelain")}));
    }
};

/**
 * Opens the dialog and waits for its commit list.
 * @param repo The repository.
 * @param upstream The upstream to preselect.
 * @returns The dialog.
 */
std::unique_ptr<RebaseDlg> openDlg(const TestRepo& repo, const QString& upstream = QStringLiteral("main"))
{
    auto dlg = std::make_unique<RebaseDlg>(repo.path(), RebaseOptions{.upstream = upstream, .force = false});
    QSignalSpy fetched(dlg.get(), &RebaseDlg::logListFetched);
    if (!fetched.wait(timeout)) {
        return nullptr;
    }
    return dlg;
}

/**
 * Presses Start/Continue/Commit/Amend and waits for the worker to stop.
 * @param dlg The dialog.
 * @returns Whether it stopped in time.
 */
bool pressContinue(const RebaseDlg& dlg)
{
    QSignalSpy finished(&dlg, &RebaseDlg::threadFinished);
    dlg.continueButton()->click();
    return finished.wait(timeout);
}

/**
 * @param dlg The dialog.
 * @returns The rebase actions of the listed commits, newest first, by name.
 */
QStringList actionNames(const RebaseDlg& dlg)
{
    QStringList names;
    const GitLogListModel *model = dlg.commitList()->logModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        names.append(GitLogListModel::rebaseActionName(model->rev(row).rebaseAction & GitRevLoglist::RebaseModeMask));
    }
    return names;
}

/**
 * Selects one row of the commit list.
 * @param dlg The dialog.
 * @param row The row.
 */
void selectRow(const RebaseDlg& dlg, int row)
{
    const GitLogList *list = dlg.commitList();
    list->selectionModel()->select(list->model()->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

/**
 * Waits for a file list to finish loading.
 * @param files The list.
 * @returns Whether it finished in time.
 */
bool waitIdle(const GitStatusListCtrl *files)
{
    const QDeadlineTimer deadline(timeout);
    while (files->isBusy() && !deadline.hasExpired()) {
        QTest::qWait(10);
    }
    return !files->isBusy();
}

/**
 * Saves a screenshot of the dialog if KGT_SCREENSHOT_DIR is set.
 * @param dlg The dialog.
 * @param name The file name.
 */
void saveScreenshot(RebaseDlg& dlg, const QString& name)
{
    dlg.show();
    QCoreApplication::processEvents();
    const QPixmap screenshot = dlg.grab();
    QVERIFY(!screenshot.isNull());
    if (const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR"); !screenshotDir.isEmpty()) {
        QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(name)));
    }
}

} // namespace

class RebaseTest : public QObject
{
    Q_OBJECT

    /**
     * Writes a resolution of a.txt and marks it resolved in the conflict list.
     * @param setup The repository.
     * @param files The conflict list, with a.txt in its first row.
     */
    static void resolve(const RebaseSetup& setup, GitStatusListCtrl *files)
    {
        setup.repo.write(QStringLiteral("a.txt"), "resolved\n");
        files->selectionModel()->select(files->model()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const BoxAnswerer answerer(QMessageBox::Yes);
        QSignalSpy resolved(files, &GitStatusListCtrl::resolved);
        files->resolveSelected();
        QVERIFY(resolved.wait(timeout));
        QCOMPARE(answerer.texts(), QStringList{QStringLiteral("Are you sure you want to mark the conflicted file(s) as resolved?")});
        QVERIFY(waitIdle(files));
        QVERIFY(!(files->statusModel()->entry(0).actions & TGitPath::Action::Unmerged));
    }

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void commandIsRegistered()
    {
        const Result<std::unique_ptr<Command>> command = CommandServer::command(QStringLiteral("rebase"));
        QVERIFY(command.has_value());
        QCOMPARE((*command)->pathRequirement(), PathRequirement::WorkingTreeRequired);
    }

    void f5RefreshesWhereverTheFocusIs()
    {
        const RebaseSetup setup;
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        dlg->show();
        QVERIFY(QTest::qWaitForWindowActive(dlg.get()));
        dlg->upstreamCombo()->setFocus();
        QCOMPARE(dlg->branchCombo()->findText(QStringLiteral("later")), -1);

        setup.repo.run({QStringLiteral("branch"), QStringLiteral("later")});
        QTest::keyClick(dlg->upstreamCombo(), Qt::Key_F5);
        QVERIFY(QTest::qWaitFor(
            [&dlg] {
                return dlg->branchCombo()->findText(QStringLiteral("later")) >= 0;
            },
            timeout));
    }

    void setupDefaults()
    {
        const RebaseSetup setup;
        setup.repo.run({QStringLiteral("config"), QStringLiteral("branch.feature.remote"), QStringLiteral("origin")});
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo, {});
        QVERIFY(dlg);
        // The current branch; no remote-tracking upstream exists, so none is selected
        QCOMPARE(dlg->branchCombo()->currentText(), QStringLiteral("feature"));
        QCOMPARE(dlg->upstreamCombo()->currentIndex(), -1);
        QVERIFY(!dlg->continueButton()->isEnabled());

        const std::unique_ptr<RebaseDlg> withUpstream = openDlg(setup.repo);
        QVERIFY(withUpstream);
        QCOMPARE(withUpstream->upstreamCombo()->currentText(), QStringLiteral("main"));
        QCOMPARE(withUpstream->continueButton()->text(), QStringLiteral("Start Rebase"));
        QVERIFY(withUpstream->continueButton()->isEnabled());
        QCOMPARE(actionNames(*withUpstream), (QStringList{QStringLiteral("Pick"), QStringLiteral("Pick"), QStringLiteral("Pick")}));
        const GitLogListModel *model = withUpstream->commitList()->logModel();
        QCOMPARE(model->headerData(GitLogListModel::GraphColumn, Qt::Horizontal).toString(), QStringLiteral("REBASE"));
        QCOMPARE(model->headerData(GitLogListModel::ActionsColumn, Qt::Horizontal).toString(), QStringLiteral("ID"));
        // Numbered from the oldest
        QCOMPARE(model->data(model->index(0, GitLogListModel::ActionsColumn)).toString(), QStringLiteral("3"));
        QCOMPARE(model->rev(0).subject, QStringLiteral("c3"));
        QCOMPARE(withUpstream->tabs()->tabText(RebaseDlg::ConflictTab), QStringLiteral("Revision Files"));
        saveScreenshot(*withUpstream, QStringLiteral("rebasedlg-setup.png"));
    }

    void plainRebase()
    {
        const RebaseSetup setup;
        const QString mainHead = setup.rev(QStringLiteral("main"));
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Done"));
        QCOMPARE(dlg->statusLabel()->text(), QStringLiteral("Finished rebasing."));
        QCOMPARE(setup.subjects(),
                 (QStringList{QStringLiteral("c3"), QStringLiteral("c2"), QStringLiteral("c1"), QStringLiteral("main work"), QStringLiteral("initial")}));
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QCOMPARE(setup.rev(QStringLiteral("feature~3")), mainHead);
        QVERIFY(setup.status().isEmpty());
        QVERIFY(dlg->logOutput()->toPlainText().contains(QLatin1String("Pick 1: ")));
        saveScreenshot(*dlg, QStringLiteral("rebasedlg-done.png"));

        // Done closes the dialog
        dlg->continueButton()->click();
        QCOMPARE(dlg->result(), int(QDialog::Accepted));
    }

    void reorder()
    {
        const RebaseSetup setup;
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        // c3 below c2, then c1 to the top
        selectRow(*dlg, 0);
        dlg->moveSelectedDown();
        QCOMPARE(dlg->commitList()->logModel()->rev(1).subject, QStringLiteral("c3"));
        selectRow(*dlg, 2);
        dlg->moveSelectedUp(true);
        QCOMPARE(dlg->commitList()->logModel()->rev(0).subject, QStringLiteral("c1"));
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(setup.subjects(),
                 (QStringList{QStringLiteral("c1"), QStringLiteral("c2"), QStringLiteral("c3"), QStringLiteral("main work"), QStringLiteral("initial")}));
    }

    void squash()
    {
        const RebaseSetup setup;
        setup.repo.run(
            {QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("--amend"), QStringLiteral("-m"), QStringLiteral("c3\n\nbody of c3")});
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        // c3 squashed into c2; the oldest commit can't be squashed
        selectRow(*dlg, 2);
        dlg->commitList()->setSelectedRebaseAction(GitRevLoglist::RebaseSquash);
        QCOMPARE(actionNames(*dlg).at(2), QStringLiteral("Pick"));
        selectRow(*dlg, 0);
        dlg->commitList()->setSelectedRebaseAction(GitRevLoglist::RebaseSquash);
        QCOMPARE(actionNames(*dlg), (QStringList{QStringLiteral("Squash"), QStringLiteral("Pick"), QStringLiteral("Pick")}));

        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::SquashEdit);
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Commit"));
        QCOMPARE(dlg->tabs()->currentIndex(), int(RebaseDlg::MessageTab));
        QCOMPARE(dlg->messageEdit()->toPlainText(), QStringLiteral("c2\n\n\nc3\nbody of c3"));
        QVERIFY(!dlg->messageEdit()->isReadOnly());

        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(setup.subjects(), (QStringList{QStringLiteral("c2"), QStringLiteral("c1"), QStringLiteral("main work"), QStringLiteral("initial")}));
        const QString message = QString::fromUtf8(setup.repo.git().run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")})).trimmed();
        QCOMPARE(message, QStringLiteral("c2\n\nc3\nbody of c3"));
        QVERIFY(QFile::exists(setup.repo.filePath(QStringLiteral("f2.txt"))));
        QVERIFY(QFile::exists(setup.repo.filePath(QStringLiteral("f3.txt"))));
        QVERIFY(setup.status().isEmpty());
    }

    void skip()
    {
        const RebaseSetup setup;
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        selectRow(*dlg, 1);
        dlg->commitList()->setSelectedRebaseAction(GitRevLoglist::RebaseSkip);
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(setup.subjects(), (QStringList{QStringLiteral("c3"), QStringLiteral("c1"), QStringLiteral("main work"), QStringLiteral("initial")}));
        QVERIFY(!QFile::exists(setup.repo.filePath(QStringLiteral("f2.txt"))));
    }

    void edit()
    {
        const RebaseSetup setup;
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        selectRow(*dlg, 1);
        dlg->commitList()->setSelectedRebaseAction(GitRevLoglist::RebaseEdit);
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Edit);
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Amend"));
        QCOMPARE(dlg->messageEdit()->toPlainText(), QStringLiteral("c2\n"));
        dlg->messageEdit()->setPlainText(QStringLiteral("c2 amended"));

        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(
            setup.subjects(),
            (QStringList{QStringLiteral("c3"), QStringLiteral("c2 amended"), QStringLiteral("c1"), QStringLiteral("main work"), QStringLiteral("initial")}));
    }

    void conflict()
    {
        const RebaseSetup setup;
        setup.repo.write(QStringLiteral("a.txt"), "feature\n");
        setup.repo.commitAll(QStringLiteral("c4"));
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        setup.repo.write(QStringLiteral("a.txt"), "main\n");
        setup.repo.commitAll(QStringLiteral("main change"));
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("feature")});

        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Conflict);
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Commit"));
        QCOMPARE(dlg->tabs()->tabText(RebaseDlg::ConflictTab), QStringLiteral("Conflict Files"));
        QCOMPARE(dlg->tabs()->currentIndex(), int(RebaseDlg::ConflictTab));
        GitStatusListCtrl *files = dlg->fileList();
        QVERIFY(waitIdle(files));
        QCOMPARE(files->model()->rowCount(), 1);
        QCOMPARE(files->statusModel()->entry(0).path, QStringLiteral("a.txt"));
        QVERIFY(files->statusModel()->entry(0).actions & TGitPath::Action::Unmerged);
        // The lock IsTGitRebaseActive looks for
        QVERIFY(QDir(setup.repo.filePath(QStringLiteral(".git/tgitrebase.active"))).exists());
        saveScreenshot(*dlg, QStringLiteral("rebasedlg-conflict.png"));

        // Continuing with the conflict unresolved is refused
        {
            const BoxAnswerer answerer(QMessageBox::Ok);
            QVERIFY(pressContinue(*dlg));
            QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Conflict);
            QCOMPARE(answerer.texts(), QStringList{QStringLiteral("One or more files are in a conflicted state.")});
        }

        resolve(setup, files);

        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(setup.subjects().constFirst(), QStringLiteral("c4"));
        QCOMPARE(setup.subjects().at(4), QStringLiteral("main change"));
        QCOMPARE(QString::fromUtf8(setup.repo.git().run({QStringLiteral("show"), QStringLiteral("HEAD:a.txt")})), QStringLiteral("resolved\n"));
        QVERIFY(setup.status().isEmpty());
        QVERIFY(!QDir(setup.repo.filePath(QStringLiteral(".git/tgitrebase.active"))).exists());
    }

    void abort()
    {
        const RebaseSetup setup;
        const QString featureHead = setup.rev(QStringLiteral("feature"));
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        selectRow(*dlg, 1);
        dlg->commitList()->setSelectedRebaseAction(GitRevLoglist::RebaseEdit);
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Edit);
        QVERIFY(setup.headBranch().isEmpty());

        const BoxAnswerer answerer(QMessageBox::Yes);
        QSignalSpy finished(dlg.get(), &QDialog::finished);
        dlg->abortButton()->click();
        QVERIFY(finished.wait(timeout));
        QCOMPARE(dlg->result(), int(QDialog::Rejected));
        QCOMPARE(answerer.texts(), QStringList{QStringLiteral("Are you sure you want to abort the rebase process?")});
        QCOMPARE(setup.rev(QStringLiteral("feature")), featureHead);
        QCOMPARE(setup.rev(QStringLiteral("HEAD")), featureHead);
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QVERIFY(setup.status().isEmpty());
    }

    void upToDate()
    {
        const RebaseSetup setup;
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo, QStringLiteral("old"));
        QVERIFY(dlg);
        QCOMPARE(dlg->commitList()->logModel()->rowCount(), 0);
        QVERIFY(!dlg->continueButton()->isEnabled());

        // Force Rebase lists the commits anyway
        QSignalSpy fetched(dlg.get(), &RebaseDlg::logListFetched);
        dlg->forceCheck()->setChecked(true);
        QVERIFY(fetched.wait(timeout));
        QCOMPARE(dlg->commitList()->logModel()->rowCount(), 3);
        QVERIFY(dlg->continueButton()->isEnabled());
    }

    void fastForward()
    {
        const RebaseSetup setup;
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("old")});
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        QCOMPARE(dlg->branchCombo()->currentText(), QStringLiteral("old"));
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Start (FastFwd)"));
        QVERIFY(dlg->continueButton()->isEnabled());
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Done);
        QCOMPARE(setup.rev(QStringLiteral("old")), setup.rev(QStringLiteral("main")));
        QCOMPARE(setup.headBranch(), QStringLiteral("old"));
    }

    void equal()
    {
        const RebaseSetup setup;
        setup.repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo, QStringLiteral("main"));
        QVERIFY(dlg);
        QCOMPARE(dlg->commitList()->logModel()->rowCount(), 0);
        QVERIFY(!dlg->continueButton()->isEnabled());
    }

    void aFailedFinishIsNotDone()
    {
        const RebaseSetup setup;
        const QString oldFeature = setup.rev(QStringLiteral("feature"));
        // The rebase runs on a detached HEAD; only moving the branch at the end needs this ref
        QFile lock(setup.repo.filePath(QStringLiteral(".git/refs/heads/feature.lock")));
        QVERIFY(lock.open(QIODevice::WriteOnly));
        lock.close();
        const std::unique_ptr<RebaseDlg> dlg = openDlg(setup.repo);
        QVERIFY(dlg);
        {
            // Picks Abort when asked whether to retry moving the branch
            const BoxAnswerer answerer;
            QVERIFY(pressContinue(*dlg));
            QCOMPARE(answerer.texts().size(), 1);
        }
        QCOMPARE(dlg->stage(), RebaseDlg::RebaseStage::Finish);
        QCOMPARE(dlg->continueButton()->text(), QStringLiteral("Finish"));
        QCOMPARE(setup.rev(QStringLiteral("feature")), oldFeature);

        // Finish tries again and closes the dialog
        QVERIFY(lock.remove());
        QVERIFY(pressContinue(*dlg));
        QCOMPARE(dlg->result(), int(QDialog::Accepted));
        QCOMPARE(setup.headBranch(), QStringLiteral("feature"));
        QVERIFY(setup.rev(QStringLiteral("feature")) != oldFeature);
    }
};

QTEST_MAIN(RebaseTest)

#include "rebasetest.moc"
