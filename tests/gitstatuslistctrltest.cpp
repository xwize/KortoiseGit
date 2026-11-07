// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitstatuslistctrl.hpp"

#include "git/git.hpp"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

#include <tuple>

namespace
{

using namespace kgt;
using Show = GitStatusListModel::Show;

/**
 * A throwaway repository on branch main. git's identity and configuration are fixed by initTestCase().
 */
class TestRepo
{
public:
    TestRepo()
        : m_git(m_dir.path())
    {
        Q_ASSERT(m_dir.isValid());
        git({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--initial-branch=main")});
    }

    /**
     * @returns The working tree root.
     */
    [[nodiscard]] QString path() const
    {
        return m_dir.path();
    }

    /**
     * @param arguments The arguments after "git".
     */
    void git(const QStringList& arguments) const
    {
        std::ignore = m_git.run(arguments);
    }

    /**
     * Creates or overwrites a file.
     * @param path Relative to the root.
     * @param content What the file holds.
     */
    void write(const QString& path, const QByteArray& content) const
    {
        QFile file(m_dir.filePath(path));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(content), content.size());
    }

    /**
     * Stages and commits everything.
     */
    void commitAll() const
    {
        git({QStringLiteral("add"), QStringLiteral("--all")});
        git({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), QStringLiteral("commit")});
    }

private:
    QTemporaryDir m_dir;
    Git m_git;
};

/**
 * Fills a repository with one file of each kind: modified (a.txt, 2 lines added and 1 removed), missing (c.txt),
 * renamed (r.txt to moved.txt), added (d.txt), unversioned (e.md) and ignored (f.log).
 * @param repo An empty repository.
 */
void makeChanges(const TestRepo& repo)
{
    repo.write(QStringLiteral("a.txt"), "1\n2\n3\n");
    repo.write(QStringLiteral("c.txt"), "c\n");
    repo.write(QStringLiteral("r.txt"), "a file that is renamed\n");
    repo.write(QStringLiteral(".gitignore"), "*.log\n");
    repo.commitAll();

    repo.write(QStringLiteral("a.txt"), "1\nTWO\n3\n4\n");
    QVERIFY(QFile::remove(QDir(repo.path()).filePath(QStringLiteral("c.txt"))));
    repo.git({QStringLiteral("mv"), QStringLiteral("r.txt"), QStringLiteral("moved.txt")});
    repo.write(QStringLiteral("d.txt"), "d\n");
    repo.git({QStringLiteral("add"), QStringLiteral("d.txt")});
    repo.write(QStringLiteral("e.md"), "e\n");
    repo.write(QStringLiteral("f.log"), "f\n");
}

/**
 * Reloads a list and waits for it.
 * @param ctrl The list.
 * @returns Whether it loaded.
 */
[[nodiscard]] bool reloadAndWait(GitStatusListCtrl& ctrl)
{
    const QSignalSpy loaded(&ctrl, &GitStatusListCtrl::loaded);
    ctrl.reload();
    return QTest::qWaitFor(
        [&] {
            return !loaded.isEmpty();
        },
        10000);
}

/**
 * @param model The list's model.
 * @param path A path relative to the root.
 * @returns The row listing it, or -1.
 */
[[nodiscard]] int rowOf(const GitStatusListModel& model, const QString& path)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.entry(row).path == path) {
            return row;
        }
    }
    return -1;
}

/**
 * @param model The list's model.
 * @returns The listed paths, sorted.
 */
[[nodiscard]] QStringList listedPaths(const GitStatusListModel& model)
{
    QStringList paths;
    for (int row = 0; row < model.rowCount(); ++row) {
        paths.append(model.entry(row).path);
    }
    paths.sort();
    return paths;
}

/**
 * @param model The list's model.
 * @param path A listed path.
 * @param column A column.
 * @returns The cell's text.
 */
[[nodiscard]] QString cell(const GitStatusListModel& model, const QString& path, int column)
{
    return model.index(rowOf(model, path), column).data().toString();
}

} // namespace

/**
 * GitStatusListCtrl and its model, against real repositories. Set KGT_SCREENSHOT_DIR to save a screenshot there.
 */
class GitStatusListCtrlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the user's own git configuration (signing, hooks, renames off) out of the tests
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
    }

    void listsColumns()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));

        const GitStatusListModel& model = *ctrl.statusModel();
        QCOMPARE(model.columnCount(), 5);
        const QStringList headers{QStringLiteral("Path"),
                                  QStringLiteral("Extension"),
                                  QStringLiteral("Status"),
                                  QStringLiteral("Lines added"),
                                  QStringLiteral("Lines removed")};
        for (int column = 0; column < headers.size(); ++column) {
            QCOMPARE(model.headerData(column, Qt::Horizontal).toString(), headers.at(column));
        }

        QCOMPARE(listedPaths(model),
                 (QStringList{QStringLiteral("a.txt"), QStringLiteral("c.txt"), QStringLiteral("d.txt"), QStringLiteral("e.md"), QStringLiteral("moved.txt")}));

        const auto modified = QStringLiteral("a.txt");
        QCOMPARE(cell(model, modified, GitStatusListModel::PathColumn), modified);
        QCOMPARE(cell(model, modified, GitStatusListModel::ExtensionColumn), QStringLiteral(".txt"));
        QCOMPARE(cell(model, modified, GitStatusListModel::StatusColumn), QStringLiteral("Modified"));
        QCOMPARE(cell(model, modified, GitStatusListModel::LinesAddedColumn), QStringLiteral("2"));
        QCOMPARE(cell(model, modified, GitStatusListModel::LinesRemovedColumn), QStringLiteral("1"));

        QCOMPARE(cell(model, QStringLiteral("c.txt"), GitStatusListModel::StatusColumn), QStringLiteral("Missing"));
        QCOMPARE(cell(model, QStringLiteral("c.txt"), GitStatusListModel::LinesRemovedColumn), QStringLiteral("1"));
        QCOMPARE(cell(model, QStringLiteral("d.txt"), GitStatusListModel::StatusColumn), QStringLiteral("Added"));
        QCOMPARE(cell(model, QStringLiteral("d.txt"), GitStatusListModel::LinesAddedColumn), QStringLiteral("1"));
        QCOMPARE(cell(model, QStringLiteral("moved.txt"), GitStatusListModel::PathColumn), QStringLiteral("moved.txt (from r.txt)"));
        QCOMPARE(cell(model, QStringLiteral("moved.txt"), GitStatusListModel::StatusColumn), QStringLiteral("Rename"));
        QCOMPARE(cell(model, QStringLiteral("e.md"), GitStatusListModel::ExtensionColumn), QStringLiteral(".md"));
        QCOMPARE(cell(model, QStringLiteral("e.md"), GitStatusListModel::LinesAddedColumn), QString());
    }

    void sortsByColumn()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));

        ctrl.sortByColumn(GitStatusListModel::LinesAddedColumn, Qt::DescendingOrder);
        QCOMPARE(ctrl.model()->index(0, GitStatusListModel::PathColumn).data(GitStatusListModel::PathRole).toString(), QStringLiteral("a.txt"));
        ctrl.sortByColumn(GitStatusListModel::PathColumn, Qt::AscendingOrder);
        QCOMPARE(ctrl.model()->index(0, GitStatusListModel::PathColumn).data(GitStatusListModel::PathRole).toString(), QStringLiteral("a.txt"));
        QCOMPARE(ctrl.model()->index(4, GitStatusListModel::PathColumn).data(GitStatusListModel::PathRole).toString(), QStringLiteral("moved.txt"));
    }

    void togglesCheckState()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));
        GitStatusListModel& model = *ctrl.statusModel();

        // Tracked changes start checked, the unversioned file doesn't
        QCOMPARE(model.checkedCount(), 4);
        const QModelIndex unversioned = model.index(rowOf(model, QStringLiteral("e.md")), GitStatusListModel::PathColumn);
        QCOMPARE(unversioned.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
        QVERIFY(model.flags(unversioned).testFlag(Qt::ItemIsUserCheckable));

        const QSignalSpy changed(&ctrl, &GitStatusListCtrl::checkedCountChanged);
        const QModelIndex modified = model.index(rowOf(model, QStringLiteral("a.txt")), GitStatusListModel::PathColumn);
        QVERIFY(model.setData(modified, Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(model.setData(unversioned, Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(model.checkedCount(), 4);
        QStringList checked;
        for (const GitStatusListEntry& entry : ctrl.checkedEntries()) {
            checked.append(entry.path);
        }
        checked.sort();
        QCOMPARE(checked, (QStringList{QStringLiteral("c.txt"), QStringLiteral("d.txt"), QStringLiteral("e.md"), QStringLiteral("moved.txt")}));

        // The check state survives a reload
        QVERIFY(reloadAndWait(ctrl));
        QCOMPARE(model.index(rowOf(model, QStringLiteral("a.txt")), 0).data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
        QCOMPARE(model.index(rowOf(model, QStringLiteral("e.md")), 0).data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
    }

    void selectAllAndCounter()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        QCheckBox selectAll;
        QLabel counter;
        ctrl.setSelectButton(&selectAll);
        ctrl.setStatLabel(&counter);
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));

        QCOMPARE(selectAll.checkState(), Qt::PartiallyChecked);
        QCOMPARE(counter.text(), QStringLiteral("4 files selected, 5 files total"));

        // As in TortoiseGit, a click on a partly checked box unchecks everything, and the next checks everything
        selectAll.click();
        QCOMPARE(selectAll.checkState(), Qt::Unchecked);
        QCOMPARE(ctrl.statusModel()->checkedCount(), 0);
        QCOMPARE(counter.text(), QStringLiteral("0 files selected, 5 files total"));
        selectAll.click();
        QCOMPARE(selectAll.checkState(), Qt::Checked);
        QCOMPARE(ctrl.statusModel()->checkedCount(), 5);
        selectAll.click();
        QCOMPARE(selectAll.checkState(), Qt::Unchecked);
    }

    void filtersByShowFlags()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));
        const GitStatusListModel& model = *ctrl.statusModel();
        QCOMPARE(listedPaths(model), (QStringList{QStringLiteral("a.txt"), QStringLiteral("c.txt"), QStringLiteral("d.txt"), QStringLiteral("moved.txt")}));

        ctrl.setShowFlags(Show::Unversioned, {});
        QCOMPARE(listedPaths(model), QStringList{QStringLiteral("e.md")});

        ctrl.setShowFlags(Show::Added | Show::Removed, {});
        QCOMPARE(listedPaths(model), (QStringList{QStringLiteral("c.txt"), QStringLiteral("d.txt")}));

        // Ignored files are loaded only once they are asked for
        QVERIFY(rowOf(model, QStringLiteral("f.log")) < 0);
        const QSignalSpy loaded(&ctrl, &GitStatusListCtrl::loaded);
        ctrl.setShowFlags(Show::Ignored, {});
        QVERIFY(ctrl.isBusy());
        QVERIFY(QTest::qWaitFor([&] {
            return !loaded.isEmpty();
        }));
        QCOMPARE(listedPaths(model), QStringList{QStringLiteral("f.log")});
        QCOMPARE(cell(model, QStringLiteral("f.log"), GitStatusListModel::StatusColumn), QStringLiteral("Ignored"));
    }

    void listsInitialCommit()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("first.cpp"), "int main() {}\n");
        repo.git({QStringLiteral("add"), QStringLiteral("first.cpp")});
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));
        const GitStatusListModel& model = *ctrl.statusModel();
        QCOMPARE(listedPaths(model), QStringList{QStringLiteral("first.cpp")});
        QCOMPARE(cell(model, QStringLiteral("first.cpp"), GitStatusListModel::StatusColumn), QStringLiteral("Added"));
        QCOMPARE(cell(model, QStringLiteral("first.cpp"), GitStatusListModel::LinesAddedColumn), QString());
    }

    void reportsLoadFailure()
    {
        const QTemporaryDir notARepo;
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(notARepo.path());
        const QSignalSpy failed(&ctrl, &GitStatusListCtrl::loadFailed);
        ctrl.reload();
        QVERIFY(QTest::qWaitFor([&] {
            return !failed.isEmpty();
        }));
        QVERIFY(!ctrl.isBusy());
        QCOMPARE(ctrl.statusModel()->rowCount(), 0);
    }

    void screenshot()
    {
        const TestRepo repo;
        makeChanges(repo);
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *ctrl = new GitStatusListCtrl(&window);
        auto *selectAll = new QCheckBox(QStringLiteral("Select/deselect &all"), &window);
        auto *counter = new QLabel(&window);
        layout->addWidget(ctrl);
        layout->addWidget(selectAll);
        layout->addWidget(counter);
        ctrl->setSelectButton(selectAll);
        ctrl->setStatLabel(counter);
        ctrl->setWorkingTree(repo.path());
        ctrl->setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        QVERIFY(reloadAndWait(*ctrl));
        window.resize(640, 300);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const QPixmap shot = window.grab();
        QVERIFY(!shot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(shot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("gitstatuslistctrl.png"))));
        }
    }
};

QTEST_MAIN(GitStatusListCtrlTest)

#include "gitstatuslistctrltest.moc"
