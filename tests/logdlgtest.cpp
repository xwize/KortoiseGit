// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlg.hpp"
#include "chooseversion.hpp"
#include "gitloglist.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "progressdlg.hpp"
#include "pushdlg.hpp"
#include "resetdlg.hpp"
#include "testrepo.hpp"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QBrush>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <tuple>
#include <vector>

namespace
{

using namespace kgt;

constexpr int timeout = 10000;
constexpr int commitCount = 150; // Commits that change dir/f.txt; one more changes other.txt

/**
 * Fills a repository: commitCount commits to dir/f.txt, then one adding other.txt.
 * @param repo An empty repository.
 */
void makeHistory(const TestRepo& repo)
{
    for (int i = 0; i < commitCount; ++i) {
        repo.write(QStringLiteral("dir/f.txt"), QByteArray::number(i));
        repo.commitAll(QStringLiteral("Commit %1").arg(i));
    }
    repo.write(QStringLiteral("other.txt"), "x");
    repo.commitAll(QStringLiteral("Other"));
}

/**
 * @param dlg The dialog, just created.
 * @returns Whether its log loaded.
 */
bool waitForLog(const LogDlg& dlg)
{
    if (!dlg.logList()->isBusy()) {
        return true;
    }
    QSignalSpy loaded(dlg.logList(), &GitLogList::loaded);
    return loaded.wait(timeout);
}

/**
 * Waits for the load a change in the dialog has just started.
 * @param dlg The dialog.
 * @returns Whether its log loaded.
 */
bool waitForReload(const LogDlg& dlg)
{
    return QTest::qWaitFor(
        [&dlg] {
            return !dlg.logList()->isBusy();
        },
        timeout);
}

/**
 * Waits for the load a date picker starts after LOGFTIME_TIMER's short delay.
 * @param dlg The dialog, with a date just changed.
 * @returns Whether the load started and ended.
 */
bool waitForDateReload(const LogDlg& dlg)
{
    QSignalSpy loaded(dlg.logList(), &GitLogList::loaded);
    return loaded.wait(timeout);
}

/**
 * @param dlg The dialog.
 * @param row The row of the commit to select.
 */
void selectRow(const LogDlg& dlg, int row)
{
    dlg.logList()->setCurrentIndex(dlg.logList()->logModel()->index(row, GitLogListModel::MessageColumn));
}

/**
 * Types a filter and applies it at once, as Enter does.
 * @param dlg The dialog.
 * @param text The filter.
 */
void applyFilter(const LogDlg& dlg, const QString& text)
{
    dlg.searchEdit()->setText(text);
    QTest::keyClick(dlg.searchEdit(), Qt::Key_Return);
}

/**
 * @param menu A menu.
 * @param text An entry's text, without its mnemonic.
 * @returns The entry, searched for in submenus too, or nullptr.
 */
QAction *findAction(const QMenu *menu, const QString& text)
{
    for (QAction *action : menu->actions()) {
        if (action->text().remove(QLatin1Char('&')) == text) {
            return action;
        }
        if (action->menu()) {
            if (QAction *found = findAction(action->menu(), text)) {
                return found;
            }
        }
    }
    return nullptr;
}

/**
 * @param model The log.
 * @returns The subjects listed, top to bottom.
 */
QStringList subjects(const GitLogListModel& model)
{
    QStringList list;
    for (int row = 0; row < model.rowCount(); ++row) {
        list.append(model.rev(row).subject);
    }
    return list;
}

/**
 * Removes what the log dialog saved, so a test starts from TortoiseGit's defaults.
 */
void clearSettings()
{
    const KSharedConfigPtr config = KSharedConfig::openConfig();
    config->deleteGroup(QStringLiteral("General"));
    config->deleteGroup(QStringLiteral("LogDialog"));
    config->sync();
}

/**
 * @param dlg The dialog.
 * @param button The button its menu opens from.
 * @param menu The menu.
 * @param name The file to save, in the screenshot directory.
 * @returns Whether the picture of the dialog with the menu over it was saved; true if no directory was asked for.
 */
bool saveMenuScreenshot(LogDlg& dlg, const QPushButton *button, QMenu *menu, const QString& name)
{
    const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
    if (screenshotDir.isEmpty()) {
        return true;
    }
    QPixmap shot = dlg.grab();
    const QPixmap menuShot = menu->grab();
    QPainter painter(&shot);
    painter.drawPixmap(button->mapTo(&dlg, QPoint(0, -menuShot.height())), menuShot);
    painter.end();
    return shot.save(QDir(screenshotDir).absoluteFilePath(name));
}

/**
 * Commits a change with a fixed commit date.
 * @param repo The repository.
 * @param date The date.
 * @param message The commit message.
 */
void commitOn(const TestRepo& repo, const QDate& date, const QString& message)
{
    const QByteArray stamp = QDateTime(date, QTime(12, 0)).toString(Qt::ISODate).toLatin1();
    qputenv("GIT_COMMITTER_DATE", stamp);
    qputenv("GIT_AUTHOR_DATE", stamp);
    repo.write(QStringLiteral("dated.txt"), message.toUtf8());
    repo.commitAll(message);
    qunsetenv("GIT_COMMITTER_DATE");
    qunsetenv("GIT_AUTHOR_DATE");
}

/**
 * Fills a repository with three commits on known dates.
 * @param repo An empty repository.
 */
void makeDatedHistory(const TestRepo& repo)
{
    commitOn(repo, QDate(2020, 3, 10), QStringLiteral("Early"));
    commitOn(repo, QDate(2021, 6, 15), QStringLiteral("Middle"));
    commitOn(repo, QDate(2023, 9, 20), QStringLiteral("Late"));
}

/**
 * @param model The log.
 * @param row A commit's row.
 * @returns Whether its files arrived.
 */
bool waitForRevFiles(const GitLogListModel& model, int row)
{
    return QTest::qWaitFor(
        [&model, row] {
            return model.rev(row).filesState != GitRevLoglist::FilesState::NotLoaded;
        },
        timeout);
}

/**
 * @param dlg The dialog, with a commit just selected.
 * @returns Whether its changed files arrived.
 */
bool waitForFiles(const LogDlg& dlg)
{
    return QTest::qWaitFor(
        [&dlg] {
            return !dlg.changedFileList()->isBusy();
        },
        timeout);
}

/**
 * Fills a repository with a few branches and merges for the graph: a feature branch merged twice into main, a
 * second branch merged across it.
 * @param repo An empty repository.
 */
void makeBranchyHistory(const TestRepo& repo)
{
    const auto git = [&repo](std::initializer_list<const char *> arguments) {
        QStringList list;
        for (const char *argument : arguments) {
            list.append(QString::fromLatin1(argument));
        }
        repo.run(list);
    };
    int counter = 0;
    const auto commit = [&repo, &counter](const QString& message) {
        repo.write(QStringLiteral("f%1.txt").arg(counter++), "x");
        repo.commitAll(message);
    };
    commit(QStringLiteral("Root"));
    commit(QStringLiteral("Second"));
    git({"switch", "--quiet", "-c", "feature"});
    commit(QStringLiteral("Feature 1"));
    git({"switch", "--quiet", "main"});
    commit(QStringLiteral("Main 1"));
    git({"switch", "--quiet", "-c", "topic"});
    commit(QStringLiteral("Topic 1"));
    git({"switch", "--quiet", "feature"});
    commit(QStringLiteral("Feature 2"));
    git({"switch", "--quiet", "main"});
    git({"merge", "--quiet", "--no-ff", "-m", "Merge feature", "feature"});
    git({"switch", "--quiet", "topic"});
    commit(QStringLiteral("Topic 2"));
    git({"switch", "--quiet", "feature"});
    commit(QStringLiteral("Feature 3"));
    git({"switch", "--quiet", "main"});
    git({"merge", "--quiet", "--no-ff", "-m", "Merge topic", "topic"});
    commit(QStringLiteral("Main 2"));
    git({"merge", "--quiet", "--no-ff", "-m", "Merge feature again", "feature"});
}

/**
 * Fills a repository with nine branches that fork from the root and merge back into main one by one, so the graph
 * has enough lanes to show every lane colour.
 * @param repo An empty repository.
 */
void makeWideHistory(const TestRepo& repo)
{
    constexpr int branches = 9;
    repo.write(QStringLiteral("root.txt"), "x");
    repo.commitAll(QStringLiteral("Root"));
    for (int i = 0; i < branches; ++i) {
        const QString branch = QStringLiteral("b%1").arg(i);
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), branch, QStringLiteral("main")});
        repo.write(QStringLiteral("%1.txt").arg(branch), "x");
        repo.commitAll(QStringLiteral("Branch %1").arg(i));
    }
    repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
    for (int i = 0; i < branches; ++i) {
        const QString branch = QStringLiteral("b%1").arg(i);
        repo.run({QStringLiteral("merge"),
                  QStringLiteral("--quiet"),
                  QStringLiteral("--no-ff"),
                  QStringLiteral("-m"),
                  QStringLiteral("Merge %1").arg(branch),
                  branch});
    }
}

