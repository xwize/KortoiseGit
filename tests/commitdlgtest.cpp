// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commitdlg.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <KMessageWidget>

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace
{

using namespace kgt;

constexpr int timeout = 10000;

/**
 * Opens the dialog and waits for its file list.
 * @param dlg The dialog, just created.
 * @returns Whether the list loaded.
 */
bool waitForList(const CommitDlg& dlg)
{
    if (!dlg.fileList()->isBusy()) {
        return true;
    }
    QSignalSpy loaded(dlg.fileList(), &GitStatusListCtrl::loaded);
    return loaded.wait(timeout);
}

/**
 * @param dlg The dialog.
 * @returns The listed paths.
 */
QStringList listedPaths(const CommitDlg& dlg)
{
    const GitStatusListModel *model = dlg.fileList()->statusModel();
    QStringList paths;
    for (int row = 0; row < model->listedCount(); ++row) {
        paths.append(model->entry(row).path);
    }
    paths.sort();
    return paths;
}

/**
 * Checks or unchecks one listed file.
 * @param dlg The dialog.
 * @param path The file, relative to the root.
 * @param checked Its new state.
 * @returns Whether the file was listed.
 */
bool setChecked(const CommitDlg& dlg, const QString& path, bool checked)
{
    GitStatusListModel *model = dlg.fileList()->statusModel();
    for (int row = 0; row < model->listedCount(); ++row) {
        if (model->entry(row).path == path) {
            return model->setData(model->entryIndex(row), checked ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        }
    }
    return false;
}

/**
 * Runs git and returns its output as lines.
 * @param repo The repository.
 * @param arguments The arguments after "git".
 * @returns The non-empty lines, sorted.
 */
QStringList gitLines(const TestRepo& repo, const QStringList& arguments)
{
    QStringList lines = QString::fromUtf8(repo.git().run(arguments)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines.sort();
    return lines;
}

/**
 * @param repo The repository.
 * @returns HEAD's hash.
 */
QString headHash(const TestRepo& repo)
{
    return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})).trimmed();
}

/**
 * Leaves a repository in the middle of a merge, with f.txt in conflict.
 * @param repo An empty repository.
 */
void makeConflict(const TestRepo& repo)
{
    repo.write(QStringLiteral("f.txt"), "base\n");
    repo.commitAll(QStringLiteral("Base"));
    repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("side")});
    repo.write(QStringLiteral("f.txt"), "side\n");
    repo.commitAll(QStringLiteral("Side"));
    repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
    repo.write(QStringLiteral("f.txt"), "main\n");
    repo.commitAll(QStringLiteral("Main"));
    try {
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("side")});
    } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch): The conflict is the point
    }
}

/**
 * Clicks a button of the open message box.
 * @param text The button's text.
 */
void clickButton(const QString& text)
{
    const QWidget *box = QApplication::activeModalWidget();
    if (!box) {
        return;
    }
    for (QPushButton *button : box->findChildren<QPushButton *>()) {
        if (button->text().remove(QLatin1Char('&')) == text) {
            button->click();
            return;
        }
    }
}

/**
 * @param repo The repository.
 * @returns The files changed by the last commit.
 */
QStringList committedFiles(const TestRepo& repo)
{
    return gitLines(repo, {QStringLiteral("show"), QStringLiteral("--format="), QStringLiteral("--name-only"), QStringLiteral("HEAD")});
}

/**
 * @param repo The repository.
 * @returns The files staged against HEAD.
 */
