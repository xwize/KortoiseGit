// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlg.hpp"
#include "gitloglist.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "testrepo.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
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
class LogDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
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
};

QTEST_MAIN(LogDlgTest)

#include "logdlgtest.moc"
