// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/command.hpp"
#include "commitdlg.hpp"
#include "gitstatuslistctrl.hpp"
#include "progresscommands/revertprogresscommand.hpp"
#include "revertdlg.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QAbstractButton>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <functional>
#include <optional>

namespace
{

using namespace kgt;

/**
 * @param repo The repository.
 * @returns Its changes, as the status list sees them.
 */
QList<GitStatusListEntry> changes(const TestRepo& repo)
{
    return GitStatusListModel::makeEntries(GitStatus::status(repo.git()), {});
}

/**
 * @param repo The repository.
 * @param paths Paths relative to its root.
 * @returns The changes to those paths.
 */
QList<GitStatusListEntry> changesTo(const TestRepo& repo, const QStringList& paths)
{
    QList<GitStatusListEntry> entries = changes(repo);
    entries.removeIf([&paths](const GitStatusListEntry& entry) {
        return !paths.contains(entry.path);
    });
    return entries;
}

/**
 * Reverts some changes with RevertProgressCommand and waits for it.
 * @param repo The repository.
 * @param entries The changes.
 * @returns The command's error, if any.
 */
std::optional<Error> revert(const TestRepo& repo, const QList<GitStatusListEntry>& entries)
{
    RevertProgressCommand command;
    command.setPathList(entries);
    QSignalSpy finished(&command, &RevertProgressCommand::finished);
    command.run(Git::workingTreeRoot(repo.path()));
    if (finished.isEmpty() && !finished.wait(10000)) {
        return Error{.category = ErrorCategory::Io, .message = QStringLiteral("Timed out"), .location = std::source_location::current()};
    }
    return command.error();
}

/**
 * @param repo The repository.
 * @returns git status --porcelain, one line per path, sorted.
 */
QStringList shortStatus(const TestRepo& repo)
{
    QStringList lines = QString::fromUtf8(repo.git().run({QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("--untracked-files=all")}))
                            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines.sort();
    return lines;
}

/**
 * @param repo The repository.
 * @param path Relative to its root.
 * @returns The file's content, or "<missing>".
 */
QByteArray content(const TestRepo& repo, const QString& path)
{
    QFile file(repo.filePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return "<missing>";
    }
    return file.readAll();
}

/**
 * A repository with a.txt, b.txt and dir/c.txt committed, each holding "original\n".
 */
std::unique_ptr<TestRepo> makeRepo()
{
    auto repo = std::make_unique<TestRepo>();
    repo->write(QStringLiteral("a.txt"), "original\n");
    repo->write(QStringLiteral("b.txt"), "original\n");
    repo->write(QStringLiteral("dir/c.txt"), "original\n");
    repo->commitAll(QStringLiteral("initial"));
    return repo;
}

/**
 * @param list The list.
 * @returns Whether it has finished loading.
 */
bool waitForList(const GitStatusListCtrl *list)
{
    if (!list->isBusy()) {
        return true;
    }
    QSignalSpy loaded(list, &GitStatusListCtrl::loaded);
    return loaded.wait(10000);
}

/**
 * @param list The list.
 * @returns The paths it lists, sorted.
 */
QStringList listedPaths(const GitStatusListCtrl *list)
{
    QStringList paths;
    const GitStatusListModel *model = list->statusModel();
    for (int row = 0; row < model->listedCount(); ++row) {
        paths.append(model->entry(row).path);
    }
    paths.sort();
    return paths;
}

/**
 * @param list The list.
 * @returns The paths it has checked, sorted.
 */
QStringList checkedPaths(const GitStatusListCtrl *list)
{
    QStringList paths;
    for (const GitStatusListEntry& entry : list->checkedEntries()) {
        paths.append(entry.path);
    }
    paths.sort();
    return paths;
}

/**
 * Selects some listed files, the first one current, as clicking them does.
 * @param list The list.
 * @param paths The files.
 */
void select(const GitStatusListCtrl *list, const QStringList& paths)
{
    list->selectionModel()->clearSelection();
    const GitStatusListModel *model = list->statusModel();
    const auto *proxy = qobject_cast<const QAbstractProxyModel *>(list->model());
    for (int row = 0; row < model->listedCount(); ++row) {
        const QModelIndex index = proxy->mapFromSource(model->entryIndex(row));
        const int position = static_cast<int>(paths.indexOf(model->entry(row).path));
        if (position < 0) {
            continue;
        }
        const auto flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
        if (position == 0) {
            list->selectionModel()->setCurrentIndex(index, flags);
        } else {
            list->selectionModel()->select(index, flags);
        }
    }
}

/**
 * Opens the list's context menu and hands it to @p handle, which must close it.
 * @param list The list.
 * @param handle Called with the open menu.
 * @returns Whether a menu opened.
 */
bool withContextMenu(const GitStatusListCtrl *list, const std::function<void(QMenu *)>& handle)
{
    bool opened = false;
    QTimer::singleShot(0, list, [&opened, &handle] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu) {
            opened = true;
            handle(menu);
            if (menu->isVisible()) {
                menu->close();
            }
        }
    });
    const QPoint position = list->viewport()->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, list->viewport()->mapToGlobal(position));
    QApplication::sendEvent(list->viewport(), &event);
    return opened;
}

