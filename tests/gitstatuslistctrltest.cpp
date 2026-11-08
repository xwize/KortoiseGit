// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitstatuslistctrl.hpp"

#include "git/git.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <memory>
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

/**
 * Selects some files of a list, the first holding the selection mark.
 * @param ctrl The list, unsorted.
 * @param paths The listed paths to select.
 */
void select(GitStatusListCtrl& ctrl, const QStringList& paths)
{
    ctrl.clearSelection();
    for (const QString& path : paths) {
        const QModelIndex index = ctrl.model()->index(rowOf(*ctrl.statusModel(), path), 0);
        ctrl.selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (path == paths.constFirst()) {
            ctrl.selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        }
    }
}

/**
 * Opens the list's context menu, hands it to @p handle, and closes it.
 * @param ctrl The list.
 * @param handle Called with the open menu.
 */
void withContextMenu(GitStatusListCtrl& ctrl, const std::function<void(QMenu *)>& handle)
{
    QTimer::singleShot(0, &ctrl, [&handle] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu) {
            handle(menu);
            menu->close();
        }
    });
    const QPoint position = ctrl.viewport()->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, ctrl.viewport()->mapToGlobal(position));
    QApplication::sendEvent(ctrl.viewport(), &event);
}

/**
 * @param ctrl The list.
 * @returns The texts of its context menu for the selected files, separators left out.
 */
[[nodiscard]] QStringList menuTexts(GitStatusListCtrl& ctrl)
{
    QStringList texts;
    withContextMenu(ctrl, [&texts](const QMenu *menu) {
        for (const QAction *action : menu->actions()) {
            if (!action->isSeparator()) {
                texts.append(action->text());
            }
        }
    });
    return texts;
}

/**
 * Answers the confirmation KIO shows, whenever it appears.
 */
class DeletePromptAnswerer : public QObject
{
public:
    /**
     * @param confirm Whether to confirm the deletion, rather than cancel it.
     */
    explicit DeletePromptAnswerer(bool confirm)
        : m_confirm(confirm)
    {
        connect(&m_timer, &QTimer::timeout, this, &DeletePromptAnswerer::answer);
        m_timer.start(10);
    }

    /**
     * @returns How many prompts were answered.
     */
    [[nodiscard]] int answered() const
    {
        return m_answered;
    }

private:
    void answer()
    {
        const auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        const auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr;
        if (!buttons) {
            return;
        }
        const QList<QAbstractButton *> all = buttons->buttons();
        const auto chosen = std::ranges::find_if(all, [this, buttons](QAbstractButton *button) {
            const QDialogButtonBox::ButtonRole role = buttons->buttonRole(button);
            return m_confirm ? (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole)
                             : (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole);
        });
        if (chosen != all.cend()) {
            ++m_answered;
            (*chosen)->click();
        }
    }

    QTimer m_timer;
    bool m_confirm;
    int m_answered = 0;
};

/**
 * Picks Delete from the list's context menu for the selected files and waits for it to finish.
 * @param ctrl The list.
 * @returns Whether the menu offered it and it finished.
 */
