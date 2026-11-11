// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitstatuslistctrl.hpp"

#include "adddlg.hpp"
#include "git/git.hpp"
#include "progressdlg.hpp"

#include <QAbstractProxyModel>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <functional>
#include <memory>
#include <tuple>

namespace
{

using namespace kgt;
using Show = GitStatusListModel::Show;
using Action = TGitPath::Action;

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
 * @param path A path relative to the root.
 * @param actions What happened to it.
 * @param parentNo The parent the change is against.
 * @returns An entry for it.
 */
[[nodiscard]] GitStatusListEntry makeEntry(const QString& path, TGitPath::Actions actions, int parentNo = 0)
{
    return GitStatusListEntry{
        .path = path,
        .origPath = {},
        .actions = actions,
        .linesAdded = {},
        .linesRemoved = {},
        .parentNo = parentNo,
    };
}

/**
 * @param model The list's model or its sorting proxy.
 * @returns The text of each group header, top to bottom.
 */
[[nodiscard]] QStringList groupHeaders(const QAbstractItemModel& model)
{
    QStringList headers;
    for (int group = 0; group < model.rowCount(); ++group) {
        headers.append(model.index(group, GitStatusListModel::PathColumn).data().toString());
    }
    return headers;
}

/**
 * @param model The list's model or its sorting proxy.
 * @param group Which group.
 * @returns The paths in it, top to bottom.
 */
[[nodiscard]] QStringList groupPaths(const QAbstractItemModel& model, int group)
{
    const QModelIndex header = model.index(group, GitStatusListModel::PathColumn);
    QStringList paths;
    for (int row = 0; row < model.rowCount(header); ++row) {
        paths.append(model.index(row, GitStatusListModel::PathColumn, header).data(GitStatusListModel::PathRole).toString());
    }
    return paths;
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
    for (int row = 0; row < model.listedCount(); ++row) {
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
    for (int row = 0; row < model.listedCount(); ++row) {
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
    return model.entryIndex(rowOf(model, path), column).data().toString();
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
        const QModelIndex index =
            qobject_cast<const QAbstractProxyModel *>(ctrl.model())->mapFromSource(ctrl.statusModel()->entryIndex(rowOf(*ctrl.statusModel(), path)));
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

/**
 * Picks an Add entry from the list's context menu for the selected files, waits for the progress dialog to succeed,
 * closes it and waits for the reload.
 * @param ctrl The list.
 * @param text The entry.
 * @param withShift Whether Shift is held as the menu opens, for the entries it reveals.
 * @returns Whether the menu offered it and git succeeded.
 */
[[nodiscard]] bool addFromMenu(GitStatusListCtrl& ctrl, const QString& text, bool withShift = false)
{
    if (withShift) {
        QTest::keyPress(ctrl.windowHandle(), Qt::Key_Shift, Qt::ShiftModifier);
    }
    bool triggered = false;
    withContextMenu(ctrl, [&triggered, &text](const QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->text() == text) {
                triggered = true;
                action->trigger();
            }
        }
    });
    if (withShift) {
        QTest::keyRelease(ctrl.windowHandle(), Qt::Key_Shift, Qt::NoModifier);
    }
    if (!triggered) {
        return false;
    }
    ProgressDlg *progress = nullptr;
    const bool done = QTest::qWaitFor(
        [&progress] {
            progress = qobject_cast<ProgressDlg *>(QApplication::activeModalWidget());
            return progress && progress->isDone();
        },
        15000);
    if (!done || progress->gitStatus() != 0) {
        return false;
    }
    progress->accept();
    return QTest::qWaitFor(
        [&ctrl] {
            return !ctrl.isBusy();
        },
        15000);
}

/**
 * Commits old.txt, then renames it to new.txt outside git.
 * @param repo An empty repository.
 */
void renameOutsideGit(const TestRepo& repo)
{
    repo.write(QStringLiteral("old.txt"), "a file that is renamed\n");
    repo.commitAll();
    QVERIFY(QFile::rename(repo.path() + QStringLiteral("/old.txt"), repo.path() + QStringLiteral("/new.txt")));
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
        const QModelIndex modifiedFiles = ctrl.model()->index(0, 0);
        QCOMPARE(ctrl.model()->index(0, GitStatusListModel::PathColumn, modifiedFiles).data(GitStatusListModel::PathRole).toString(), QStringLiteral("a.txt"));
        ctrl.sortByColumn(GitStatusListModel::PathColumn, Qt::AscendingOrder);
        QCOMPARE(ctrl.model()->index(0, GitStatusListModel::PathColumn, modifiedFiles).data(GitStatusListModel::PathRole).toString(), QStringLiteral("a.txt"));
        QCOMPARE(ctrl.model()->index(3, GitStatusListModel::PathColumn, modifiedFiles).data(GitStatusListModel::PathRole).toString(),
                 QStringLiteral("moved.txt"));
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
        const QModelIndex unversioned = model.entryIndex(rowOf(model, QStringLiteral("e.md")), GitStatusListModel::PathColumn);
        QCOMPARE(unversioned.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
        QVERIFY(model.flags(unversioned).testFlag(Qt::ItemIsUserCheckable));

        const QSignalSpy changed(&ctrl, &GitStatusListCtrl::checkedCountChanged);
        const QModelIndex modified = model.entryIndex(rowOf(model, QStringLiteral("a.txt")), GitStatusListModel::PathColumn);
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
        QCOMPARE(model.entryIndex(rowOf(model, QStringLiteral("a.txt")), 0).data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
        QCOMPARE(model.entryIndex(rowOf(model, QStringLiteral("e.md")), 0).data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
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

    void revisionEntriesAreOnlyForACommit()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        // CGitStatusListCtrl::OnContextMenuList offers these only with m_CurrentVersion set
        const QStringList revisionEntries{QStringLiteral("R&evert to this revision"),
                                          QStringLiteral("Show &log"),
                                          QStringLiteral("Save revision &to..."),
                                          QStringLiteral("View revision in alternative editor")};
        QStringList offeredFor;
        for (const GitStatusListEntry& entry : ctrl->statusModel()->allEntries()) {
            select(*ctrl, {entry.path});
            const QStringList texts = menuTexts(*ctrl);
            if (std::ranges::any_of(revisionEntries, [&texts](const QString& text) {
                    return texts.contains(text);
                })) {
                offeredFor.append(entry.path);
            }
        }
        QCOMPARE(offeredFor, QStringList());
    }

    void deletingAMissingFileNeedsNoPromptAndUpdatesTheIndex()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        QSignalSpy loaded(ctrl.get(), &GitStatusListCtrl::loaded);
        select(*ctrl, {QStringLiteral("c.txt")});
        QVERIFY(deleteFromMenu(*ctrl));
        QVERIFY(loaded.size() == 1 || loaded.wait(10000));

        QCOMPARE(lsFiles(repo, QStringLiteral("c.txt")), QByteArray());
    }

    void deleteKeyIgnoresTrackedAndMissingFiles()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        for (const QString& path : {QStringLiteral("a.txt"), QStringLiteral("c.txt")}) {
            select(*ctrl, {path});
            QTest::keyClick(ctrl.get(), Qt::Key_Delete);
            QVERIFY(!ctrl->isBusy());
        }
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/a.txt")));
    }

    void addIsOfferedForUnversionedAndIgnored()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const QString addText = QStringLiteral("Add");
        for (const QString& path : {QStringLiteral("e.md"), QStringLiteral("f.log")}) {
            select(*ctrl, {path});
            QCOMPARE(menuTexts(*ctrl).constFirst(), addText);
        }
        for (const QString& path : {QStringLiteral("a.txt"), QStringLiteral("c.txt"), QStringLiteral("d.txt")}) {
            select(*ctrl, {path});
            QVERIFY(!menuTexts(*ctrl).contains(addText));
        }
        // It comes before Compare, and the entry the menu is opened on decides
        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("a.txt")});
        QCOMPARE(menuTexts(*ctrl).mid(0, 2), QStringList({addText, QStringLiteral("Compare with b&ase")}));
        select(*ctrl, {QStringLiteral("a.txt"), QStringLiteral("e.md")});
        QVERIFY(!menuTexts(*ctrl).contains(addText));
    }

    void addIsNotOfferedInTheAddDialog()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll();
        repo.write(QStringLiteral("new.txt"), "n\n");
        AddDlg dlg(repo.path(), {repo.path()});
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QVERIFY(QTest::qWaitFor([&dlg] {
            return !dlg.listCtrl()->isBusy();
        }));
        select(*dlg.listCtrl(), {QStringLiteral("new.txt")});
        const QStringList texts = menuTexts(*dlg.listCtrl());
        QVERIFY(texts.contains(QStringLiteral("&Delete")));
        QVERIFY(!texts.contains(QStringLiteral("Add")));
    }

    void shiftOffersTheFileModes()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        select(*ctrl, {QStringLiteral("e.md")});
        QVERIFY(!menuTexts(*ctrl).contains(QStringLiteral("Add as Executable (+x)")));
        QTest::keyPress(ctrl->windowHandle(), Qt::Key_Shift, Qt::ShiftModifier);
        const QStringList texts = menuTexts(*ctrl);
        QTest::keyRelease(ctrl->windowHandle(), Qt::Key_Shift, Qt::NoModifier);
        QCOMPARE(texts.mid(0, 3), QStringList({QStringLiteral("Add"), QStringLiteral("Add as Executable (+x)"), QStringLiteral("Add as Symlink")}));
    }

    void addSetsTheChosenFileMode()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll();
        repo.write(QStringLiteral("tool.sh"), "#!/bin/sh\n");
        repo.write(QStringLiteral("link.txt"), "a.txt");
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);

        select(*ctrl, {QStringLiteral("tool.sh")});
        QVERIFY(addFromMenu(*ctrl, QStringLiteral("Add as Executable (+x)"), true));
        QVERIFY(indexEntry(repo, QStringLiteral("tool.sh")).startsWith("100755 "));

        select(*ctrl, {QStringLiteral("link.txt")});
        QVERIFY(addFromMenu(*ctrl, QStringLiteral("Add as Symlink"), true));
        QVERIFY(indexEntry(repo, QStringLiteral("link.txt")).startsWith("120000 "));
    }

    void addActsOnTheSelectionAndChecksTheFiles()
    {
        const TestRepo repo;
        makeChanges(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const GitStatusListModel *model = ctrl->statusModel();
        const QModelIndex before = model->entryIndex(rowOf(*model, QStringLiteral("e.md")), GitStatusListModel::PathColumn);
        QCOMPARE(before.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);

        select(*ctrl, {QStringLiteral("e.md"), QStringLiteral("f.log")});
        QVERIFY(addFromMenu(*ctrl, QStringLiteral("Add")));

        QCOMPARE(lsFiles(repo, QStringLiteral("e.md")), QByteArray("e.md\n"));
        QCOMPARE(lsFiles(repo, QStringLiteral("f.log")), QByteArray("f.log\n"));
        for (const QString& path : {QStringLiteral("e.md"), QStringLiteral("f.log")}) {
            QCOMPARE(cell(*model, path, GitStatusListModel::StatusColumn), GitStatusListModel::actionName(TGitPath::Action::Added));
            const QModelIndex after = model->entryIndex(rowOf(*model, path), GitStatusListModel::PathColumn);
            QCOMPARE(after.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
        }
    }

    void deleteThenAddShowsARename()
    {
        const TestRepo repo;
        renameOutsideGit(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        select(*ctrl, {QStringLiteral("old.txt")});
        QVERIFY(deleteFromMenu(*ctrl));
        select(*ctrl, {QStringLiteral("new.txt")});
        QVERIFY(addFromMenu(*ctrl, QStringLiteral("Add")));
        verifyShowsRename(*ctrl);
    }

    void addThenDeleteShowsARename()
    {
        const TestRepo repo;
        renameOutsideGit(repo);
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        const GitStatusListModel& model = *ctrl->statusModel();
        select(*ctrl, {QStringLiteral("new.txt")});
        QVERIFY(addFromMenu(*ctrl, QStringLiteral("Add")));
        // With only the add done, TortoiseGit shows the two halves separately
        QCOMPARE(cell(model, QStringLiteral("new.txt"), GitStatusListModel::PathColumn), QStringLiteral("new.txt"));
        QCOMPARE(cell(model, QStringLiteral("new.txt"), GitStatusListModel::StatusColumn), GitStatusListModel::actionName(TGitPath::Action::Added));
        QCOMPARE(cell(model, QStringLiteral("old.txt"), GitStatusListModel::StatusColumn),
                 GitStatusListModel::actionName(TGitPath::Actions(TGitPath::Action::Deleted) | TGitPath::Action::Missing));

        select(*ctrl, {QStringLiteral("old.txt")});
        QVERIFY(deleteFromMenu(*ctrl));
        verifyShowsRename(*ctrl);
    }

    void renameIsDetectedWithStatusRenamesOff()
    {
        const TestRepo repo;
        renameOutsideGit(repo);
        repo.git({QStringLiteral("config"), QStringLiteral("status.renames"), QStringLiteral("false")});
        repo.git({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("--cached"), QStringLiteral("old.txt")});
        repo.git({QStringLiteral("add"), QStringLiteral("new.txt")});
        auto ctrl = makeShownList(repo);
        QVERIFY(ctrl);
        verifyShowsRename(*ctrl);
    }

    void groupsByStatusInTortoiseGitsOrder()
    {
        const TestRepo repo;
        makeChanges(repo);
        GitStatusListCtrl ctrl;
        ctrl.setWorkingTree(repo.path());
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned | Show::Ignored, Show::Versioned);
        QVERIFY(reloadAndWait(ctrl));
        const GitStatusListModel& model = *ctrl.statusModel();

        QVERIFY(model.hasGroups());
        QCOMPARE(groupHeaders(model), (QStringList{QStringLiteral("Modified Files"), QStringLiteral("Not Versioned Files"), QStringLiteral("Ignored Files")}));
        QStringList modified = groupPaths(model, 0);
        modified.sort();
        QCOMPARE(modified, (QStringList{QStringLiteral("a.txt"), QStringLiteral("c.txt"), QStringLiteral("d.txt"), QStringLiteral("moved.txt")}));
        QCOMPARE(groupPaths(model, 1), QStringList{QStringLiteral("e.md")});
        QCOMPARE(groupPaths(model, 2), QStringList{QStringLiteral("f.log")});
        QCOMPARE(model.listedCount(), 6);

        // The view shows them whichever way the model was reset
        QVERIFY(ctrl.isFirstColumnSpanned(0, {}));
        QVERIFY(ctrl.isExpanded(ctrl.model()->index(2, 0)));
    }

    void onlyModifiedFilesStaysFlat()
    {
        GitStatusListModel model;
        model.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        model.setEntries({makeEntry(QStringLiteral("a.txt"), Action::Modified), makeEntry(QStringLiteral("b.txt"), Action::Added)});
        QVERIFY(!model.hasGroups());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.entry(model.index(1, 0))->path, QStringLiteral("b.txt"));
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
        QCOMPARE(model.entryIndex(1), model.index(1, 0));
    }

    void showFlagsAddAndRemoveGroups()
    {
        GitStatusListModel model;
        model.setEntries({makeEntry(QStringLiteral("a.txt"), Action::Modified),
                          makeEntry(QStringLiteral("e.md"), Action::Unversioned),
                          makeEntry(QStringLiteral("f.log"), Action::Ignore),
                          makeEntry(QStringLiteral("v.txt"), Action::AssumeValid),
                          makeEntry(QStringLiteral("w.txt"), Action::SkipWorktree)});
        const QString modified = QStringLiteral("Modified Files");
        const QString unversioned = QStringLiteral("Not Versioned Files");
        const QString ignored = QStringLiteral("Ignored Files");
        const QString localChanges = QStringLiteral("Local changes ignored (assumed valid/unchanged or skip worktree flagged files)");

        model.setShowFlags(Show::Versioned, {});
        QVERIFY(!model.hasGroups());
        QCOMPARE(model.rowCount(), 1);

        model.setShowFlags(Show::Versioned | Show::Unversioned, {});
        QCOMPARE(groupHeaders(model), (QStringList{modified, unversioned}));

        model.setShowFlags(Show::Versioned | Show::Ignored, {});
        QCOMPARE(groupHeaders(model), (QStringList{modified, ignored}));

        model.setShowFlags(Show::Versioned | Show::AssumeValid, {});
        QCOMPARE(groupHeaders(model), (QStringList{modified, localChanges}));
        QCOMPARE(groupPaths(model, 1), QStringList{QStringLiteral("v.txt")});

        model.setShowFlags(Show::Versioned | Show::Unversioned | Show::Ignored | Show::AssumeValid | Show::SkipWorktree, {});
        QCOMPARE(groupHeaders(model), (QStringList{modified, unversioned, ignored, localChanges}));
        QCOMPARE(groupPaths(model, 3), (QStringList{QStringLiteral("v.txt"), QStringLiteral("w.txt")}));
    }

    void emptyGroupsHaveNoRow()
    {
        GitStatusListModel model;
        model.setShowFlags(Show::Versioned | Show::Unversioned | Show::Ignored, {});
        model.setEntries({makeEntry(QStringLiteral("f.log"), Action::Ignore)});
        QCOMPARE(groupHeaders(model), QStringList{QStringLiteral("Ignored Files")});
        QCOMPARE(model.entry(model.entryIndex(0))->path, QStringLiteral("f.log"));
    }

    void sortingStaysWithinGroups()
    {
        GitStatusListCtrl ctrl;
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        ctrl.updateWithGitPathList({makeEntry(QStringLiteral("a.txt"), Action::Modified),
                                    makeEntry(QStringLiteral("z.txt"), Action::Modified),
                                    makeEntry(QStringLiteral("b.md"), Action::Unversioned),
                                    makeEntry(QStringLiteral("y.md"), Action::Unversioned)});
        const QStringList headers{QStringLiteral("Modified Files"), QStringLiteral("Not Versioned Files")};

        ctrl.sortByColumn(GitStatusListModel::PathColumn, Qt::DescendingOrder);
        QCOMPARE(groupHeaders(*ctrl.model()), headers);
        QCOMPARE(groupPaths(*ctrl.model(), 0), (QStringList{QStringLiteral("z.txt"), QStringLiteral("a.txt")}));
        QCOMPARE(groupPaths(*ctrl.model(), 1), (QStringList{QStringLiteral("y.md"), QStringLiteral("b.md")}));

        ctrl.sortByColumn(GitStatusListModel::PathColumn, Qt::AscendingOrder);
        QCOMPARE(groupHeaders(*ctrl.model()), headers);
        QCOMPARE(groupPaths(*ctrl.model(), 0), (QStringList{QStringLiteral("a.txt"), QStringLiteral("z.txt")}));
        QCOMPARE(groupPaths(*ctrl.model(), 1), (QStringList{QStringLiteral("b.md"), QStringLiteral("y.md")}));
    }

    void headerRowsAreNotEntries()
    {
        GitStatusListModel model;
        model.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        model.setEntries({makeEntry(QStringLiteral("a.txt"), Action::Modified), makeEntry(QStringLiteral("e.md"), Action::Unversioned)});
        const QModelIndex header = model.index(0, GitStatusListModel::PathColumn);

        QCOMPARE(model.entry(header), nullptr);
        QCOMPARE(model.flags(header), Qt::NoItemFlags);
        QVERIFY(!header.data(Qt::CheckStateRole).isValid());
        QVERIFY(!model.setData(header, Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(header.data(Qt::FontRole).value<QFont>().bold(), true);

        QCOMPARE(model.listedCount(), 2);
        QCOMPARE(model.checkedCount(), 1);
        model.setAllChecked(true);
        QCOMPARE(model.checkedCount(), 2);
        QCOMPARE(model.checkedEntries().size(), 2);
        model.setAllChecked(false);
        QCOMPARE(model.checkedCount(), 0);
        QVERIFY(model.checkedEntries().isEmpty());
    }

    void keyboardStepsOverHeaderRows()
    {
        GitStatusListCtrl ctrl;
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        ctrl.updateWithGitPathList({makeEntry(QStringLiteral("a.txt"), Action::Modified),
                                    makeEntry(QStringLiteral("b.txt"), Action::Modified),
                                    makeEntry(QStringLiteral("e.md"), Action::Unversioned)});
        ctrl.resize(400, 300);
        ctrl.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ctrl));
        const auto currentPath = [&ctrl] {
            return ctrl.currentIndex().data(GitStatusListModel::PathRole).toString();
        };

        ctrl.setCurrentIndex(ctrl.model()->index(1, 0, ctrl.model()->index(0, 0)));
        QCOMPARE(currentPath(), QStringLiteral("b.txt"));
        QTest::keyClick(&ctrl, Qt::Key_Down);
        QCOMPARE(currentPath(), QStringLiteral("e.md"));
        QTest::keyClick(&ctrl, Qt::Key_Up);
        QCOMPARE(currentPath(), QStringLiteral("b.txt"));
        QTest::keyClick(&ctrl, Qt::Key_Home);
        QCOMPARE(currentPath(), QStringLiteral("a.txt"));
        QTest::keyClick(&ctrl, Qt::Key_End);
        QCOMPARE(currentPath(), QStringLiteral("e.md"));

        ctrl.selectAll();
        QCOMPARE(ctrl.selectionModel()->selectedRows().size(), 3);
    }

    void selectFirstConflictFindsAGroupedEntry()
    {
        GitStatusListCtrl ctrl;
        ctrl.setShowFlags(Show::Versioned | Show::Unversioned, Show::Versioned);
        ctrl.updateWithGitPathList({makeEntry(QStringLiteral("a.txt"), Action::Modified),
                                    makeEntry(QStringLiteral("b.txt"), Action::Unmerged),
                                    makeEntry(QStringLiteral("e.md"), Action::Unversioned)});
        ctrl.selectFirstConflict();
        const QModelIndexList selected = ctrl.selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.constFirst().data(GitStatusListModel::PathRole).toString(), QStringLiteral("b.txt"));
    }

    void mergeParentsGroupAsBefore()
    {
        GitStatusListModel model;
        model.setEntries({makeEntry(QStringLiteral("a.txt"), Action::Modified, 0), makeEntry(QStringLiteral("b.txt"), Action::Added, 1)});
        model.setParentHeaders({QStringLiteral("Parent one"), QStringLiteral("Parent two")});
        QCOMPARE(groupHeaders(model), (QStringList{QStringLiteral("Parent one"), QStringLiteral("Parent two")}));
        QCOMPARE(groupPaths(model, 0), QStringList{QStringLiteral("a.txt")});
        QCOMPARE(groupPaths(model, 1), QStringList{QStringLiteral("b.txt")});

        // A parent with no changes has no row, and the others keep their own headers
        model.setEntries({makeEntry(QStringLiteral("b.txt"), Action::Added, 1)});
        QCOMPARE(groupHeaders(model), QStringList{QStringLiteral("Parent two")});
        QCOMPARE(groupPaths(model, 0), QStringList{QStringLiteral("b.txt")});
    }

    void coloursFollowOnNMCustomdrawsPrecedence()
    {
        GitStatusListModel model;
        model.setShowFlags(Show::All | Show::Ignored | Show::AssumeValid | Show::SkipWorktree, {});
        const auto colour = [&model](TGitPath::Actions actions) {
            model.setEntries({makeEntry(QStringLiteral("a.txt"), actions)});
            return model.entryIndex(0).data(Qt::ForegroundRole);
        };

        const QColor conflict = colour(Action::Unmerged).value<QColor>();
        const QColor modified = colour(Action::Modified).value<QColor>();
        const QColor added = colour(Action::Added).value<QColor>();
        const QColor deleted = colour(Action::Deleted).value<QColor>();
        const QColor renamed = colour(Action::Replaced).value<QColor>();
        const QColor merged = colour(Action::Merged).value<QColor>();
        const QSet<QRgb> distinct{conflict.rgb(), modified.rgb(), added.rgb(), deleted.rgb(), renamed.rgb(), merged.rgb()};
        QCOMPARE(distinct.size(), 6);

        // The first match wins
        QCOMPARE(colour(Action::Unmerged | Action::Modified).value<QColor>(), conflict);
        QCOMPARE(colour(Action::Modified | Action::Added).value<QColor>(), modified);
        QCOMPARE(colour(Action::Added | Action::Deleted).value<QColor>(), added);
        QCOMPARE(colour(Action::Copy).value<QColor>(), added);
        QCOMPARE(colour(Action::Deleted | Action::Missing | Action::Replaced).value<QColor>(), deleted);
        QCOMPARE(colour(Action::Replaced | Action::Merged).value<QColor>(), renamed);

        // Everything else keeps the normal text colour
        QVERIFY(!colour(Action::Unversioned).isValid());
        QVERIFY(!colour(Action::Ignore).isValid());
        QVERIFY(!colour(Action::AssumeValid).isValid());

        // Gray comes before all of them
        GitStatusListEntry gray = makeEntry(QStringLiteral("a.txt"), Action::Unmerged);
        gray.isGrayed = true;
        model.setEntries({gray});
        QCOMPARE(model.entryIndex(0).data(Qt::ForegroundRole).value<QBrush>(), QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text));

        // A header isn't coloured as an entry
        model.setEntries({makeEntry(QStringLiteral("e.md"), Action::Unversioned)});
        QCOMPARE(model.index(0, 0).data(Qt::ForegroundRole).value<QBrush>(), QGuiApplication::palette().brush(QPalette::Text));
    }

private:
    /**
     * Checks that the list shows old.txt's rename to new.txt as one entry.
     * @param ctrl The list.
     */
    static void verifyShowsRename(GitStatusListCtrl& ctrl)
    {
        const GitStatusListModel& model = *ctrl.statusModel();
        QCOMPARE(listedPaths(model), QStringList{QStringLiteral("new.txt")});
        QCOMPARE(cell(model, QStringLiteral("new.txt"), GitStatusListModel::PathColumn), QStringLiteral("new.txt (from old.txt)"));
        QCOMPARE(cell(model, QStringLiteral("new.txt"), GitStatusListModel::StatusColumn), GitStatusListModel::actionName(TGitPath::Action::Replaced));
    }

    /**
     * @param repo A repository.
     * @param path A path relative to its root.
     * @returns Its line of git ls-files --stage, which starts with its mode.
     */
    [[nodiscard]] static QByteArray indexEntry(const TestRepo& repo, const QString& path)
    {
        return Git(repo.path()).run({QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("--"), path}, RunMode::ReadOnly);
    }

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
};

QTEST_MAIN(GitStatusListCtrlTest)

#include "gitstatuslistctrltest.moc"