/**
 * @returns A dark palette like Breeze Dark's.
 */
QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(32, 35, 38));
    palette.setColor(QPalette::WindowText, QColor(252, 252, 252));
    palette.setColor(QPalette::Base, QColor(20, 22, 24));
    palette.setColor(QPalette::AlternateBase, QColor(29, 31, 34));
    palette.setColor(QPalette::Text, QColor(252, 252, 252));
    palette.setColor(QPalette::Button, QColor(41, 44, 48));
    palette.setColor(QPalette::ButtonText, QColor(252, 252, 252));
    palette.setColor(QPalette::Highlight, QColor(61, 174, 233));
    palette.setColor(QPalette::HighlightedText, QColor(252, 252, 252));
    return palette;
}

/**
 * @param image A picture of the log list's viewport.
 * @param rect The Graph column's cells.
 * @param background The list's background colour.
 * @returns How many pixels of the column differ from the background.
 */
int graphPixels(const QImage& image, const QRect& rect, const QColor& background)
{
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            if (image.pixelColor(x, y) != background) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

/**
 * The log dialog on a throwaway repository. Set KGT_SCREENSHOT_DIR to save a screenshot of it there.
 */
/**
 * Answers the message boxes that come up, in turn.
 */
class BoxAnswerer
{
public:
    /**
     * @param answers The button to press in each box.
     */
    explicit BoxAnswerer(const QList<QDialogButtonBox::StandardButton>& answers)
        : m_answers(answers)
    {
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] {
            const auto *box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            const auto *buttons = box ? box->findChild<QDialogButtonBox *>() : nullptr;
            if (buttons && !m_answers.isEmpty()) {
                buttons->button(m_answers.takeFirst())->click();
            }
        });
        m_timer.start(pollInterval);
    }

private:
    static constexpr int pollInterval = 10;

    QList<QDialogButtonBox::StandardButton> m_answers;
    QTimer m_timer;
};

using MenuHandler = std::function<void(QMenu *)>; // Called with an open context menu

/**
 * Selects a commit, right-clicks it, hands the open menu to @p handle and closes the menu.
 * @param list The list.
 * @param row The commit's row.
 * @param handle Called with the open menu.
 * @param position Where to click, in the viewport; by default on the commit's Author cell, away from any label.
 * @returns Whether a menu opened.
 */
bool withMenuOn(GitLogList *list, int row, const MenuHandler& handle, std::optional<QPoint> position = std::nullopt)
{
    list->setCurrentIndex(list->logModel()->index(row, GitLogListModel::MessageColumn));
    bool opened = false;
    QTimer::singleShot(0, list, [&opened, &handle] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu) {
            opened = true;
            handle(menu);
            menu->close();
        }
    });
    const QModelIndex author = list->logModel()->index(row, GitLogListModel::AuthorColumn);
    const QPoint pos = position.value_or(list->visualRect(author).center());
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, list->viewport()->mapToGlobal(pos));
    QApplication::sendEvent(list->viewport(), &event);
    return opened;
}

/**
 * Makes git difftool in a repository append a line to a file instead of opening a window: the last lines of both
 * sides, and for a directory diff of dir/a.txt, what the right-hand side's top level holds.
 */
class DiffRecorder
{
public:
    /**
     * @param repo The repository.
     */
    explicit DiffRecorder(const TestRepo& repo)
        : m_output(repo.filePath(QStringLiteral(".git/recorded-diffs")))
    {
        const QString command = QStringLiteral(
                                    "{ if [ -d \"$LOCAL\" ]; "
                                    "then tail -qn 1 \"$LOCAL/dir/a.txt\" \"$REMOTE/dir/a.txt\"; ls \"$REMOTE\"; "
                                    "else tail -qn 1 \"$LOCAL\" \"$REMOTE\"; fi; } | paste -sd ' ' >> '%1'")
                                    .arg(m_output);
        repo.run({QStringLiteral("config"), QStringLiteral("diff.tool"), QStringLiteral("record")});
        repo.run({QStringLiteral("config"), QStringLiteral("difftool.record.cmd"), command});
    }

    /**
     * Waits for one diff to be recorded, and empties the record for the next.
     * @param expected What it should record.
     * @returns Whether it did.
     */
    [[nodiscard]] bool saw(const QString& expected) const
    {
        QString actual;
        const bool matched = QTest::qWaitFor(
            [&] {
                QFile file(m_output);
                actual = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
                return actual == expected;
            },
            timeout);
        if (!matched) {
            qWarning() << "Recorded" << actual << "instead of" << expected;
        }
        QFile::remove(m_output);
        return matched;
    }

private:
    QString m_output;
};

/**
 * @param menu A menu.
 * @returns The entries' texts, with "-" for a separator.
 */
QStringList entryTexts(const QMenu *menu)
{
    QStringList texts;
    for (const QAction *action : menu->actions()) {
        texts.append(action->isSeparator() ? QStringLiteral("-") : action->text());
    }
    return texts;
}

/**
 * @param menu A menu.
 * @param text An entry's text.
 * @returns The entry, or nullptr.
 */
QAction *entry(const QMenu *menu, const QString& text)
{
    for (QAction *action : menu->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

/**
 * @param list The list.
 * @param row A row.
 * @param shortName A label's text.
 * @returns A point on that label in the viewport, or nothing if the row has no such label.
 */
std::optional<QPoint> labelPoint(const GitLogList *list, int row, const QString& shortName)
{
    const QRect cell = list->visualRect(list->logModel()->index(row, GitLogListModel::MessageColumn));
    for (int x = cell.left(); x <= cell.right(); ++x) {
        const QPoint point(x, cell.center().y());
        if (const std::optional<GitRef> ref = list->refLabelAt(row, point); ref && ref->name == shortName) {
            return point;
        }
    }
    return std::nullopt;
}

/**
 * @param list The list.
 * @param row A row.
 * @returns The names of the commit's refs.
 */
QStringList refNames(const GitLogList *list, int row)
{
    QStringList names;
    for (const GitRef& ref : list->logModel()->rev(row).refs) {
        names.append(ref.fullName());
    }
    return names;
}

/**
 * @param repo A repository.
 * @param ref A ref.
 * @returns Whether it exists.
 */
bool refExists(const TestRepo& repo, const QString& ref)
{
    return repo.git().run({QStringLiteral("for-each-ref"), ref}, RunMode::ReadOnly).contains(ref.toUtf8());
}

/**
 * Lays the dialog out so its rows and labels have their places.
 * @param dlg The dialog.
 * @returns Whether it is on screen.
 */
bool showDialog(LogDlg& dlg)
{
    dlg.resize(1000, 600);
    dlg.show();
    return QTest::qWaitForWindowExposed(&dlg);
}

/**
 * A repository with a bare "origin" that has a branch feature on the second of two commits, also local.
 */
class RemoteSetup
{
public:
    RemoteSetup()
    {
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("First"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Second"));
        const QStringList init = QStringLiteral("init --quiet --bare").split(QLatin1Char(' '));
        std::ignore = Git(upstream.path()).run(init);
        repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), upstream.path()});
        repo.run({QStringLiteral("branch"), QStringLiteral("feature")});
        repo.run(QStringLiteral("push --quiet origin feature").split(QLatin1Char(' ')));
    }

    QTemporaryDir upstream;
    TestRepo repo;
};

class LogDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        TestRepo::isolateGit();
    }

    void init()
    {
        clearSettings();
    }

    void refreshClearsFilter()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Alpha"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Beta"));
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 2);

        applyFilter(dlg, QStringLiteral("alpha"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(*dlg.logList()->logModel()), QStringList{QStringLiteral("Alpha")});

        dlg.refreshButton()->click();
        QVERIFY(waitForReload(dlg));
        QVERIFY(dlg.searchEdit()->text().isEmpty());
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 2);

        // F5 does the same, wherever the focus is
        dlg.show();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));
        applyFilter(dlg, QStringLiteral("alpha"));
        QVERIFY(waitForReload(dlg));
        QTest::keyClick(&dlg, Qt::Key_F5);
        QVERIFY(waitForReload(dlg));
        QVERIFY(dlg.searchEdit()->text().isEmpty());
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 2);
    }

    void refreshKeepsSelection()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Alpha"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Beta"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        selectRow(dlg, 1);
        applyFilter(dlg, QStringLiteral("alpha"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(dlg.logList()->selectedRow(), 0);
        QCOMPARE(dlg.logList()->logModel()->rev(0).subject, QStringLiteral("Alpha"));
    }

    void delaysFilter()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Alpha"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Beta"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        dlg.searchEdit()->setText(QStringLiteral("alpha"));
        QTest::qWait(100);
        QCOMPARE(model.rowCount(), 2); // Not yet: LOGFILTER_TIMER runs for a second
        QVERIFY(QTest::qWaitFor(
            [&model] {
                return model.rowCount() == 1;
            },
            timeout));
        QVERIFY(waitForReload(dlg));
    }

    void emptyingSearchClearsFilterAtOnce()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Alpha"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Beta"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        applyFilter(dlg, QStringLiteral("alpha"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 1);
        dlg.searchEdit()->clear();
        QVERIFY(dlg.logList()->isBusy()); // No delay
        QVERIFY(waitForReload(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 2);
    }

    void filtersByBackgroundData()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("first.txt"), "1");
        repo.commitAll(QStringLiteral("One"));
        repo.run({QStringLiteral("tag"), QStringLiteral("-a"), QStringLiteral("-m"), QStringLiteral("Shipping build"), QStringLiteral("v1")});
        repo.write(QStringLiteral("dir/second.txt"), "1");
        repo.commitAll(QStringLiteral("Two"));
        repo.run({QStringLiteral("notes"), QStringLiteral("add"), QStringLiteral("-m"), QStringLiteral("Annotated later"), QStringLiteral("HEAD")});
        repo.write(QStringLiteral("third.txt"), "1");
        repo.commitAll(QStringLiteral("Three"));

        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();

        // Everything is selected by default, so the paths and the tag text are fetched in the background
        applyFilter(dlg, QStringLiteral("dir/second"));
        QVERIFY(dlg.logList()->isBusy());
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(model), QStringList{QStringLiteral("Two")});

        applyFilter(dlg, QStringLiteral("shipping"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(model), QStringList{QStringLiteral("One")});

        applyFilter(dlg, QStringLiteral("annotated"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(model), QStringList{QStringLiteral("Two")});

        // The paths are remembered: filtering again finds them without another diff-tree
        applyFilter(dlg, QStringLiteral("third.txt"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(model), QStringList{QStringLiteral("Three")});
    }

    void filterMenuChoosesFields()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("subject.txt"), "1");
        repo.commitAll(QStringLiteral("Subject"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        const QMenu *menu = dlg.filterMenu();
        QVERIFY(!findAction(menu, QStringLiteral("Bug-IDs")));

        applyFilter(dlg, QStringLiteral("subject.txt"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(model.rowCount(), 1);

        // Toggle turns everything off; Paths back on still finds it, Subject alone doesn't
        findAction(menu, QStringLiteral("Toggle filters"))->trigger();
        QVERIFY(!findAction(menu, QStringLiteral("Paths"))->isChecked());
        findAction(menu, QStringLiteral("Subject"))->trigger();
        QTest::keyClick(dlg.searchEdit(), Qt::Key_Return);
        QVERIFY(waitForReload(dlg));
        QCOMPARE(model.rowCount(), 0);

        findAction(menu, QStringLiteral("All"))->trigger();
        QVERIFY(findAction(menu, QStringLiteral("Paths"))->isChecked());
        QTest::keyClick(dlg.searchEdit(), Qt::Key_Return);
        QVERIFY(waitForReload(dlg));
        QCOMPARE(model.rowCount(), 1);

        // A regular expression that doesn't compile filters for nothing and isn't applied by the timer
        findAction(menu, QStringLiteral("Use regular expression"))->trigger();
        QTest::keyClick(dlg.searchEdit(), Qt::Key_Return);
        QVERIFY(waitForReload(dlg));
        applyFilter(dlg, QStringLiteral("(unclosed"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(dlg.searchEdit()->palette().color(QPalette::Base) != dlg.palette().color(QPalette::Base));
        QVERIFY(!dlg.searchEdit()->toolTip().isEmpty());
        applyFilter(dlg, QStringLiteral("subject\\.txt"));
        QVERIFY(waitForReload(dlg));
        QCOMPARE(model.rowCount(), 1);
    }

    void suppressesGraphWhileFiltering()
    {
        const TestRepo repo;
        makeBranchyHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        QVERIFY(model.isGraphShown());

        applyFilter(dlg, QStringLiteral("Merge"));
        QVERIFY(waitForReload(dlg));
        QVERIFY(model.rowCount() > 0);
        QVERIFY(!model.isGraphShown());

        dlg.refreshButton()->click();
        QVERIFY(waitForReload(dlg));
        QVERIFY(model.isGraphShown());
    }

    void walkBehaviorOptions()
    {
        const TestRepo repo;
        makeBranchyHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        const int allCommits = model.rowCount();
        QVERIFY(!dlg.walkBehaviorButton()->isChecked());
        const QMenu *menu = dlg.walkBehaviorMenu();

        const auto toggle = [&](const QString& text) {
            findAction(menu, text)->trigger();
            return waitForReload(dlg);
        };

        QVERIFY(toggle(QStringLiteral("First Parent")));
        QVERIFY(dlg.walkBehaviorButton()->isChecked());
        QVERIFY(dlg.logList()->walkOptions().firstParent);
        const int firstParentCommits = model.rowCount();
        QVERIFY(firstParentCommits < allCommits);
        QVERIFY(toggle(QStringLiteral("First Parent")));
        QVERIFY(!dlg.walkBehaviorButton()->isChecked());
        QCOMPARE(model.rowCount(), allCommits);

        QVERIFY(toggle(QStringLiteral("No merges")));
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(model.rev(row).parents.size() <= 1);
        }
        QVERIFY(toggle(QStringLiteral("No merges")));

        QVERIFY(toggle(QStringLiteral("Full history")));
        QVERIFY(dlg.logList()->walkOptions().fullHistory);
        QVERIFY(toggle(QStringLiteral("Full history")));
        QVERIFY(!dlg.walkBehaviorButton()->isChecked());
    }

    void compressedGraphAndLabeledCommits()
    {
        const TestRepo repo;
        makeBranchyHistory(repo);
        repo.run({QStringLiteral("tag"), QStringLiteral("marker"), QStringLiteral("HEAD~1")});
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        const int allCommits = model.rowCount();
        const QMenu *menu = dlg.walkBehaviorMenu();
        QAction *compressed = findAction(menu, QStringLiteral("Compressed Graph"));
        QAction *labeled = findAction(menu, QStringLiteral("Show labeled commits only"));

        compressed->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(dlg.walkBehaviorButton()->isChecked());
        QVERIFY(compressed->isChecked());
        const int compressedCommits = model.rowCount();
        QVERIFY(compressedCommits < allCommits);
        QVERIFY(model.isGraphShown());
        QVERIFY(!model.lanes(0).empty());
        const QStringList compressedList = subjects(model);
        QVERIFY(compressedList.contains(QStringLiteral("Merge feature")));
        QVERIFY(!compressedList.contains(QStringLiteral("Topic 1")));

        // The two exclude each other, and labeled commits only has no graph (no FILTERSHOW_MERGEPOINTS)
        labeled->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(!compressed->isChecked());
        QVERIFY(labeled->isChecked());
        QVERIFY(!model.isGraphShown());
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(!model.rev(row).refs.isEmpty());
        }

        // Hiding a kind of label takes its commits out of the labeled list
        const int labeledCommits = model.rowCount();
        findAction(dlg.viewMenu(), QStringLiteral("Tags"))->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(model.rowCount() < labeledCommits);

        labeled->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(!dlg.walkBehaviorButton()->isChecked());
        QCOMPARE(model.rowCount(), allCommits);
    }

    void followRenames()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("old.txt"), "the same content on every line\nthe same content\n");
        repo.commitAll(QStringLiteral("Add"));
        repo.run({QStringLiteral("mv"), QStringLiteral("old.txt"), QStringLiteral("new.txt")});
        repo.commitAll(QStringLiteral("Rename"));
        repo.write(QStringLiteral("dir/other.txt"), "1");
        repo.commitAll(QStringLiteral("Other"));

        const LogDlg folderDlg({repo.filePath(QStringLiteral("dir"))});
        QVERIFY(waitForLog(folderDlg));
        QVERIFY(!findAction(folderDlg.walkBehaviorMenu(), QStringLiteral("Follow renames"))->isEnabled());
        const LogDlg wholeDlg({repo.path()});
        QVERIFY(waitForLog(wholeDlg));
        QVERIFY(!findAction(wholeDlg.walkBehaviorMenu(), QStringLiteral("Follow renames"))->isEnabled());

        const LogDlg dlg({repo.filePath(QStringLiteral("new.txt"))});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!dlg.logList()->isColumnHidden(GitLogListModel::GraphColumn));
        QAction *follow = findAction(dlg.walkBehaviorMenu(), QStringLiteral("Follow renames"));
        QVERIFY(follow->isEnabled());

        follow->trigger();
        QVERIFY(waitForReload(dlg));
        QCOMPARE(subjects(model), (QStringList{QStringLiteral("Rename"), QStringLiteral("Add")}));
        QVERIFY(dlg.logList()->isColumnHidden(GitLogListModel::GraphColumn));
        QVERIFY(dlg.walkBehaviorButton()->isChecked());

        follow->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(!dlg.logList()->isColumnHidden(GitLogListModel::GraphColumn));
    }

    void viewMenuOmitsUnfinishedEntries()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Add"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(!findAction(dlg.viewMenu(), QStringLiteral("Gravatar")));
        QVERIFY(!findAction(dlg.viewMenu(), QStringLiteral("View Patch")));
        QVERIFY(findAction(dlg.viewMenu(), QStringLiteral("Hide Unrelated Changed Paths")));
        QVERIFY(findAction(dlg.viewMenu(), QStringLiteral("Other refs")));
    }

    void hidesOrGraysUnrelatedPaths()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("dir/inside.txt"), "1");
        repo.write(QStringLiteral("outside.txt"), "1");
        repo.commitAll(QStringLiteral("Both"));

        const LogDlg dlg({repo.filePath(QStringLiteral("dir"))});
        QVERIFY(waitForLog(dlg));
        selectRow(dlg, 0);
        QVERIFY(waitForFiles(dlg));
        const GitStatusListModel *files = dlg.changedFileList()->statusModel();

        const auto foreground = [files](const QString& path) -> QVariant {
            for (int row = 0; row < files->rowCount(); ++row) {
                const QModelIndex index = files->index(row, GitStatusListModel::PathColumn);
                if (index.data(GitStatusListModel::PathRole).toString() == path) {
                    return index.data(Qt::ForegroundRole);
                }
            }
            return QStringLiteral("absent");
        };

        // OnInitDialog starts with "unrelated paths" in gray
        QCOMPARE(files->rowCount(), 2);
        const QBrush gray = QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        QCOMPARE(foreground(QStringLiteral("outside.txt")).value<QBrush>(), gray);
        QVERIFY(foreground(QStringLiteral("dir/inside.txt")).value<QBrush>() != gray);

        QAction *hide = findAction(dlg.viewMenu(), QStringLiteral("Hide Unrelated Changed Paths"));
        hide->trigger();
        QVERIFY(waitForFiles(dlg));
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(foreground(QStringLiteral("outside.txt")).toString(), QStringLiteral("absent"));

        hide->trigger(); // Off again: neither hidden nor gray
        QVERIFY(waitForFiles(dlg));
        QCOMPARE(files->rowCount(), 2);
        QVERIFY(foreground(QStringLiteral("outside.txt")).value<QBrush>() != gray);
    }

    void labelsHiddenPerCategory()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Add"));
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel& model = *dlg.logList()->logModel();
        const GitRef tag{.type = GitRef::Type::Tag, .name = QStringLiteral("t")};
        const GitRef local{.type = GitRef::Type::LocalBranch, .name = QStringLiteral("l")};
        const GitRef remote{.type = GitRef::Type::RemoteBranch, .name = QStringLiteral("r")};
        const GitRef other{.type = GitRef::Type::Other, .name = QStringLiteral("refs/x/y")};
        const GitRef stash{.type = GitRef::Type::Stash, .name = QStringLiteral("stash")};
        QVERIFY(model.isRefShown(tag) && model.isRefShown(local) && model.isRefShown(remote) && model.isRefShown(other));

        const QMenu *menu = dlg.viewMenu();
        findAction(menu, QStringLiteral("Tags"))->trigger();
        QVERIFY(!model.isRefShown(tag));
        QVERIFY(model.isRefShown(local));
        findAction(menu, QStringLiteral("Local branches"))->trigger();
        QVERIFY(!model.isRefShown(local));
        findAction(menu, QStringLiteral("Remote branches"))->trigger();
        QVERIFY(!model.isRefShown(remote));
        findAction(menu, QStringLiteral("Other refs"))->trigger();
        QVERIFY(!model.isRefShown(other));
        QVERIFY(model.isRefShown(stash)); // TortoiseGit has no entry for these
        findAction(menu, QStringLiteral("Tags"))->trigger();
        QVERIFY(model.isRefShown(tag));
    }

    void datePickersShowCommitRange()
    {
        const TestRepo repo;
        makeDatedHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 3);
        // The pickers show the range of the commits, and setting them there starts no reload
        QCOMPARE(dlg.dateFrom()->date(), QDate(2020, 3, 10));
        QCOMPARE(dlg.dateTo()->date(), QDate(2023, 9, 20));
        QCOMPARE(dlg.logList()->walkOptions().from, -1);
        QCOMPARE(dlg.logList()->walkOptions().to, -1);
    }

    void fromDateLimitsLog()
    {
        const TestRepo repo;
        makeDatedHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.dateFrom()->setDate(QDate(2021, 6, 15));
        QVERIFY(waitForDateReload(dlg));
        QCOMPARE(subjects(*dlg.logList()->logModel()), (QStringList{QStringLiteral("Late"), QStringLiteral("Middle")}));
        // The From picker keeps the chosen day; To is still the newest commit
        QCOMPARE(dlg.dateFrom()->date(), QDate(2021, 6, 15));
        QCOMPARE(dlg.dateTo()->date(), QDate(2023, 9, 20));
    }

    void toDateLimitsLog()
    {
        const TestRepo repo;
        makeDatedHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.dateTo()->setDate(QDate(2021, 6, 15));
        QVERIFY(waitForDateReload(dlg));
        QCOMPARE(subjects(*dlg.logList()->logModel()), (QStringList{QStringLiteral("Middle"), QStringLiteral("Early")}));
        QCOMPARE(dlg.logList()->walkOptions().to, QDate(2021, 6, 15).endOfDay().toSecsSinceEpoch());
    }

    void fromDateCannotPassToDate()
    {
        const TestRepo repo;
        makeDatedHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.dateFrom()->setDate(QDate(2024, 1, 1));
        QCOMPARE(dlg.dateFrom()->date(), QDate(2023, 9, 20));
    }

    void settingsSurviveReopen()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("Add"));
        {
            const LogDlg dlg({repo.path()});
            QVERIFY(waitForLog(dlg));
            findAction(dlg.filterMenu(), QStringLiteral("Paths"))->trigger();
            findAction(dlg.filterMenu(), QStringLiteral("Use regular expression"))->trigger();
            findAction(dlg.filterMenu(), QStringLiteral("Case-sensitive"))->trigger();
            findAction(dlg.viewMenu(), QStringLiteral("Tags"))->trigger();
            findAction(dlg.viewMenu(), QStringLiteral("Remote branches"))->trigger();
            findAction(dlg.walkBehaviorMenu(), QStringLiteral("First Parent"))->trigger();
            QVERIFY(waitForReload(dlg));
        }
        const KConfigGroup general = KSharedConfig::openConfig()->group(QStringLiteral("General"));
        QCOMPARE(general.readEntry("SelectedLogFilters", 0U), LogFilterAll & ~static_cast<unsigned int>(LogFilterPaths));
        QCOMPARE(general.readEntry("UseRegexFilter", false), true);

        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(!findAction(dlg.filterMenu(), QStringLiteral("Paths"))->isChecked());
        QVERIFY(findAction(dlg.filterMenu(), QStringLiteral("Subject"))->isChecked());
        QVERIFY(findAction(dlg.filterMenu(), QStringLiteral("Use regular expression"))->isChecked());
        QVERIFY(findAction(dlg.filterMenu(), QStringLiteral("Case-sensitive"))->isChecked());
        QVERIFY(!findAction(dlg.viewMenu(), QStringLiteral("Tags"))->isChecked());
        QVERIFY(!findAction(dlg.viewMenu(), QStringLiteral("Remote branches"))->isChecked());
        QVERIFY(findAction(dlg.viewMenu(), QStringLiteral("Local branches"))->isChecked());
        const GitRef tag{.type = GitRef::Type::Tag, .name = QStringLiteral("t")};
        QVERIFY(!dlg.logList()->logModel()->isRefShown(tag));
        // TortoiseGit doesn't keep the walk options
        QVERIFY(!findAction(dlg.walkBehaviorMenu(), QStringLiteral("First Parent"))->isChecked());
        QVERIFY(!dlg.walkBehaviorButton()->isChecked());
    }

    void screenshotMenusAndFilter()
    {
        const TestRepo repo;
        makeBranchyHistory(repo);
        repo.run({QStringLiteral("tag"), QStringLiteral("v1.0"), QStringLiteral("HEAD~1")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        findAction(dlg.walkBehaviorMenu(), QStringLiteral("First Parent"))->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(saveMenuScreenshot(dlg, dlg.walkBehaviorButton(), dlg.walkBehaviorMenu(), QStringLiteral("logdlg-walkbehavior.png")));
        findAction(dlg.walkBehaviorMenu(), QStringLiteral("First Parent"))->trigger();
        QVERIFY(waitForReload(dlg));
        QVERIFY(saveMenuScreenshot(dlg, dlg.viewButton(), dlg.viewMenu(), QStringLiteral("logdlg-view.png")));

        applyFilter(dlg, QStringLiteral("merge"));
        QVERIFY(waitForReload(dlg));
        QVERIFY(dlg.logList()->logModel()->rowCount() > 0);
        QVERIFY(!dlg.grab().isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(dlg.grab().save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("logdlg-filter.png"))));
        }
    }

    void loadsWholeHistory()
    {
        const TestRepo repo;
        makeHistory(repo);
        const LogDlg dlg({repo.path()});
        QVERIFY(dlg.pathFilter().isEmpty());
        QVERIFY(waitForLog(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), commitCount + 1);
        QCOMPARE(dlg.logList()->logModel()->rev(0).refs.size(), 1);
    }

    void limitsToSubfolder()
    {
        const TestRepo repo;
        makeHistory(repo);
        const LogDlg dlg({repo.filePath(QStringLiteral("dir"))});
        QCOMPARE(dlg.pathFilter(), QStringList{QStringLiteral("dir")});
        QVERIFY(waitForLog(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), commitCount);
    }

    void fetchesActionsOfDrawnCommits()
    {
        const TestRepo repo;
        makeHistory(repo);
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        // Nothing selected: drawing the rows is enough (AsyncDiffThread)
        const GitLogListModel *model = dlg.logList()->logModel();
        QVERIFY(waitForRevFiles(*model, 0));
        QVERIFY(waitForRevFiles(*model, 1));
        QCOMPARE(model->rev(0).actions, TGitPath::Actions(TGitPath::Action::Added));
        QCOMPARE(model->rev(1).actions, TGitPath::Actions(TGitPath::Action::Modified));
        // A row far below the visible ones is left until it is drawn
        QVERIFY(model->rev(commitCount).filesState == GitRevLoglist::FilesState::NotLoaded);
    }

    void groupsMergeFilesByParent()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("Root"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("side")});
        repo.write(QStringLiteral("side.txt"), "side\n");
        repo.commitAll(QStringLiteral("Side"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(QStringLiteral("main.txt"), "main\n");
        repo.commitAll(QStringLiteral("Main"));
        repo.run({QStringLiteral("merge"),
                  QStringLiteral("--quiet"),
                  QStringLiteral("--no-ff"),
                  QStringLiteral("-m"),
                  QStringLiteral("Merge"),
                  QStringLiteral("side")});

        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        const GitLogListModel *model = dlg.logList()->logModel();
        dlg.logList()->setCurrentIndex(model->index(0, GitLogListModel::MessageColumn));
        QVERIFY(waitForFiles(dlg));

        // CGitStatusListCtrl::PrepareGroups: a group per parent, headed with its short hash
        const GitStatusListModel *files = dlg.changedFileList()->statusModel();
        QVERIFY(files->hasGroups());
        QCOMPARE(files->rowCount(), 2);
        const QStringList& parents = model->rev(0).parents;
        QCOMPARE(files->index(0, 0).data().toString(), QStringLiteral("Diff with parent 1: ") + parents.at(0).left(8));
        QCOMPARE(files->index(1, 0).data().toString(), QStringLiteral("Diff with parent 2: ") + parents.at(1).left(8));
        QCOMPARE(files->rowCount(files->index(0, 0)), 1);
        QCOMPARE(files->entry(files->index(0, 0, files->index(0, 0)))->path, QStringLiteral("side.txt"));
        QCOMPARE(files->entry(files->index(0, 0, files->index(1, 0)))->path, QStringLiteral("main.txt"));
        QCOMPARE(files->entry(files->index(0, 0, files->index(1, 0)))->parentNo, 1);
        QCOMPARE(model->rev(0).actions, TGitPath::Actions(TGitPath::Action::Added));
    }

    void showsSelectedCommit()
    {
        const TestRepo repo;
        makeHistory(repo);
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));

        const GitLogListModel *model = dlg.logList()->logModel();
        dlg.logList()->setCurrentIndex(model->index(0, GitLogListModel::MessageColumn));
        QVERIFY(dlg.messageText().contains(model->rev(0).hash));
        QVERIFY(dlg.messageText().contains(QStringLiteral("Other")));
        QVERIFY(waitForFiles(dlg));

        const GitStatusListModel *files = dlg.changedFileList()->statusModel();
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(files->entry(0).path, QStringLiteral("other.txt"));
        QCOMPARE(dlg.changedFileList()->currentVersion(), model->rev(0).hash);

        // The Actions column draws TortoiseGit's own icons, bundled in icons.qrc
        QVERIFY(QFile::exists(QStringLiteral(":/kortoisegit/icons/actionmodified-16.png")));

        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        constexpr int lastDrawnRow = 15;
        QVERIFY(waitForRevFiles(*model, lastDrawnRow));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("logdlg.png"))));
        }
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void drawsGraph_data()
    {
        QTest::addColumn<bool>("isDark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void drawsGraph()
    {
        QFETCH(const bool, isDark);
        const TestRepo repo;
        makeBranchyHistory(repo);
        LogDlg dlg({repo.path()});
        if (isDark) {
            dlg.setPalette(darkPalette());
        }
        QVERIFY(waitForLog(dlg));
        const GitLogListModel *model = dlg.logList()->logModel();
        QCOMPARE(model->rowCount(), 12);
        // The last merge starts a lane for feature on its right
        QCOMPARE(model->lanes(0), (std::vector<Lanes::LaneType>{Lanes::LaneType::MergeForkL, Lanes::LaneType::HeadR}));
        QCOMPARE(model->lanes(model->rowCount() - 1), (std::vector<Lanes::LaneType>{Lanes::LaneType::Initial}));

        dlg.resize(900, 500);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const GitLogList *list = dlg.logList();
        const QImage viewport = list->viewport()->grab().toImage();
        const int graphWidth = list->columnWidth(GitLogListModel::GraphColumn);
        const QRect graphCells = QRect(0, 0, graphWidth, viewport.height()).intersected(viewport.rect());
        QVERIFY(graphPixels(viewport, graphCells, list->palette().color(QPalette::Base)) > 0);

        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            const QString name = isDark ? QStringLiteral("logdlg-graph-dark.png") : QStringLiteral("logdlg-graph-light.png");
            QVERIFY(dlg.grab().save(QDir(screenshotDir).absoluteFilePath(name)));
        }
    }
    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void drawsEveryLaneColour_data()
    {
        drawsGraph_data();
    }

    void drawsEveryLaneColour()
    {
        QFETCH(const bool, isDark);
        const TestRepo repo;
        makeWideHistory(repo);
        LogDlg dlg({repo.path()});
        if (isDark) {
            dlg.setPalette(darkPalette());
        }
        QVERIFY(waitForLog(dlg));
        const GitLogListModel *model = dlg.logList()->logModel();
        std::size_t widest = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            widest = std::max(widest, model->lanes(row).size());
        }
        QVERIFY(widest > Lanes::colorsNum);

        dlg.resize(900, 1100);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        // Wide enough for every lane, so the screenshot shows each colour
        constexpr int graphWidth = 180;
        dlg.logList()->setColumnWidth(GitLogListModel::GraphColumn, graphWidth);
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            const QString name = isDark ? QStringLiteral("logdlg-lanes-dark.png") : QStringLiteral("logdlg-lanes-light.png");
            QVERIFY(dlg.grab().save(QDir(screenshotDir).absoluteFilePath(name)));
        }
    }

    void anEmptyRepositoryHasAnEmptyLog()
    {
        const TestRepo repo;
        const LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QCOMPARE(dlg.logList()->logModel()->rowCount(), 0);
        QVERIFY(dlg.walkBehaviorButton()->isEnabled()); // The load counts as finished, not failed
    }

    void listsTheGivenRange()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("On main"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("topic")});
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("On topic"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});

        const LogDlg dlg({repo.path()}, QStringLiteral("refs/heads/topic"));
        QVERIFY(waitForLog(dlg));
        QCOMPARE(subjects(*dlg.logList()->logModel()), (QStringList{QStringLiteral("On topic"), QStringLiteral("On main")}));
    }

    void aPathIsNeverAPathspecPattern()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("odd*.txt"), "1");
        repo.commitAll(QStringLiteral("Odd"));
        repo.write(QStringLiteral("oddly.txt"), "1");
        repo.commitAll(QStringLiteral("Other"));
        const LogDlg dlg({repo.filePath(QStringLiteral("odd*.txt"))});
        QVERIFY(waitForLog(dlg));
        QCOMPARE(subjects(*dlg.logList()->logModel()), QStringList{QStringLiteral("Odd")});
    }

    void menuOffersTheEntriesTortoiseGitShows()
    {
        const TestRepo repo;
        for (int i = 0; i < 3; ++i) {
            repo.write(QStringLiteral("f.txt"), QByteArray::number(i));
            repo.commitAll(QStringLiteral("Commit %1").arg(i));
        }
        repo.run({QStringLiteral("tag"), QStringLiteral("v1"), QStringLiteral("HEAD~1")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        GitLogList *list = dlg.logList();
        const QString compare = QStringLiteral("Compare with &working tree");
        const QString previous = QStringLiteral("&Compare with previous revision");
        const QString reset = QStringLiteral("R&eset \"main\" to this...");
        const QString createTag = QStringLiteral("Create &Tag at this version...");
        const QString push = QStringLiteral("Pus&h...");
        const QString sep = QStringLiteral("-");

        QStringList texts;
        const auto collect = [&texts](QMenu *menu) {
            texts = entryTexts(menu);
        };
        // The current branch is never offered, and is what makes HEAD pushable
        QVERIFY(withMenuOn(list, 0, collect));
        QCOMPARE(texts, (QStringList{compare, previous, sep, reset, createTag, sep, push, sep}));
        QVERIFY(withMenuOn(list, 1, collect));
        const QString deleteTag = QStringLiteral("&Delete refs/tags/v1");
        QCOMPARE(texts, (QStringList{compare, previous, sep, reset, createTag, sep, push, deleteTag, sep}));
        // No refs: nothing to push or delete. A root commit has nothing to compare with
        QVERIFY(withMenuOn(list, 2, collect));
        QCOMPARE(texts, (QStringList{compare, sep, reset, createTag, sep}));

        // None of the entries are for several commits
        list->setCurrentIndex(list->logModel()->index(0, 0));
        list->selectAll();
        bool opened = false;
        QTimer::singleShot(0, list, [&opened] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (menu) {
                opened = true;
                menu->close();
            }
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), list->viewport()->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(list->viewport(), &event);
        QVERIFY(!opened);
    }

    void aMergeComparesWithTheChosenParent()
    {
        const TestRepo repo;
        const QString file = QStringLiteral("f.txt");
        repo.write(file, "base\n");
        repo.commitAll(QStringLiteral("Base"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("side")});
        repo.write(file, "side\n");
        repo.commitAll(QStringLiteral("A & long subject from the side branch"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(file, "main\n");
        repo.commitAll(QStringLiteral("Main"));
        const QString noEdit = QStringLiteral("--no-edit");
        const QString ours = QStringLiteral("--strategy=ours");
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), ours, noEdit, QStringLiteral("side")});
        repo.write(file, "merged\n");
        repo.run({QStringLiteral("add"), file});
        repo.run({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("--amend"), noEdit});
        const DiffRecorder recorder(repo);
        const auto hashOf = [&repo](const QString& rev) {
            return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), rev}).trimmed());
        };

        LogDlg dlg({repo.filePath(file)});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        const QString first = QStringLiteral("Parent 1: \"Main\" (%1)").arg(hashOf(QStringLiteral("HEAD^1")).left(8));
        const QString sideHash = hashOf(QStringLiteral("side")).left(8);
        const QString second = QStringLiteral("Parent 2: \"A && long subject fro...\" (%1)").arg(sideHash);
        QStringList texts;
        const auto chooseSecond = [&texts, &second](QMenu *menu) {
            const QMenu *submenu = entry(menu, QStringLiteral("&Compare with previous revision"))->menu();
            texts = entryTexts(submenu);
            entry(submenu, second)->trigger();
        };
        QVERIFY(withMenuOn(dlg.logList(), 0, chooseSecond));
        QCOMPARE(texts, (QStringList{first, second}));
        QVERIFY(recorder.saw(QStringLiteral("side merged")));
    }

    void comparesAFolderInOneDirDiff()
    {
        const TestRepo repo;
        const QString file = QStringLiteral("dir/a.txt");
        const QString outside = QStringLiteral("outside.txt");
        repo.write(file, "one\n");
        repo.write(outside, "one\n");
        repo.commitAll(QStringLiteral("First"));
        repo.write(file, "two\n");
        repo.write(outside, "two\n");
        repo.commitAll(QStringLiteral("Second"));
        repo.write(file, "working\n");
        repo.write(outside, "working\n");
        const DiffRecorder recorder(repo);

        const LogDlg folder({repo.filePath(QStringLiteral("dir"))});
        QVERIFY(waitForLog(folder));
        folder.logList()->compareWithWorkingTree(0);
        QVERIFY(recorder.saw(QStringLiteral("two working dir")));
        folder.logList()->compareWithPrevious(0, 0);
        QVERIFY(recorder.saw(QStringLiteral("one two dir")));

        const LogDlg whole({repo.path()});
        QVERIFY(waitForLog(whole));
        whole.logList()->compareWithPrevious(0, 0);
        QVERIFY(recorder.saw(QStringLiteral("one two dir outside.txt")));
    }

    void comparesAFileAcrossItsRenames()
    {
        const TestRepo repo;
        const QByteArray common = "the same line\nthe same line\nthe same line\nthe same line\nthe same line\n";
        repo.write(QStringLiteral("old.txt"), common + "one\n");
        repo.commitAll(QStringLiteral("Add"));
        repo.run({QStringLiteral("mv"), QStringLiteral("old.txt"), QStringLiteral("new.txt")});
        repo.write(QStringLiteral("new.txt"), common + "two\n");
        repo.commitAll(QStringLiteral("Rename"));
        repo.write(QStringLiteral("new.txt"), common + "three\n");
        repo.commitAll(QStringLiteral("Edit"));
        repo.write(QStringLiteral("new.txt"), common + "working\n");
        const DiffRecorder recorder(repo);

        const LogDlg dlg({repo.filePath(QStringLiteral("new.txt"))});
        QVERIFY(waitForLog(dlg));
        findAction(dlg.walkBehaviorMenu(), QStringLiteral("Follow renames"))->trigger();
        QVERIFY(waitForReload(dlg));
        GitLogList *list = dlg.logList();
        QCOMPARE(list->logModel()->rowCount(), 3);

        // Row 2 predates the rename: the files above it are fetched first, in the background
        list->compareWithWorkingTree(2);
        QVERIFY(recorder.saw(QStringLiteral("one working")));
        list->compareWithPrevious(1, 0); // The rename itself: the name differs on each side
        QVERIFY(recorder.saw(QStringLiteral("one two")));
        list->compareWithPrevious(0, 0);
        QVERIFY(recorder.saw(QStringLiteral("two three")));
    }

    void resetNeedsAWorkingTree()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("First"));
        const QTemporaryDir bareDir;
        QVERIFY(bareDir.isValid());
        const QStringList clone = QStringLiteral("clone --quiet --bare").split(QLatin1Char(' '));
        repo.run(clone + QStringList{repo.path(), bareDir.path()});

        LogDlg dlg({bareDir.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        QStringList texts;
        QVERIFY(withMenuOn(dlg.logList(), 0, [&texts](QMenu *menu) {
            texts = entryTexts(menu);
        }));
        QVERIFY(texts.contains(QStringLiteral("Create &Tag at this version...")));
        QVERIFY(!texts.contains(QStringLiteral("R&eset \"main\" to this...")));

        // CResetDlg::OnInitDialog: only Soft is left
        const ResetDlg resetDlg(Git(bareDir.path()), QStringLiteral("HEAD"));
        QCOMPARE(resetDlg.resetType(), ResetType::Soft);
        const auto isEnabled = [&resetDlg](const QString& start) {
            const QList<QRadioButton *> radios = resetDlg.findChildren<QRadioButton *>();
            const auto found = std::ranges::find_if(radios, [&start](const QRadioButton *radio) {
                return radio->text().startsWith(start);
            });
            return found != radios.cend() && (*found)->isEnabled();
        };
        QVERIFY(isEnabled(QStringLiteral("&Soft")));
        QVERIFY(!isEnabled(QStringLiteral("&Mixed")));
        QVERIFY(!isEnabled(QStringLiteral("&Hard")));
    }

    void resetMovesTheLabelsAndKeepsTheCommits()
    {
        const TestRepo repo;
        for (int i = 0; i < 3; ++i) {
            repo.write(QStringLiteral("f.txt"), QByteArray::number(i));
            repo.commitAll(QStringLiteral("Commit %1").arg(i));
        }
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        GitLogList *list = dlg.logList();
        const GitLogListModel *model = list->logModel();
        const QString newest = model->rev(0).hash;
        const QString oldest = model->rev(2).hash;
        const auto head = [&repo] {
            return QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})).trimmed();
        };
        const auto hasMain = [model](int row) {
            return std::ranges::any_of(model->rev(row).refs, [](const GitRef& ref) {
                return ref.name == QLatin1String("main");
            });
        };

        // Answers ResetDlg once it has loaded, and closes the progress dialog when the reset is done
        QString startVersion;
        ResetType startType = ResetType::Merge;
        const auto chooseReset = [&](bool accepted) {
            QTimer answerer;
            QObject::connect(&answerer, &QTimer::timeout, &answerer, [&] {
                QWidget *modal = QApplication::activeModalWidget();
                auto *resetDlg = qobject_cast<ResetDlg *>(modal);
                auto *progress = qobject_cast<ProgressDlg *>(modal);
                if (resetDlg && !resetDlg->chooseVersion()->isLoading()) {
                    startVersion = resetDlg->chooseVersion()->versionName();
                    startType = resetDlg->resetType();
                    accepted ? resetDlg->accept() : resetDlg->reject();
                } else if (progress && progress->isDone()) {
                    progress->accept();
                }
            });
            answerer.start(10);
            return withMenuOn(list, 2, [](QMenu *menu) {
                entry(menu, QStringLiteral("R&eset \"main\" to this..."))->trigger();
            });
        };

        QVERIFY(chooseReset(false));
        QCOMPARE(startVersion, oldest);
        QCOMPARE(startType, ResetType::Mixed);
        QCOMPARE(head(), newest);
        QVERIFY(hasMain(0));

        QVERIFY(chooseReset(true));
        QCOMPARE(head(), oldest);
        QVERIFY(!hasMain(0));
        QVERIFY(hasMain(2));
        QCOMPARE(model->rowCount(), 3);
        QVERIFY(!list->isBusy());
    }

    void stashEntriesCanOnlyBeDeleted()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("First"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.run({QStringLiteral("stash"), QStringLiteral("--quiet")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        GitLogList *list = dlg.logList();
        list->setRange(QStringLiteral("refs/stash"));
        list->reload();
        QVERIFY(waitForReload(dlg));
        QVERIFY(showDialog(dlg));
        const GitLogListModel *model = list->logModel();
        QStringList texts;
        const auto collect = [&texts](QMenu *menu) {
            texts = entryTexts(menu);
        };
        const QString compare = QStringLiteral("Compare with &working tree");
        const QString previous = QStringLiteral("&Compare with previous revision");
        const QString sep = QStringLiteral("-");
        QVERIFY(withMenuOn(list, 0, collect));
        QCOMPARE(texts, (QStringList{compare, previous, sep, sep, QStringLiteral("&Delete refs/stash"), sep}));
        // The commit holding the stash's index is on the stash too, when listed right below it
        QCOMPARE(model->rev(1).hash, model->rev(0).parents.at(1));
        QVERIFY(withMenuOn(list, 1, collect));
        QCOMPARE(texts, (QStringList{compare, previous, sep, sep}));
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void deletesTagsAndUpdatesTheLabels_data()
    {
        QTest::addColumn<QStringList>("tagArguments");
        QTest::newRow("lightweight") << QStringList{QStringLiteral("tag"), QStringLiteral("v1")};
        QTest::newRow("annotated") << QStringLiteral("tag -am Note v1").split(QLatin1Char(' '));
    }

    void deletesTagsAndUpdatesTheLabels()
    {
        QFETCH(const QStringList, tagArguments);
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("First"));
        repo.write(QStringLiteral("f.txt"), "2");
        repo.commitAll(QStringLiteral("Second"));
        repo.run(QStringList(tagArguments) << QStringLiteral("HEAD~1"));
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        GitLogList *list = dlg.logList();
        QCOMPARE(refNames(list, 1), QStringList{QStringLiteral("refs/tags/v1")});

        const BoxAnswerer answer({QDialogButtonBox::Yes});
        QVERIFY(withMenuOn(list, 1, [](QMenu *menu) {
            entry(menu, QStringLiteral("&Delete refs/tags/v1"))->trigger();
        }));
        QVERIFY(!refExists(repo, QStringLiteral("refs/tags/v1")));
        // At once, without a reload
        QVERIFY(!list->isBusy());
        QVERIFY(refNames(list, 1).isEmpty());
        QCOMPARE(refNames(list, 0), QStringList{QStringLiteral("refs/heads/main")});
    }

    void allStopsAtTheFirstRefusal()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("First"));
        repo.run({QStringLiteral("branch"), QStringLiteral("zz")});
        repo.run({QStringLiteral("branch"), QStringLiteral("aa")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        const BoxAnswerer stops({QDialogButtonBox::Yes, QDialogButtonBox::No});
        QVERIFY(withMenuOn(dlg.logList(), 0, [](QMenu *menu) {
            entry(entry(menu, QStringLiteral("&Delete branch/tag"))->menu(), QStringLiteral("All"))->trigger();
        }));
        // In TortoiseGit's sorted order, whatever order git log decorated them in
        QVERIFY(!refExists(repo, QStringLiteral("refs/heads/aa")));
        QVERIFY(refExists(repo, QStringLiteral("refs/heads/zz")));
        QCOMPARE(refNames(dlg.logList(), 0).size(), 2);
    }

    void rightClickingALabelLimitsTheEntriesToIt()
    {
        const RemoteSetup setup;
        const TestRepo& repo = setup.repo;
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        GitLogList *list = dlg.logList();

        const std::optional<QPoint> tagLabel = labelPoint(list, 0, QStringLiteral("v1"));
        QVERIFY(tagLabel);
        QStringList texts;
        const auto collect = [&texts](QMenu *menu) {
            texts = entryTexts(menu);
        };
        QVERIFY(withMenuOn(list, 0, collect, tagLabel));
        QVERIFY(texts.contains(QStringLiteral("Pus&h \"v1\"...")));
        QVERIFY(texts.contains(QStringLiteral("&Delete refs/tags/v1")));
        QVERIFY(!texts.contains(QStringLiteral("&Delete branch/tag")));

        // A remote branch's label limits Delete, but Push keeps its plain text
        QVERIFY(withMenuOn(list, 0, collect, labelPoint(list, 0, QStringLiteral("origin/feature"))));
        QVERIFY(texts.contains(QStringLiteral("Pus&h...")));
        QVERIFY(texts.contains(QStringLiteral("&Delete refs/remotes/origin/feature")));

        // The current branch's label leaves nothing to delete
        QVERIFY(withMenuOn(list, 0, collect, labelPoint(list, 0, QStringLiteral("main"))));
        QVERIFY(texts.contains(QStringLiteral("Pus&h \"main\"...")));
        QVERIFY(std::ranges::none_of(texts, [](const QString& text) {
            return text.startsWith(QStringLiteral("&Delete"));
        }));

        // What is drawn is what is hit: the first label ends on its border, and the row's background follows
        const QImage image = list->viewport()->grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        const QRect row = list->visualRect(list->logModel()->index(0, GitLogListModel::MessageColumn));
        const int middle = row.center().y();
        const auto pixelAt = [&image, ratio, middle](int x) {
            return image.pixel(static_cast<int>(x * ratio), static_cast<int>(middle * ratio));
        };
        int lastHit = row.left();
        QVERIFY(list->refLabelAt(0, QPoint(lastHit, middle)));
        while (list->refLabelAt(0, QPoint(lastHit + 1, middle))) {
            ++lastHit;
        }
        const QRgb background = pixelAt(row.right() - 1);
        QVERIFY(pixelAt(lastHit) != background);
        QCOMPARE(pixelAt(lastHit + 1), background);

        // Away from the labels, and past them
        QVERIFY(!list->refLabelAt(0, QPoint(row.right() - 1, row.center().y())));
        QVERIFY(!list->refLabelAt(0, QPoint(row.left(), row.bottom() + 5)));
    }

    void pushPreselectsTheGuessedRef()
    {
        const RemoteSetup setup;
        const TestRepo& repo = setup.repo;
        repo.run({QStringLiteral("tag"), QStringLiteral("v1"), QStringLiteral("HEAD~1")});
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        QVERIFY(showDialog(dlg));
        GitLogList *list = dlg.logList();

        const auto preselected = [list](int row, const std::optional<QPoint>& position, const QString& text) {
            QString local;
            QTimer closer;
            QObject::connect(&closer, &QTimer::timeout, &closer, [&local, &closer] {
                if (auto *pushDlg = qobject_cast<PushDlg *>(QApplication::activeModalWidget())) {
                    closer.stop();
                    local = pushDlg->options().localBranch;
                    pushDlg->reject();
                }
            });
            closer.start(10);
            const bool opened = withMenuOn(
                list,
                row,
                [&text](QMenu *menu) {
                    entry(menu, text)->trigger();
                },
                position);
            return opened ? local : QString();
        };

        // The first local branch, then a tag
        QCOMPARE(preselected(0, std::nullopt, QStringLiteral("Pus&h...")), QStringLiteral("feature"));
        QCOMPARE(preselected(1, std::nullopt, QStringLiteral("Pus&h...")), QStringLiteral("refs/tags/v1"));
        // The ref under the mouse, unless it is a remote branch
        const QString mainText = QStringLiteral("Pus&h \"main\"...");
        QCOMPARE(preselected(0, labelPoint(list, 0, QStringLiteral("main")), mainText), QStringLiteral("main"));
        const std::optional<QPoint> remoteLabel = labelPoint(list, 0, QStringLiteral("origin/feature"));
        QCOMPARE(preselected(0, remoteLabel, QStringLiteral("Pus&h...")), QStringLiteral("feature"));
    }

    void selectModeReturnsTheCommit()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("One"));
        LogDlg dlg({repo.path()});
        QVERIFY(waitForLog(dlg));
        dlg.setSelect(true);
        const QPushButton *okButton = dlg.buttonBox()->button(QDialogButtonBox::Ok);
        QVERIFY(dlg.buttonBox()->button(QDialogButtonBox::Cancel));
        QVERIFY(!okButton->isEnabled()); // CLogDlg::EnableOKButton: exactly one commit
        selectRow(dlg, 0);
        QVERIFY(okButton->isEnabled());
        dlg.accept();
        QCOMPARE(dlg.selectedHash(), dlg.logList()->logModel()->rev(0).hash);
    }
};

QTEST_MAIN(LogDlgTest)

#include "logdlgtest.moc"