[[nodiscard]] bool deleteFromMenu(GitStatusListCtrl& ctrl)
{
    bool triggered = false;
    withContextMenu(ctrl, [&triggered](const QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->text() == QStringLiteral("&Delete")) {
                triggered = true;
                action->trigger();
            }
        }
    });
    // Queued by the menu entry
    QCoreApplication::processEvents();
    return triggered
        && QTest::qWaitFor(
               [&ctrl] {
                   return !ctrl.isBusy();
               },
               15000);
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
        /**
         * Deleted files go to a trash of the test's own. KIO only uses the home trash for files on the same file
         * system as HOME, and won't make a trash folder on a tmpfs, so HOME moves next to the repositories too.
         */
        QVERIFY(m_dataHome.isValid());
        qputenv("HOME", m_dataHome.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_dataHome.path().toUtf8());
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

    void aPathFilterTooLongForACommandLineStillFilters()
    {
        const TestRepo repo;
        makeChanges(repo);
        QStringList filter{QStringLiteral("a.txt")};
        constexpr int extraPaths = 20000;
        for (int i = 0; i < extraPaths; ++i) {
            filter.append(QStringLiteral("no/such/folder/with/quite/a/long/name/for/this/test/%1.txt").arg(i));
        }
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setPathFilter(filter);
        ctrl.setShowFlags(Show::All, {});
        QVERIFY(reloadAndWait(ctrl));
        QCOMPARE(listedPaths(*ctrl.statusModel()), QStringList{QStringLiteral("a.txt")});
    }

    void aPathFilterIsTakenLiterally()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("odd*.txt"), "1\n");
        repo.write(QStringLiteral("oddly.txt"), "1\n");
        repo.commitAll();
        repo.write(QStringLiteral("odd*.txt"), "2\n");
        repo.write(QStringLiteral("oddly.txt"), "2\n");
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setPathFilter({QStringLiteral("odd*.txt")});
        ctrl.setShowFlags(Show::All, {});
        QVERIFY(reloadAndWait(ctrl));
        QCOMPARE(listedPaths(*ctrl.statusModel()), QStringList{QStringLiteral("odd*.txt")});
    }

    void deleteIsOfferedForUnversionedIgnoredAndMissing()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const QStringList afterExplore{QStringLiteral("Explore to"), QStringLiteral("&Delete")};
        for (const QString& path : {QStringLiteral("e.md"), QStringLiteral("f.log")}) {
            select(*ctrl, {path});
            QCOMPARE(menuTexts(*ctrl).sliced(menuTexts(*ctrl).size() - 2), afterExplore);
        }
        // A missing file has nothing to explore to
        select(*ctrl, {QStringLiteral("c.txt")});
        QCOMPARE(menuTexts(*ctrl).constLast(), QStringLiteral("&Delete"));
    }

    void deleteIsNotOfferedForTrackedChanges()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const QString deleteText = QStringLiteral("&Delete");
        for (const QString& path : {QStringLiteral("a.txt"), QStringLiteral("d.txt")}) {
            select(*ctrl, {path});
            QVERIFY(!menuTexts(*ctrl).contains(deleteText));
        }
        // The entry the menu is opened on decides
        select(*ctrl, {QStringLiteral("a.txt"), QStringLiteral("e.md")});
        QVERIFY(!menuTexts(*ctrl).contains(deleteText));
        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("a.txt")});
        QVERIFY(menuTexts(*ctrl).contains(deleteText));
    }

    void deleteTrashesUnversionedFilesAndDropsOnlyTheirRows()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        QLabel label;
        ctrl->setStatLabel(&label);
        ctrl->statusModel()->setData(ctrl->statusModel()->index(rowOf(*ctrl->statusModel(), QStringLiteral("d.txt")), GitStatusListModel::PathColumn),
                                     Qt::Unchecked,
                                     Qt::CheckStateRole);
        const int checkedBefore = ctrl->statusModel()->checkedCount();
        const int totalBefore = ctrl->statusModel()->listedCount();

        const DeletePromptAnswerer answerer(true);
        const QSignalSpy loaded(ctrl.get(), &GitStatusListCtrl::loaded);
        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("f.log")});
        QVERIFY(deleteFromMenu(*ctrl));

        QCOMPARE(answerer.answered(), 1);
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/e.md")));
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/f.log")));
        QVERIFY(QFile::exists(m_dataHome.filePath(QStringLiteral("Trash/files/e.md"))));
        QVERIFY(QFile::exists(m_dataHome.filePath(QStringLiteral("Trash/files/f.log"))));
        // The rows are dropped without a reload, which keeps the other check states
        QVERIFY(loaded.isEmpty());
        QCOMPARE(rowOf(*ctrl->statusModel(), QStringLiteral("e.md")), -1);
        QCOMPARE(rowOf(*ctrl->statusModel(), QStringLiteral("f.log")), -1);
        QCOMPARE(ctrl->statusModel()->listedCount(), totalBefore - 2);
        QCOMPARE(ctrl->statusModel()->checkedCount(), checkedBefore);
        QVERIFY(label.text().contains(QString::number(totalBefore - 2)));
        const auto dIndex = ctrl->statusModel()->index(rowOf(*ctrl->statusModel(), QStringLiteral("d.txt")), GitStatusListModel::PathColumn);
        QCOMPARE(dIndex.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
    }

    void deleteOfAnIgnoredFolderAndADanglingSymlink()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "ignored/\n");
        repo.commitAll();
        QVERIFY(QDir(repo.path()).mkdir(QStringLiteral("ignored")));
        repo.write(QStringLiteral("ignored/x"), "x\n");
        QVERIFY(QFile::link(QStringLiteral("nowhere"), repo.path() + QStringLiteral("/link")));
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        const DeletePromptAnswerer answerer(true);
        select(*ctrl, {QStringLiteral("link"), QStringLiteral("ignored/")});
        QVERIFY(deleteFromMenu(*ctrl));
        QVERIFY(!QFileInfo(repo.path() + QStringLiteral("/link")).isSymLink());
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/ignored")));
        QCOMPARE(ctrl->statusModel()->listedCount(), 0);
    }

    void shiftDeletesPermanently()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("keep.txt"), "k\n");
        repo.commitAll();
        repo.write(QStringLiteral("shifted.txt"), "s\n");
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        const DeletePromptAnswerer answerer(true);
        select(*ctrl, {QStringLiteral("shifted.txt")});
        QTest::keyPress(ctrl->windowHandle(), Qt::Key_Shift, Qt::ShiftModifier);
        const bool deleted = deleteFromMenu(*ctrl);
        QTest::keyRelease(ctrl->windowHandle(), Qt::Key_Shift, Qt::NoModifier);
        QVERIFY(deleted);
        QCOMPARE(answerer.answered(), 1);
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/shifted.txt")));
        QVERIFY(!QFile::exists(m_dataHome.filePath(QStringLiteral("Trash/files/shifted.txt"))));
    }

    void cancellingDeleteChangesNothing()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const QStringList listed = listedPaths(*ctrl->statusModel());

        const DeletePromptAnswerer answerer(false);
        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("d.txt")});
        QVERIFY(deleteFromMenu(*ctrl));

        QCOMPARE(answerer.answered(), 1);
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/e.md")));
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/d.txt")));
        QCOMPARE(lsFiles(repo, QStringLiteral("d.txt")), QByteArray("d.txt\n"));
        QCOMPARE(listedPaths(*ctrl->statusModel()), listed);
    }

    void deleteRemovesIndexedPathsAndReloads()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        const DeletePromptAnswerer answerer(true);
        QSignalSpy loaded(ctrl.get(), &GitStatusListCtrl::loaded);
        // The added file comes along because it is selected, as in TortoiseGit
        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("d.txt")});
        QVERIFY(deleteFromMenu(*ctrl));
        QVERIFY(loaded.size() == 1 || loaded.wait(10000));

        QCOMPARE(answerer.answered(), 1);
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/d.txt")));
        QCOMPARE(lsFiles(repo, QStringLiteral("d.txt")), QByteArray());
        QCOMPARE(rowOf(*ctrl->statusModel(), QStringLiteral("d.txt")), -1);
        QCOMPARE(rowOf(*ctrl->statusModel(), QStringLiteral("e.md")), -1);
    }

    void deletingAMissingFileNeedsNoPromptAndUpdatesTheIndex()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        const DeletePromptAnswerer answerer(true);
        QSignalSpy loaded(ctrl.get(), &GitStatusListCtrl::loaded);
        select(*ctrl, {QStringLiteral("c.txt")});
        QVERIFY(deleteFromMenu(*ctrl));
        QVERIFY(loaded.size() == 1 || loaded.wait(10000));

        QCOMPARE(answerer.answered(), 0);
        QCOMPARE(lsFiles(repo, QStringLiteral("c.txt")), QByteArray());
    }

    void deleteKeyActsOnUnversionedAndIgnoredOnly()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        const DeletePromptAnswerer answerer(true);
        for (const QString& path : {QStringLiteral("a.txt"), QStringLiteral("c.txt")}) {
            select(*ctrl, {path});
            QTest::keyClick(ctrl.get(), Qt::Key_Delete);
            QVERIFY(!ctrl->isBusy());
        }
        QCOMPARE(answerer.answered(), 0);
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/a.txt")));

        select(*ctrl, {QStringLiteral("e.md")});
        QTest::keyClick(ctrl.get(), Qt::Key_Delete);
        QVERIFY(QTest::qWaitFor(
            [&ctrl] {
                return !ctrl->isBusy();
            },
            15000));
        QCOMPARE(answerer.answered(), 1);
        QVERIFY(!QFile::exists(repo.path() + QStringLiteral("/e.md")));
        QCOMPARE(rowOf(*ctrl->statusModel(), QStringLiteral("e.md")), -1);
    }