QStringList stagedFiles(const TestRepo& repo)
{
    return gitLines(repo, {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only")});
}

/**
 * Types a message and clicks Commit.
 * @param dlg The dialog.
 * @param message The commit message.
 */
void typeAndCommit(const CommitDlg& dlg, const QString& message)
{
    dlg.messageEdit()->setPlainText(message);
    QVERIFY(dlg.commitButton()->isEnabled());
    QTest::mouseClick(dlg.commitButton(), Qt::LeftButton);
}

/**
 * Types a message, clicks Commit and waits for the dialog to close.
 * @param dlg The dialog.
 * @param message The commit message.
 * @returns Whether the commit succeeded.
 */
bool commitAndWait(const CommitDlg& dlg, const QString& message)
{
    dlg.messageEdit()->setPlainText(message);
    if (!dlg.commitButton()->isEnabled()) {
        return false;
    }
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QTest::mouseClick(dlg.commitButton(), Qt::LeftButton);
    return accepted.wait(timeout);
}

/**
 * Commits old.txt (and whatever else is in the working tree), then renames it to new.txt outside git.
 * @param repo The repository.
 */
void renameOutsideGit(const TestRepo& repo)
{
    repo.write(QStringLiteral("old.txt"), "a file that is renamed\n");
    repo.commitAll(QStringLiteral("Base"));
    QVERIFY(QFile::rename(repo.filePath(QStringLiteral("old.txt")), repo.filePath(QStringLiteral("new.txt"))));
}

/**
 * @param repo The repository.
 * @returns What HEAD changed, with renames detected: one "<status>\t<path>..." line per file.
 */
QStringList lastCommitNameStatus(const TestRepo& repo)
{
    return gitLines(repo, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("-M"), QStringLiteral("--name-status"), QStringLiteral("--format=")});
}

/**
 * A repository with a.txt and b.txt committed.
 * @param repo The repository to fill.
 */
void commitTwoFiles(const TestRepo& repo)
{
    repo.write(QStringLiteral("a.txt"), "a\n");
    repo.write(QStringLiteral("b.txt"), "b\n");
    repo.commitAll(QStringLiteral("initial"));
}

/**
 * commitTwoFiles(), then a second commit that changes a.txt and adds c.txt.
 * @param repo The repository to fill.
 */
void commitTwice(const TestRepo& repo)
{
    commitTwoFiles(repo);
    repo.write(QStringLiteral("a.txt"), "a changed\n");
    repo.write(QStringLiteral("c.txt"), "c\n");
    repo.commitAll(QStringLiteral("Last subject\n\nLast body."));
}

/**
 * @param repo The repository.
 * @param revision A commit.
 * @returns Its message, without the final newline.
 */
QString commitMessage(const TestRepo& repo, const QString& revision)
{
    return QString::fromUtf8(repo.git().run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B"), revision})).trimmed();
}

/**
 * @param repo The repository.
 * @returns How many commits HEAD has.
 */
int commitCount(const TestRepo& repo)
{
    return QString::fromUtf8(repo.git().run({QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")})).trimmed().toInt();
}

/**
 * Ticks or unticks "Amend Last Commit" as the user does, and waits for the list to reload.
 * @param dlg The dialog.
 * @returns Whether the list reloaded.
 */
bool clickAmend(const CommitDlg& dlg)
{
    if (!dlg.amendButton()->isEnabled()) {
        return false;
    }
    QSignalSpy loaded(dlg.fileList(), &GitStatusListCtrl::loaded);
    dlg.amendButton()->click();
    return loaded.wait(timeout);
}

} // namespace

/**
 * CommitDlg against real repositories: what is committed, what is left in the index, and failures.
 * Set KGT_SCREENSHOT_DIR to save a screenshot there.
 */
class CommitDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void listsChangesWithTrackedChecked()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("new.txt"), "new\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("a.txt"), QStringLiteral("new.txt")}));
        const QList<GitStatusListEntry> checked = dlg.fileList()->checkedEntries();
        QCOMPARE(checked.size(), 1);
        QCOMPARE(checked.constFirst().path, QStringLiteral("a.txt"));

        dlg.showUnversionedButton()->setChecked(false);
        QCOMPARE(listedPaths(dlg), QStringList{QStringLiteral("a.txt")});
    }

    void commitButtonNeedsMessageAndFiles()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(!dlg.commitButton()->isEnabled());
        dlg.messageEdit()->setPlainText(QStringLiteral(" \n\t"));
        QVERIFY(!dlg.commitButton()->isEnabled());
        dlg.messageEdit()->setPlainText(QStringLiteral("message"));
        QVERIFY(dlg.commitButton()->isEnabled());
        QVERIFY(setChecked(dlg, QStringLiteral("a.txt"), false));
        QVERIFY(!dlg.commitButton()->isEnabled());
    }

    void commitsCheckedSubset()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("b.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("b.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Change a\n\nOnly a.")));

        QCOMPARE(committedFiles(repo), QStringList{QStringLiteral("a.txt")});
        QCOMPARE(QString::fromUtf8(repo.git().run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")})).trimmed(),
                 QStringLiteral("Change a\n\nOnly a."));
        QCOMPARE(gitLines(repo, {QStringLiteral("diff"), QStringLiteral("--name-only")}), QStringList{QStringLiteral("b.txt")});
        QVERIFY(stagedFiles(repo).isEmpty());
    }

    void commitsAnAddedFileAndAMissingOneAsARename()
    {
        const TestRepo repo;
        renameOutsideGit(repo);
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), QStringList({QStringLiteral("new.txt"), QStringLiteral("old.txt")}));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Rename")));

        QCOMPARE(lastCommitNameStatus(repo), QStringList{QStringLiteral("R100\told.txt\tnew.txt")});
        QVERIFY(gitLines(repo, {QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty());
    }

    void commitsADetectedRename()
    {
        const TestRepo repo;
        renameOutsideGit(repo);
        repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("--cached"), QStringLiteral("old.txt")});
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), QStringList{QStringLiteral("new.txt")});
        QVERIFY(commitAndWait(dlg, QStringLiteral("Rename")));

        QCOMPARE(lastCommitNameStatus(repo), QStringList{QStringLiteral("R100\told.txt\tnew.txt")});
        QVERIFY(gitLines(repo, {QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty());
    }

    void leavesAnUncheckedRenameStaged()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        renameOutsideGit(repo);
        repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("--cached"), QStringLiteral("old.txt")});
        repo.run({QStringLiteral("add"), QStringLiteral("new.txt")});
        repo.write(QStringLiteral("a.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("new.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Only a")));

        QCOMPARE(lastCommitNameStatus(repo), QStringList{QStringLiteral("M\ta.txt")});
        QCOMPARE(gitLines(repo, {QStringLiteral("status"), QStringLiteral("--porcelain")}), QStringList{QStringLiteral("R  old.txt -> new.txt")});
    }

    void commitsCheckedUnversionedFile()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("new file.txt"), "new\n");
        repo.write(QStringLiteral("other.txt"), "other\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("new file.txt"), true));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Add a file")));

        QCOMPARE(committedFiles(repo), QStringList{QStringLiteral("new file.txt")});
        QCOMPARE(gitLines(repo, {QStringLiteral("ls-files"), QStringLiteral("--others")}), QStringList{QStringLiteral("other.txt")});
    }

    void excludesUncheckedStagedFileAndStagesItAgain()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("staged.txt"), "staged\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("staged.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Change a")));

        QCOMPARE(committedFiles(repo), QStringList{QStringLiteral("a.txt")});
        QCOMPARE(stagedFiles(repo), QStringList{QStringLiteral("staged.txt")});
    }

    void listsStagedFilesOutsideThePaths()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("dir/c.txt"), "c\n");
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("b.txt"), "changed\n");
        repo.run({QStringLiteral("add"), QStringLiteral("b.txt")});

        // Opened for dir: git commit would also take the staged b.txt, so it is listed too
        const CommitDlg dlg({repo.filePath(QStringLiteral("dir"))});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("b.txt"), QStringLiteral("dir/c.txt")}));
    }

    void failingHookKeepsDialogOpen()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("staged.txt"), "staged\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});
        const QString hook = repo.filePath(QStringLiteral(".git/hooks/pre-commit"));
        repo.write(QStringLiteral(".git/hooks/pre-commit"), "#!/bin/sh\necho 'hook says no' >&2\nexit 1\n");
        QVERIFY(QFile::setPermissions(hook, QFile::permissions(hook) | QFile::ExeOwner));
        const QString head = QString::fromUtf8(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}));

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("staged.txt"), false));
        const QSignalSpy accepted(&dlg, &QDialog::accepted);
        QSignalSpy failed(&dlg, &CommitDlg::commitFailed);
        typeAndCommit(dlg, QStringLiteral("Rejected"));
        QVERIFY(failed.wait(timeout));

        QVERIFY(accepted.isEmpty());
        QVERIFY(!dlg.isCommitting());
        QCOMPARE(dlg.messageEdit()->toPlainText(), QStringLiteral("Rejected"));
        const auto *errorView = dlg.findChild<KMessageWidget *>();
        QVERIFY(errorView && !errorView->isHidden());
        QVERIFY2(errorView->text().contains(QStringLiteral("hook says no")), qPrintable(errorView->text()));
        QCOMPARE(QString::fromUtf8(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})), head);
        // The unchecked file that was added is staged again
        QVERIFY(stagedFiles(repo).contains(QStringLiteral("staged.txt")));
        QVERIFY(waitForList(dlg));
        QVERIFY(dlg.commitButton()->isEnabled());
    }

    void f5RefreshesFromTheMessageEditor()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));
        QVERIFY(dlg.messageEdit()->hasFocus());
        const auto *commitTo = dlg.findChild<QLineEdit *>();
        QVERIFY(commitTo);
        QCOMPARE(commitTo->text(), QStringLiteral("main"));

        repo.write(QStringLiteral("b.txt"), "changed\n");
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("other")});
        QSignalSpy loaded(dlg.fileList(), &GitStatusListCtrl::loaded);
        QTest::keyClick(dlg.messageEdit(), Qt::Key_F5);
        QVERIFY(loaded.wait(timeout));

        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
        QCOMPARE(commitTo->text(), QStringLiteral("other"));
    }

    void f5IsIgnoredWhileCommitting()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\n");
        CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));

        QSignalSpy accepted(&dlg, &QDialog::accepted);
        typeAndCommit(dlg, QStringLiteral("Committed"));
        QVERIFY(dlg.isCommitting());
        QTest::keyClick(dlg.messageEdit(), Qt::Key_F5);
        QVERIFY(!dlg.fileList()->isBusy());
        QVERIFY(accepted.wait(timeout));
    }

    void failedCommitLeavesCheckedFilesStaged()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        const QByteArray original = repo.git().run({QStringLiteral("show"), QStringLiteral("HEAD:a.txt")});
        repo.write(QStringLiteral("a.txt"), "changed\n");
        const QString hook = repo.filePath(QStringLiteral(".git/hooks/pre-commit"));
        repo.write(QStringLiteral(".git/hooks/pre-commit"), "#!/bin/sh\nexit 1\n");
        QVERIFY(QFile::setPermissions(hook, QFile::permissions(hook) | QFile::ExeOwner));

        CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));
        QSignalSpy failed(&dlg, &CommitDlg::commitFailed);
        typeAndCommit(dlg, QStringLiteral("Rejected"));
        QVERIFY(failed.wait(timeout));
        QVERIFY(waitForList(dlg));

        // CCommitDlg::OnOK doesn't unstage what it staged, so the file is still a change once the edit is undone
        QCOMPARE(stagedFiles(repo), QStringList{QStringLiteral("a.txt")});
        repo.write(QStringLiteral("a.txt"), original);
        QSignalSpy loaded(dlg.fileList(), &GitStatusListCtrl::loaded);
        QTest::keyClick(dlg.messageEdit(), Qt::Key_F5);
        QVERIFY(loaded.wait(timeout));
        QVERIFY(listedPaths(dlg).contains(QStringLiteral("a.txt")));
    }

    void failedPreparationDoesNotStageUncheckedFilesAgain()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("staged.txt"), "staged\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});
        repo.write(QStringLiteral("staged.txt"), "edited\n");
        repo.write(QStringLiteral("gone.txt"), "gone\n");
        const QString head = headHash(repo);

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("staged.txt"), false));
        QVERIFY(setChecked(dlg, QStringLiteral("gone.txt"), true));
        // git add then fails, before anything else has touched the index
        QVERIFY(QFile::remove(repo.filePath(QStringLiteral("gone.txt"))));
        QSignalSpy failed(&dlg, &CommitDlg::commitFailed);
        typeAndCommit(dlg, QStringLiteral("Never committed"));
        QVERIFY(failed.wait(timeout));

        QCOMPARE(headHash(repo), head);
        // bAddSuccess is false, so mgtReAddAfterCommit doesn't run and the edit isn't staged
        QCOMPARE(repo.git().run({QStringLiteral("show"), QStringLiteral(":staged.txt")}), QByteArray("staged\n"));
    }

    void commitsInRepositoryWithoutCommits()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.write(QStringLiteral("b.txt"), "b\n");
        repo.run({QStringLiteral("add"), QStringLiteral("a.txt"), QStringLiteral("b.txt")});

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("b.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("First")));

        QCOMPARE(committedFiles(repo), QStringList{QStringLiteral("a.txt")});
        QCOMPARE(stagedFiles(repo), QStringList{QStringLiteral("b.txt")});
    }

    void amendIsOfferedOnlyWithHead()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        const CommitDlg initial({repo.path()});
        QVERIFY(waitForList(initial));
        QVERIFY(!initial.amendButton()->isEnabled());
        QCOMPARE(initial.amendButton()->text(), QStringLiteral("Amend &Last Commit"));
        QVERIFY(initial.amendButton()->toolTip().startsWith(QStringLiteral("Use this to modify/replace the latest checked out commit")));

        repo.commitAll(QStringLiteral("first"));
        const CommitDlg withHead({repo.path()});
        QVERIFY(waitForList(withHead));
        QVERIFY(withHead.amendButton()->isEnabled());
    }

    void amendIsNotOfferedDuringCherryPick()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral(".git/CHERRY_PICK_HEAD"), repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}));

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(!dlg.amendButton()->isEnabled());
    }

    void amendSwapsTheMessage()
    {
        const TestRepo repo;
        commitTwice(repo);
        repo.write(QStringLiteral("b.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.messageEdit()->setPlainText(QStringLiteral("typed"));
        QVERIFY(clickAmend(dlg));
        QCOMPARE(dlg.messageEdit()->toPlainText(), QStringLiteral("Last subject\n\nLast body."));
        dlg.messageEdit()->setPlainText(QStringLiteral("Last subject, edited"));
        QVERIFY(clickAmend(dlg));
        QCOMPARE(dlg.messageEdit()->toPlainText(), QStringLiteral("typed"));
        // The edited amend message is kept for the next tick
        QVERIFY(clickAmend(dlg));
        QCOMPARE(dlg.messageEdit()->toPlainText(), QStringLiteral("Last subject, edited"));
    }

    void amendListsTheLastCommitsFiles()
    {
        const TestRepo repo;
        commitTwice(repo);
        repo.write(QStringLiteral("b.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), QStringList{QStringLiteral("b.txt")});
        QVERIFY(clickAmend(dlg));
        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
        QCOMPARE(dlg.fileList()->checkedEntries().size(), 3);
        QVERIFY(clickAmend(dlg));
        QCOMPARE(listedPaths(dlg), QStringList{QStringLiteral("b.txt")});
    }

    void amendsOnlyTheMessage()
    {
        const TestRepo repo;
        commitTwice(repo);
        repo.write(QStringLiteral("b.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(clickAmend(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("b.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("New message")));

        QVERIFY(!dlg.amendButton()->isChecked());
        QCOMPARE(commitCount(repo), 2);
        QCOMPARE(commitMessage(repo, QStringLiteral("HEAD")), QStringLiteral("New message"));
        QCOMPARE(committedFiles(repo), (QStringList{QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
        QCOMPARE(gitLines(repo, {QStringLiteral("diff"), QStringLiteral("--name-only")}), QStringList{QStringLiteral("b.txt")});
        QVERIFY(stagedFiles(repo).isEmpty());
    }

    void amendsWithAnExtraFile()
    {
        const TestRepo repo;
        commitTwice(repo);
        repo.write(QStringLiteral("b.txt"), "changed\n");

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(clickAmend(dlg));
        QVERIFY(commitAndWait(dlg, dlg.messageEdit()->toPlainText()));

        QCOMPARE(commitCount(repo), 2);
        QCOMPARE(commitMessage(repo, QStringLiteral("HEAD")), QStringLiteral("Last subject\n\nLast body."));
        QCOMPARE(committedFiles(repo), (QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
        QVERIFY(gitLines(repo, {QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty());
    }

    void amendLeavesUncheckedFilesOutOfTheCommit()
    {
        const TestRepo repo;
        commitTwice(repo);

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(clickAmend(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("a.txt"), false));
        QVERIFY(commitAndWait(dlg, QStringLiteral("Only c")));

        QCOMPARE(committedFiles(repo), QStringList{QStringLiteral("c.txt")});
        // a.txt's change from the old commit stays in the working tree
        QCOMPARE(gitLines(repo, {QStringLiteral("diff"), QStringLiteral("--name-only")}), QStringList{QStringLiteral("a.txt")});
    }

    void amendsRootCommitWithoutFiles()
    {
        const TestRepo repo;
        commitTwoFiles(repo);

        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(clickAmend(dlg));
        // A root commit is compared with itself, so nothing is listed, but the message alone can be amended
        QVERIFY(listedPaths(dlg).isEmpty());
        QVERIFY(commitAndWait(dlg, QStringLiteral("Reworded")));
        QCOMPARE(commitCount(repo), 1);
        QCOMPARE(commitMessage(repo, QStringLiteral("HEAD")), QStringLiteral("Reworded"));
    }

    void screenshotAmend()
    {
        const TestRepo repo;
        commitTwice(repo);
        repo.write(QStringLiteral("b.txt"), "changed\n");

        CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QVERIFY(clickAmend(dlg));

        const QPixmap shot = dlg.grab();
        QVERIFY(!shot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(shot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("commitdlg-amend.png"))));
        }
    }

    void screenshot()
    {
        const TestRepo repo;
        commitTwoFiles(repo);
        repo.write(QStringLiteral("a.txt"), "changed\nmore\n");
        repo.write(QStringLiteral("src/main.cpp"), "int main() {}\n");
        repo.run({QStringLiteral("add"), QStringLiteral("src/main.cpp")});
        repo.remove(QStringLiteral("b.txt"));
        repo.write(QStringLiteral("notes.md"), "notes\n");

        CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        dlg.messageEdit()->setPlainText(QStringLiteral("Add the entry point\n\nAnd drop b.txt."));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        const QPixmap shot = dlg.grab();
        QVERIFY(!shot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(shot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("commitdlg.png"))));
        }
    }

    void warnsBeforeCommittingConflicts()
    {
        const TestRepo repo;
        makeConflict(repo);
        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        const QString head = headHash(repo);
        dlg.messageEdit()->setPlainText(QStringLiteral("Merge"));
        QVERIFY(dlg.commitButton()->isEnabled());

        // OK goes back to the list with the conflicted file selected
        QTimer::singleShot(0, [] {
            clickButton(QStringLiteral("OK"));
        });
        dlg.commitButton()->click();
        QVERIFY(!dlg.isCommitting());
        QCOMPARE(headHash(repo), head);
        const QModelIndexList selected = dlg.fileList()->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.constFirst().data(GitStatusListModel::PathRole).toString(), QStringLiteral("f.txt"));
    }

    void ignoringTheWarningCommits()
    {
        const TestRepo repo;
        makeConflict(repo);
        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        const QString head = headHash(repo);
        dlg.messageEdit()->setPlainText(QStringLiteral("Merge"));
        QTimer::singleShot(0, [] {
            clickButton(QStringLiteral("Ignore"));
        });
        dlg.commitButton()->click();
        QVERIFY(QTest::qWaitFor(
            [&repo, &head] {
                return headHash(repo) != head;
            },
            timeout));
    }

    void pathsGoToGitOnStdin()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("deleted.txt"), "1");
        repo.write(QStringLiteral("changed.txt"), "1");
        repo.commitAll(QStringLiteral("One"));
        repo.remove(QStringLiteral("deleted.txt"));
        repo.write(QStringLiteral("changed.txt"), "2");
        repo.write(QStringLiteral("odd*.txt"), "3");
        const CommitDlg dlg({repo.path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("odd*.txt"), true));
        QVERIFY(dlg.fileList()->statusModel()->hasGroups());

        // However long the list, none of the paths are on a command line
        const CommitPlan plan = CommitDlg::planCommit(*dlg.fileList()->statusModel());
        QVERIFY(plan.prepareIndex.size() >= 3);
        for (const CommitStep& step : plan.prepareIndex) {
            QVERIFY(!step.input.isEmpty());
            QVERIFY(!step.arguments.join(QLatin1Char(' ')).contains(QLatin1String(".txt")));
        }
        QVERIFY(commitAndWait(dlg, QStringLiteral("Paths")));
        QCOMPARE(committedFiles(repo), (QStringList{QStringLiteral("changed.txt"), QStringLiteral("deleted.txt"), QStringLiteral("odd*.txt")}));
    }
};

QTEST_MAIN(CommitDlgTest)

#include "commitdlgtest.moc"
