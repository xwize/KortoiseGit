// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "adddlg.hpp"
#include "commands/addcommand.hpp"
#include "gitstatuslistctrl.hpp"
#include "testrepo.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

namespace
{

using namespace kgt;

/**
 * @param repo A repository with at least one commit.
 * @returns The paths staged in its index, sorted.
 */
QStringList stagedPaths(const TestRepo& repo)
{
    QStringList paths;
    const QList<QByteArray> lines =
        repo.git().run({QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only"), QStringLiteral("-z")}).split('\0');
    for (const QByteArray& line : lines) {
        if (!line.isEmpty()) {
            paths.append(QString::fromUtf8(line));
        }
    }
    paths.sort();
    return paths;
}

/**
 * @param dlg The dialog.
 * @returns The paths it lists, sorted.
 */
QStringList listedPaths(const AddDlg& dlg)
{
    const GitStatusListModel *model = dlg.listCtrl()->statusModel();
    QStringList paths;
    for (int row = 0; row < model->listedCount(); ++row) {
        paths.append(model->entry(row).path);
    }
    paths.sort();
    return paths;
}

/**
 * @param dlg The dialog.
 * @returns The paths it has checked, sorted.
 */
QStringList checkedPaths(const AddDlg& dlg)
{
    QStringList paths;
    for (const GitStatusListEntry& entry : dlg.listCtrl()->checkedEntries()) {
        paths.append(entry.path);
    }
    paths.sort();
    return paths;
}

/**
 * Checks or unchecks one listed file, as a click on its checkbox does.
 * @param dlg The dialog.
 * @param path The file, relative to the root.
 * @param state The new state.
 * @returns Whether the file is listed.
 */
bool setChecked(const AddDlg& dlg, const QString& path, Qt::CheckState state)
{
    GitStatusListModel *model = dlg.listCtrl()->statusModel();
    for (int row = 0; row < model->listedCount(); ++row) {
        if (model->entry(row).path == path) {
            return model->setData(model->entryIndex(row), state, Qt::CheckStateRole);
        }
    }
    return false;
}

/**
 * @param dlg The dialog.
 * @returns Its OK button.
 */
QPushButton *okButton(const AddDlg& dlg)
{
    const auto *buttons = dlg.findChild<QDialogButtonBox *>();
    Q_ASSERT(buttons);
    return buttons->button(QDialogButtonBox::Ok);
}

/**
 * @param dlg The dialog.
 * @returns Its "Include ignored files" checkbox.
 */
QCheckBox *includeIgnored(const AddDlg& dlg)
{
    const QList<QCheckBox *> boxes = dlg.findChildren<QCheckBox *>();
    const auto found = std::ranges::find_if(boxes, [](const QCheckBox *box) {
        return box->text() == QStringLiteral("Include &ignored files");
    });
    Q_ASSERT(found != boxes.cend());
    return *found;
}

/**
 * Creates a repository with a committed .gitignore (*.log) and tracked.txt, then a modified tracked.txt, the
 * unversioned a.txt, sub/b.txt and sub/c.txt, and the ignored x.log.
 * @returns The repository.
 */
std::unique_ptr<TestRepo> makeRepo()
{
    auto repo = std::make_unique<TestRepo>();
    repo->write(QStringLiteral(".gitignore"), "*.log\n");
    repo->write(QStringLiteral("tracked.txt"), "a\n");
    repo->commitAll(QStringLiteral("initial"));
    repo->write(QStringLiteral("tracked.txt"), "changed\n");
    repo->write(QStringLiteral("a.txt"), "a\n");
    repo->write(QStringLiteral("sub/b.txt"), "b\n");
    repo->write(QStringLiteral("sub/c.txt"), "c\n");
    repo->write(QStringLiteral("x.log"), "log\n");
    return repo;
}

/**
 * @param dlg A dialog that has just been created or refreshed.
 * @returns Whether its list finished loading.
 */
bool waitForList(const AddDlg& dlg)
{
    if (!dlg.listCtrl()->isBusy()) {
        return true;
    }
    QSignalSpy loaded(dlg.listCtrl(), &GitStatusListCtrl::loaded);
    return loaded.wait(10000);
}

} // namespace

/**
 * The Add dialog and command against real repositories. Set KGT_SCREENSHOT_DIR to save a screenshot there.
 */
class AddDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void computesPaths()
    {
        QCOMPARE(AddDlg::commonRoot({QStringLiteral("/a/b/c.txt")}), QStringLiteral("/a/b/c.txt"));
        QCOMPARE(AddDlg::commonRoot({QStringLiteral("/a/b/c.txt"), QStringLiteral("/a/b/d/e.txt")}), QStringLiteral("/a/b"));
        QCOMPARE(AddDlg::commonRoot({QStringLiteral("/a/bc"), QStringLiteral("/a/bd")}), QStringLiteral("/a"));
        QCOMPARE(AddDlg::commonRoot({QStringLiteral("/a"), QStringLiteral("/b")}), QStringLiteral("/"));

        const TestRepo repo;
        const QString root = Git::workingTreeRoot(repo.path());
        QCOMPARE(AddDlg::relativePaths(root, {repo.path(), repo.filePath(QStringLiteral("sub/x.txt"))}),
                 (QStringList{QStringLiteral("."), QStringLiteral("sub/x.txt")}));
    }

    void listsUnversionedFilesChecked()
    {
        const auto repo = makeRepo();
        const AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(dlg.windowTitle().endsWith(QStringLiteral(" - Add - KortoiseGit")));
        QVERIFY(!okButton(dlg)->isEnabled());
        QVERIFY(waitForList(dlg));

        // Neither the modified tracked file nor the ignored one
        const QStringList unversioned{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt"), QStringLiteral("sub/c.txt")};
        QCOMPARE(listedPaths(dlg), unversioned);
        QCOMPARE(checkedPaths(dlg), unversioned);
        QVERIFY(okButton(dlg)->isEnabled());
        QVERIFY(dlg.listCtrl()->isColumnHidden(GitStatusListModel::LinesAddedColumn));
    }

    void addsTheCheckedFiles()
    {
        const auto repo = makeRepo();
        AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg));
        QVERIFY(setChecked(dlg, QStringLiteral("sub/c.txt"), Qt::Unchecked));

        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("adddlg.png"))));
        }

        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::mouseClick(okButton(dlg), Qt::LeftButton);
        QVERIFY(dlg.isAdding());
        QVERIFY(accepted.wait(10000));
        QCOMPARE(dlg.result(), QDialog::Accepted);
        QCOMPARE(stagedPaths(*repo), (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt")}));
    }

    void listsOnlyBelowItsPaths()
    {
        const auto repo = makeRepo();
        const AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->filePath(QStringLiteral("sub"))});
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("sub/b.txt"), QStringLiteral("sub/c.txt")}));
    }

    void includesIgnoredFilesUnchecked()
    {
        const auto repo = makeRepo();
        AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg));
        includeIgnored(dlg)->setChecked(true);
        QVERIFY(waitForList(dlg));
        QCOMPARE(listedPaths(dlg), (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt"), QStringLiteral("sub/c.txt"), QStringLiteral("x.log")}));
        QCOMPARE(checkedPaths(dlg), (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt"), QStringLiteral("sub/c.txt")}));

        // An ignored file the user checks is added in spite of .gitignore
        QVERIFY(setChecked(dlg, QStringLiteral("x.log"), Qt::Checked));
        QVERIFY(setChecked(dlg, QStringLiteral("a.txt"), Qt::Unchecked));
        QVERIFY(setChecked(dlg, QStringLiteral("sub/b.txt"), Qt::Unchecked));
        QVERIFY(setChecked(dlg, QStringLiteral("sub/c.txt"), Qt::Unchecked));
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        dlg.accept();
        QVERIFY(accepted.wait(10000));
        QCOMPARE(stagedPaths(*repo), QStringList{QStringLiteral("x.log")});

        includeIgnored(dlg)->setChecked(false);
        QVERIFY(waitForList(dlg));
        QVERIFY(!listedPaths(dlg).contains(QStringLiteral("x.log")));
    }

    void needsSomethingChecked()
    {
        const auto repo = makeRepo();
        AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg));
        dlg.listCtrl()->statusModel()->setAllChecked(false);
        QVERIFY(!okButton(dlg)->isEnabled());
        dlg.accept();
        QVERIFY(!dlg.isAdding());
        QCOMPARE(dlg.result(), QDialog::Rejected);
    }

    void saysWhenThereIsNothingToAdd()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("tracked.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const AddDlg dlg(Git::workingTreeRoot(repo.path()), {repo.path()});
        QVERIFY(waitForList(dlg));
        QCOMPARE(dlg.listCtrl()->statusModel()->rowCount(), 0);
        QVERIFY(!okButton(dlg)->isEnabled());
    }

    void showsGitsError()
    {
        const auto repo = makeRepo();
        AddDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg));
        // Another git holds the index
        repo->write(QStringLiteral(".git/index.lock"), "");

        QSignalSpy failed(&dlg, &AddDlg::addFailed);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        dlg.accept();
        QVERIFY(failed.wait(10000));
        QVERIFY2(failed.constFirst().constFirst().toString().contains(QStringLiteral("index.lock")), qPrintable(failed.constFirst().constFirst().toString()));
        QVERIFY(accepted.isEmpty());
        QVERIFY(!dlg.isAdding());
        auto *box = dlg.findChild<QMessageBox *>();
        QVERIFY(box);
        QCOMPARE(box->icon(), QMessageBox::Critical);
        box->close();

        // The dialog stays usable: with the lock gone, OK adds
        repo->remove(QStringLiteral(".git/index.lock"));
        QVERIFY(waitForList(dlg));
        dlg.accept();
        QVERIFY(accepted.wait(10000));
        QCOMPARE(stagedPaths(*repo), (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt"), QStringLiteral("sub/c.txt")}));
    }

    void addCommandAddsFilesWithoutDialog()
    {
        const auto repo = makeRepo();
        AddCommand command;
        command.setPaths({repo->filePath(QStringLiteral("a.txt")), repo->filePath(QStringLiteral("sub/b.txt"))});
        QVERIFY(command.execute());
        QCOMPARE(stagedPaths(*repo), (QStringList{QStringLiteral("a.txt"), QStringLiteral("sub/b.txt")}));
    }

    void aLinkToAFileStaysTheLink()
    {
        // Revert and Add must act on the link the user selected, not on the file it points to
        const TestRepo repo;
        repo.write(QStringLiteral("real.txt"), "1");
        QVERIFY(QFile::link(QStringLiteral("real.txt"), repo.filePath(QStringLiteral("link.txt"))));
        const QString root = QFileInfo(repo.path()).canonicalFilePath();
        QCOMPARE(AddDlg::relativePaths(root, {repo.filePath(QStringLiteral("link.txt"))}), QStringList{QStringLiteral("link.txt")});
        QCOMPARE(AddDlg::relativePaths(root, {repo.filePath(QStringLiteral("real.txt"))}), QStringList{QStringLiteral("real.txt")});
    }
};

QTEST_MAIN(AddDlgTest)

#include "adddlgtest.moc"