/**
 * @param menu A menu.
 * @param text An entry's text.
 * @returns The entry, or nullptr.
 */
QAction *findAction(const QMenu *menu, const QString& text)
{
    const QList<QAction *> actions = menu->actions();
    const auto found = std::ranges::find_if(actions, [&text](const QAction *action) {
        return action->text() == text;
    });
    return found == actions.cend() ? nullptr : *found;
}

/**
 * @param menu A menu.
 * @returns The texts of its entries, separators left out.
 */
QStringList actionTexts(const QMenu *menu)
{
    QStringList texts;
    for (const QAction *action : menu->actions()) {
        if (!action->isSeparator()) {
            texts.append(action->text());
        }
    }
    return texts;
}

/**
 * @param list The list.
 * @returns The texts of its context menu, separators left out.
 */
QStringList menuTexts(const GitStatusListCtrl *list)
{
    QStringList texts;
    std::ignore = withContextMenu(list, [&texts](const QMenu *menu) {
        texts = actionTexts(menu);
    });
    return texts;
}

/**
 * Answers the next question box, recording its text.
 */
class QuestionAnswerer : public QObject
{
public:
    /**
     * @param answer The button to press.
     */
    explicit QuestionAnswerer(QMessageBox::StandardButton answer)
        : m_answer(answer)
    {
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box && box->icon() == QMessageBox::Question) {
                m_text = box->text();
                box->button(m_answer)->click();
                m_timer.stop();
            }
        });
        m_timer.start(10);
    }

    /**
     * @returns The question asked, or empty if none was.
     */
    [[nodiscard]] const QString& text() const
    {
        return m_text;
    }

private:
    QTimer m_timer;
    QMessageBox::StandardButton m_answer;
    QString m_text;
};

/**
 * Picks "Revert..." from the list's context menu for the selected files and waits for the revert and the reload.
 * @param list The list.
 * @returns Whether the revert ran.
 */
bool revertFromMenu(const GitStatusListCtrl *list)
{
    QSignalSpy reverted(list, &GitStatusListCtrl::reverted);
    bool triggered = false;
    std::ignore = withContextMenu(list, [&triggered](QMenu *menu) {
        if (QAction *action = findAction(menu, QStringLiteral("Re&vert..."))) {
            triggered = true;
            action->trigger();
        }
    });
    if (!triggered || !reverted.wait(10000)) {
        return false;
    }
    return waitForList(list);
}

/**
 * @param shot A screenshot.
 * @param name The file name.
 */
