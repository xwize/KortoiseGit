// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commitdlg.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "testrepo.hpp"

#include <KMessageWidget>

#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

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
    for (int row = 0; row < model->rowCount(); ++row) {
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
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->entry(row).path == path) {
            return model->setData(model->index(row, GitStatusListModel::PathColumn), checked ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
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
 * A repository with a.txt and b.txt committed.
 * @param repo The repository to fill.
 */
void commitTwoFiles(const TestRepo& repo)
{
    repo.write(QStringLiteral("a.txt"), "a\n");
    repo.write(QStringLiteral("b.txt"), "b\n");
    repo.commitAll(QStringLiteral("initial"));
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
};

QTEST_MAIN(CommitDlgTest)

#include "commitdlgtest.moc"
