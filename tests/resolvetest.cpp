// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitcall.hpp"
#include "gitstatuslistctrl.hpp"
#include "progresscommands/resolveprogresscommand.hpp"
#include "progressdlg.hpp"
#include "resolvedlg.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace kgt;

namespace
{

/**
 * Leaves the repository in a merge with conflicts in top.txt and dir/nested.txt, and clean.txt merged without one.
 * @param repo An empty repository.
 */
void startConflictedMerge(const TestRepo& repo)
{
    const QString other = QStringLiteral("other");
    repo.write(QStringLiteral("top.txt"), "base\n");
    repo.write(QStringLiteral("dir/nested.txt"), "base\n");
    repo.write(QStringLiteral("clean.txt"), "base\n");
    repo.commitAll(QStringLiteral("base"));
    repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), other});
    repo.write(QStringLiteral("top.txt"), "theirs\n");
    repo.write(QStringLiteral("dir/nested.txt"), "theirs\n");
    repo.write(QStringLiteral("clean.txt"), "theirs\n");
    repo.commitAll(QStringLiteral("theirs"));
    repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
    repo.write(QStringLiteral("top.txt"), "ours\n");
    repo.write(QStringLiteral("dir/nested.txt"), "ours\n");
    repo.commitAll(QStringLiteral("ours"));
    try {
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("--no-edit"), other});
    } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch): The conflicts are the point
    }
}

/**
 * @param repo The repository.
 * @returns The files still unmerged, each once and sorted.
 */
QStringList unmergedPaths(const TestRepo& repo)
{
    const QStringList args{QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("--diff-filter=U")};
    const QString output = QString::fromUtf8(repo.git().run(args));
    QStringList paths = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    paths.sort();
    return paths;
}

/**
 * @param path A file.
 * @param actions What happened to it.
 * @returns It as the Resolve dialog would hand it over.
 */
GitStatusListEntry entryFor(const QString& path, TGitPath::Actions actions)
{
    GitStatusListEntry entry;
    entry.path = path;
    entry.actions = actions;
    return entry;
}

/**
 * Runs what the progress command sets up.
 * @param repo The repository.
 * @param entries The files to resolve.
 * @param dialog The dialog to run it in.
 * @returns Whether the commands ended.
 */
bool runResolve(const TestRepo& repo, const QList<GitStatusListEntry>& entries, ProgressDlg& dialog)
{
    ResolveProgressCommand command;
    command.setPathList(entries);
    command.setUp(repo.path(), dialog);
    QSignalSpy finished(&dialog, &ProgressDlg::commandsFinished);
    dialog.start();
    return dialog.isDone() || finished.wait();
}

/**
 * @param dlg A dialog that has just been created.
 * @returns Whether its list finished loading.
 */
bool waitLoaded(const ResolveDlg& dlg)
{
    QSignalSpy loaded(dlg.listCtrl(), &GitStatusListCtrl::loaded);
    return loaded.wait(10000);
}

/**
 * @param dlg The dialog.
 * @returns The paths it lists, sorted.
 */
QStringList listedPaths(const ResolveDlg& dlg)
{
    const GitStatusListModel *model = dlg.listCtrl()->statusModel();
    QStringList paths;
    for (int row = 0; row < model->listedCount(); ++row) {
        paths.append(model->entry(row).path);
    }
    paths.sort();
    return paths;
}

} // namespace

/**
 * CResolveDlg's choice of files and ResolveProgressCommand's handling of them.
 */
class ResolveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsOnlyConflictsBelowTheSelection()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        repo.write(QStringLiteral("untracked.txt"), "new\n");

        const ResolveDlg folder(repo.path(), {repo.filePath(QStringLiteral("dir"))});
        QVERIFY(waitLoaded(folder));
        QCOMPARE(listedPaths(folder), QStringList{QStringLiteral("dir/nested.txt")});
        QCOMPARE(folder.listCtrl()->checkedEntries().size(), 1);
        QVERIFY(folder.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());

        const ResolveDlg root(repo.path(), {repo.path()});
        QVERIFY(waitLoaded(root));
        QCOMPARE(listedPaths(root), (QStringList{QStringLiteral("dir/nested.txt"), QStringLiteral("top.txt")}));
        QCOMPARE(root.listCtrl()->checkedEntries().size(), 2);
    }

    void okIsDisabledWithNothingInConflict()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));

        const ResolveDlg dlg(repo.path(), {repo.path()});
        QVERIFY(waitLoaded(dlg));
        QVERIFY(listedPaths(dlg).isEmpty());
        QVERIFY(!dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    }

    void addsUnmergedFilesAndSkipsTheRest()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        repo.write(QStringLiteral("clean.txt"), "edited since the merge\n");

        ProgressDlg dialog(repo.path());
        const GitStatusListEntry notUnmerged = entryFor(QStringLiteral("clean.txt"), TGitPath::Action::Modified);
        const GitStatusListEntry unmerged = entryFor(QStringLiteral("top.txt"), TGitPath::Action::Unmerged);
        QVERIFY(runResolve(repo, {unmerged, notUnmerged}, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QCOMPARE(unmergedPaths(repo), QStringList{QStringLiteral("dir/nested.txt")});
        QVERIFY2(dialog.logText().contains(QStringLiteral("Skipped  clean.txt")), qPrintable(dialog.logText()));
        QVERIFY2(dialog.logText().contains(QStringLiteral("Resolved  top.txt")), qPrintable(dialog.logText()));
        // git add never saw it: the edit is not staged
        const QByteArray unstaged = repo.git().run({QStringLiteral("diff"), QStringLiteral("--name-only")});
        QVERIFY2(unstaged.contains("clean.txt"), unstaged.constData());
    }

    void onlySkippedFilesIsASuccess()
    {
        const TestRepo repo;
        startConflictedMerge(repo);

        ProgressDlg dialog(repo.path());
        const GitStatusListEntry notUnmerged = entryFor(QStringLiteral("clean.txt"), TGitPath::Action::Modified);
        QVERIFY(runResolve(repo, {notUnmerged}, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QVERIFY2(dialog.logText().contains(QStringLiteral("Skipped  clean.txt")), qPrintable(dialog.logText()));
    }

    void failureReportsNothingResolved()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        repo.write(QStringLiteral(".git/index.lock"), "");

        ProgressDlg dialog(repo.path());
        QVERIFY(runResolve(repo, {entryFor(QStringLiteral("top.txt"), TGitPath::Action::Unmerged)}, dialog));

        QVERIFY(dialog.gitStatus() != 0);
        QVERIFY2(!dialog.logText().contains(QStringLiteral("Resolved  top.txt")), qPrintable(dialog.logText()));
    }
};

QTEST_MAIN(ResolveTest)

#include "resolvetest.moc"