void saveScreenshot(const QPixmap& shot, const QString& name)
{
    QVERIFY(!shot.isNull());
    const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(shot.save(QDir(screenshotDir).absoluteFilePath(name)));
    }
}

/**
 * @param dlg The dialog.
 * @returns Its OK button.
 */
QPushButton *okButton(const RevertDlg& dlg)
{
    const auto *buttons = dlg.findChild<QDialogButtonBox *>();
    Q_ASSERT(buttons);
    return buttons->button(QDialogButtonBox::Ok);
}

} // namespace

/**
 * Reverting local changes: RevertProgressCommand for each kind of change, the status list's Revert entry (in the
 * commit dialog too) and RevertDlg, on real repositories. Set KGT_SCREENSHOT_DIR to save screenshots there.
 */
class RevertTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void revertsModifiedFile()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        QCOMPARE(revert(*repo, changes(*repo)), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void revertsStagedFile()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "staged\n");
        repo->run({QStringLiteral("add"), QStringLiteral("a.txt")});
        repo->write(QStringLiteral("a.txt"), "staged and changed again\n");
        QCOMPARE(revert(*repo, changes(*repo)), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void revertingAddedFileKeepsItOnDisk()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("new.txt"), "new\n");
        repo->run({QStringLiteral("add"), QStringLiteral("new.txt")});
        QCOMPARE(revert(*repo, changes(*repo)), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("new.txt")), QByteArray("new\n"));
        QCOMPARE(shortStatus(*repo), QStringList{QStringLiteral("?? new.txt")});
    }

    void revertsDeletedFile()
    {
        const auto repo = makeRepo();
        // Deleted from disk only, and deleted with git rm
        repo->remove(QStringLiteral("a.txt"));
        repo->run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("b.txt")});
        QCOMPARE(shortStatus(*repo), (QStringList{QStringLiteral(" D a.txt"), QStringLiteral("D  b.txt")}));
        QCOMPARE(revert(*repo, changes(*repo)), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(content(*repo, QStringLiteral("b.txt")), QByteArray("original\n"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void revertsRenamedFile()
    {
        const auto repo = makeRepo();
        repo->run({QStringLiteral("mv"), QStringLiteral("a.txt"), QStringLiteral("renamed.txt")});
        const QList<GitStatusListEntry> entries = changes(*repo);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.constFirst().origPath, QStringLiteral("a.txt"));
        QCOMPARE(revert(*repo, entries), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        // git rm takes the new name out of the index, and off the disk
        QCOMPARE(content(*repo, QStringLiteral("renamed.txt")), QByteArray("<missing>"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void revertsRenamedFileWhoseNewNameIsGone()
    {
        const auto repo = makeRepo();
        repo->run({QStringLiteral("mv"), QStringLiteral("a.txt"), QStringLiteral("renamed.txt")});
        repo->remove(QStringLiteral("renamed.txt"));
        QCOMPARE(revert(*repo, changes(*repo)), std::nullopt);
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void refusesRenameOntoDirectory()
    {
        const auto repo = makeRepo();
        repo->run({QStringLiteral("mv"), QStringLiteral("a.txt"), QStringLiteral("renamed.txt")});
        QVERIFY(QDir(repo->path()).mkdir(QStringLiteral("a.txt")));
        const std::optional<Error> error = revert(*repo, changes(*repo));
        QVERIFY(error.has_value());
        const QString message = error.value_or(Error{}).message;
        QVERIFY2(message.startsWith(QStringLiteral("Revert failed:\nCannot revert renaming of \"renamed.txt\".")), qPrintable(message));
        // Nothing was touched
        QCOMPARE(content(*repo, QStringLiteral("renamed.txt")), QByteArray("original\n"));
    }

    void revertsConflictedFile()
    {
        const auto repo = makeRepo();
        repo->run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("other")});
        repo->write(QStringLiteral("a.txt"), "theirs\n");
        repo->commitAll(QStringLiteral("theirs"));
        repo->run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo->write(QStringLiteral("a.txt"), "ours\n");
        repo->commitAll(QStringLiteral("ours"));
        // The merge stops at the conflict
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = repo->git().run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("other")}));
        QCOMPARE(shortStatus(*repo), QStringList{QStringLiteral("UU a.txt")});

        const QList<GitStatusListEntry> entries = changes(*repo);
        QVERIFY(entries.constFirst().actions & TGitPath::Action::Unmerged);
        QCOMPARE(revert(*repo, entries), std::nullopt);
        // HEAD's side, and resolved
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("ours\n"));
        QCOMPARE(shortStatus(*repo), QStringList());
    }

    void revertsOnlyTheGivenFiles()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral("b.txt"), "changed\n");
        QCOMPARE(revert(*repo, changesTo(*repo, {QStringLiteral("a.txt")})), std::nullopt);
        QCOMPARE(shortStatus(*repo), QStringList{QStringLiteral(" M b.txt")});
    }

    void reportsGitsError()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral(".git/index.lock"), "");
        const std::optional<Error> error = revert(*repo, changes(*repo));
        QVERIFY(error.has_value());
        const QString message = error.value_or(Error{}).message;
        QVERIFY2(message.contains(QStringLiteral("index.lock")), qPrintable(message));
    }

    void listOffersRevertForTrackedChanges()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral("new.txt"), "new\n");
        GitStatusListCtrl list;
        list.setWorkingTree(Git::workingTreeRoot(repo->path()));
        list.setShowFlags(GitStatusListModel::Show::All, GitStatusListModel::Show::Versioned);
        list.resize(400, 300);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        list.reload();
        QVERIFY(waitForList(&list));

        select(&list, {QStringLiteral("a.txt")});
        QVERIFY(menuTexts(&list).contains(QStringLiteral("Re&vert...")));
        // Decided by the entry the menu is opened on, as in TortoiseGit
        select(&list, {QStringLiteral("new.txt")});
        QVERIFY(!menuTexts(&list).contains(QStringLiteral("Re&vert...")));
        select(&list, {QStringLiteral("new.txt"), QStringLiteral("a.txt")});
        QVERIFY(!menuTexts(&list).contains(QStringLiteral("Re&vert...")));

        // Not when the dialog turns it off (GITSLC_POPREVERT)
        select(&list, {QStringLiteral("a.txt")});
        list.setContextMenus(GitStatusListCtrl::ContextMenu::All & ~GitStatusListCtrl::ContextMenus(GitStatusListCtrl::ContextMenu::Revert));
        QVERIFY(!menuTexts(&list).contains(QStringLiteral("Re&vert...")));
        list.setContextMenus(GitStatusListCtrl::ContextMenu::All);

        // Nor for a commit's files, as the log dialog shows them
        list.setCurrentVersion(QStringLiteral("0123456789abcdef"), {});
        list.updateWithGitPathList(changes(*repo));
        select(&list, {QStringLiteral("a.txt")});
        QVERIFY(!menuTexts(&list).contains(QStringLiteral("Re&vert...")));
    }

    void listAsksBeforeRevertingModifiedFiles()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral("b.txt"), "changed\n");
        GitStatusListCtrl list;
        list.setWorkingTree(Git::workingTreeRoot(repo->path()));
        list.setShowFlags(GitStatusListModel::Show::All, GitStatusListModel::Show::Versioned);
        list.resize(400, 300);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        list.reload();
        QVERIFY(waitForList(&list));
        select(&list, {QStringLiteral("a.txt"), QStringLiteral("b.txt")});

        // No: nothing happens
        const QuestionAnswerer refuse(QMessageBox::No);
        const QSignalSpy reverted(&list, &GitStatusListCtrl::reverted);
        std::ignore = withContextMenu(&list, [](const QMenu *menu) {
            findAction(menu, QStringLiteral("Re&vert..."))->trigger();
        });
        const bool asked = QTest::qWaitFor([&refuse] {
            return !refuse.text().isEmpty();
        });
        QVERIFY(asked);
        QCOMPARE(refuse.text(), QStringLiteral("Are you sure you want to revert 2 item(s)? You will lose ALL changes since the last update!"));
        QTest::qWait(50);
        QVERIFY(reverted.isEmpty());
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("changed\n"));

        // Yes: reverted, and the list reloads without them
        const QuestionAnswerer accept(QMessageBox::Yes);
        QVERIFY(revertFromMenu(&list));
        QVERIFY(!accept.text().isEmpty());
        QCOMPARE(shortStatus(*repo), QStringList());
        QCOMPARE(listedPaths(&list), QStringList());
    }

    void listRevertsAddedFilesWithoutAsking()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("new.txt"), "new\n");
        repo->run({QStringLiteral("add"), QStringLiteral("new.txt")});
        GitStatusListCtrl list;
        list.setWorkingTree(Git::workingTreeRoot(repo->path()));
        list.setShowFlags(GitStatusListModel::Show::All, GitStatusListModel::Show::Versioned);
        list.resize(400, 300);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        list.reload();
        QVERIFY(waitForList(&list));
        select(&list, {QStringLiteral("new.txt")});

        const QuestionAnswerer answerer(QMessageBox::No);
        QVERIFY(revertFromMenu(&list));
        QVERIFY(answerer.text().isEmpty());
        QCOMPARE(shortStatus(*repo), QStringList{QStringLiteral("?? new.txt")});
        // Listed again, now unversioned
        QCOMPARE(listedPaths(&list), QStringList{QStringLiteral("new.txt")});
        QCOMPARE(list.statusModel()->entry(0).actions, TGitPath::Actions(TGitPath::Action::Unversioned));
    }

    void commitDialogOffersRevertAndReloads()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral("b.txt"), "changed\n");
        repo->write(QStringLiteral("new.txt"), "new\n");

        CommitDlg dlg({repo->path()});
        const GitStatusListCtrl *list = dlg.fileList();
        QVERIFY(waitForList(list));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        select(list, {QStringLiteral("a.txt")});

        QStringList texts;
        QVERIFY(withContextMenu(list, [&dlg, &texts](QMenu *menu) {
            texts = actionTexts(menu);
            // The dialog with its menu open
            QPixmap shot = dlg.grab();
            QPainter painter(&shot);
            painter.drawPixmap(dlg.mapFromGlobal(menu->pos()), menu->grab());
            painter.end();
            saveScreenshot(shot, QStringLiteral("commitdlg-revert-menu.png"));
        }));
        QCOMPARE(texts,
                 (QStringList{QStringLiteral("Compare with b&ase"),
                              QStringLiteral("Re&vert..."),
                              QStringLiteral("&Open"),
                              QStringLiteral("Open with..."),
                              QStringLiteral("Explore to")}));

        const QuestionAnswerer answerer(QMessageBox::Yes);
        QVERIFY(revertFromMenu(list));
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(listedPaths(list), (QStringList{QStringLiteral("b.txt"), QStringLiteral("new.txt")}));
    }

    void dialogChecksDirectFilesAndAddedOnes()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->write(QStringLiteral("dir/c.txt"), "changed\n");
        repo->write(QStringLiteral("dir/new.txt"), "new\n");
        repo->run({QStringLiteral("add"), QStringLiteral("dir/new.txt")});
        repo->write(QStringLiteral("unversioned.txt"), "new\n");
        const QString root = Git::workingTreeRoot(repo->path());

        {
            // A folder: what is inside it is listed, only the added file checked (CRevertDlg::RevertThread)
            const RevertDlg dlg(root, {repo->path()});
            QVERIFY(dlg.windowTitle().endsWith(QStringLiteral(" - Revert - KortoiseGit")));
            QVERIFY(waitForList(dlg.listCtrl()));
            QCOMPARE(listedPaths(dlg.listCtrl()), (QStringList{QStringLiteral("a.txt"), QStringLiteral("dir/c.txt"), QStringLiteral("dir/new.txt")}));
            QCOMPARE(checkedPaths(dlg.listCtrl()), QStringList{QStringLiteral("dir/new.txt")});
        }
        {
            // Files picked directly start checked
            const RevertDlg dlg(root, {repo->filePath(QStringLiteral("a.txt")), repo->filePath(QStringLiteral("dir"))});
            QVERIFY(waitForList(dlg.listCtrl()));
            QCOMPARE(checkedPaths(dlg.listCtrl()), (QStringList{QStringLiteral("a.txt"), QStringLiteral("dir/new.txt")}));
        }
    }

    void dialogRevertsCheckedFiles()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        repo->remove(QStringLiteral("b.txt"));
        repo->run({QStringLiteral("mv"), QStringLiteral("dir/c.txt"), QStringLiteral("dir/moved.txt")});
        repo->write(QStringLiteral("new.txt"), "new\n");
        repo->run({QStringLiteral("add"), QStringLiteral("new.txt")});
        repo->write(QStringLiteral("unversioned.txt"), "new\n");

        RevertDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg.listCtrl()));
        dlg.listCtrl()->statusModel()->setAllChecked(true);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        saveScreenshot(dlg.grab(), QStringLiteral("revertdlg.png"));

        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::mouseClick(okButton(dlg), Qt::LeftButton);
        QVERIFY(accepted.wait(10000));
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
        QCOMPARE(content(*repo, QStringLiteral("b.txt")), QByteArray("original\n"));
        QCOMPARE(content(*repo, QStringLiteral("dir/c.txt")), QByteArray("original\n"));
        QCOMPARE(content(*repo, QStringLiteral("new.txt")), QByteArray("new\n"));
        QCOMPARE(shortStatus(*repo), (QStringList{QStringLiteral("?? new.txt"), QStringLiteral("?? unversioned.txt")}));
    }

    void dialogNeedsSomethingChecked()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        RevertDlg dlg(Git::workingTreeRoot(repo->path()), {repo->path()});
        QVERIFY(waitForList(dlg.listCtrl()));
        QVERIFY(checkedPaths(dlg.listCtrl()).isEmpty());
        QVERIFY(!okButton(dlg)->isEnabled());
        dlg.accept();
        QVERIFY(!dlg.isReverting());
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("changed\n"));
    }

    void dialogShowsGitsError()
    {
        const auto repo = makeRepo();
        repo->write(QStringLiteral("a.txt"), "changed\n");
        RevertDlg dlg(Git::workingTreeRoot(repo->path()), {repo->filePath(QStringLiteral("a.txt"))});
        QVERIFY(waitForList(dlg.listCtrl()));
        repo->write(QStringLiteral(".git/index.lock"), "");

        QSignalSpy failed(&dlg, &RevertDlg::revertFailed);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        dlg.accept();
        QVERIFY(failed.wait(10000));
        QVERIFY(accepted.isEmpty());
        QVERIFY(!dlg.isReverting());
        auto *box = dlg.findChild<QMessageBox *>();
        QVERIFY(box);
        QCOMPARE(box->icon(), QMessageBox::Critical);
        box->close();

        repo->remove(QStringLiteral(".git/index.lock"));
        QVERIFY(waitForList(dlg.listCtrl()));
        dlg.accept();
        QVERIFY(accepted.wait(10000));
        QCOMPARE(content(*repo, QStringLiteral("a.txt")), QByteArray("original\n"));
    }

    void commandIsRegistered()
    {
        const Result<std::unique_ptr<Command>> command = CommandServer::command(QStringLiteral("revert"));
        QVERIFY(command);
        QCOMPARE((*command)->pathRequirement(), PathRequirement::WorkingTreeRequired);
    }
};

QTEST_MAIN(RevertTest)

#include "reverttest.moc"