private:
    /**
     * @param repo A repository.
     * @returns A shown list of everything in it, tracked files checked and loaded, or nullptr if it didn't load.
     */
    [[nodiscard]] static std::unique_ptr<GitStatusListCtrl> makeShownList(const TestRepo& repo)
    {
        auto ctrl = std::make_unique<GitStatusListCtrl>();
        ctrl->setWorkingTree(repo.path());
        ctrl->setShowFlags(GitStatusListModel::ShowFlags(Show::All) | Show::Ignored, Show::Versioned);
        ctrl->resize(600, 400);
        ctrl->show();
        if (!QTest::qWaitForWindowExposed(ctrl.get()) || !reloadAndWait(*ctrl)) {
            return nullptr;
        }
        return ctrl;
    }

    /**
     * @param repo A repository.
     * @param path A path relative to its root.
     * @returns What git ls-files says about it: the path and a newline if it is in the index.
     */
    [[nodiscard]] static QByteArray lsFiles(const TestRepo& repo, const QString& path)
    {
        return Git(repo.path()).run({QStringLiteral("ls-files"), QStringLiteral("--"), path}, RunMode::ReadOnly);
    }

    QTemporaryDir m_dataHome;
};

QTEST_MAIN(GitStatusListCtrlTest)

#include "gitstatuslistctrltest.moc"
