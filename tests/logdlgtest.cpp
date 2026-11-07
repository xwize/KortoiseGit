// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlg.hpp"
#include "gitloglist.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "testrepo.hpp"

#include <QDir>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

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
        QCOMPARE(model->rev(0).actions, TGitPath::Actions(TGitPath::Action::Added));

        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("logdlg.png"))));
        }
    }
};

QTEST_MAIN(LogDlgTest)

#include "logdlgtest.moc"
