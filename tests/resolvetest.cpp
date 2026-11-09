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

#include <tuple>

Q_DECLARE_METATYPE(kgt::ResolveWith)

using namespace kgt;

namespace
{

using Entries = QList<GitStatusListEntry>;

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
 * Runs Resolve conflict using on some files, as the file list does.
 * @param repo The repository.
 * @param resolveWith The side.
 * @param entries The files.
 * @param dialog The dialog to run it in.
 * @returns Whether the commands ended.
 */
bool runResolveWith(const TestRepo& repo, ResolveWith resolveWith, const Entries& entries, ProgressDlg& dialog)
{
    ResolveProgressCommand command(resolveWith);
    command.setPathList(entries);
    QSignalSpy read(&command, &ResolveProgressCommand::conflictInfoRead);
    command.readConflictInfo(repo.path());
    if ((read.isEmpty() && !read.wait()) || command.error()) {
        return false;
    }
    command.setUp(repo.path(), dialog);
    QSignalSpy finished(&dialog, &ProgressDlg::commandsFinished);
    dialog.start();
    return dialog.isDone() || finished.wait();
}

/**
 * Leaves the repository in a merge with a delete/modify conflict in file.txt.
 * @param repo An empty repository.
 * @param oursDeletes Whether the current branch deleted the file; if not, the other branch did.
 */
void startDeleteModifyMerge(const TestRepo& repo, bool oursDeletes)
{
    const QString file = QStringLiteral("file.txt");
    const QString other = QStringLiteral("other");
    const auto change = [&repo, &file](bool deletes, const QByteArray& content) {
        if (deletes) {
            repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), file});
        } else {
            repo.write(file, content);
        }
        repo.commitAll(QStringLiteral("change"));
    };
    repo.write(file, "base\n");
    repo.commitAll(QStringLiteral("base"));
    repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), other});
    change(!oursDeletes, "theirs\n");
    repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
    change(oursDeletes, "ours\n");
    try {
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("--no-edit"), other});
    } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch): The conflict is the point
    }
}

/**
 * @param repo The repository.
 * @param path A file.
 * @returns What the working tree's file holds.
 */
QByteArray contentOf(const TestRepo& repo, const QString& path)
{
    QFile file(repo.filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

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

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void takesTheChosenSide_data()
    {
        QTest::addColumn<ResolveWith>("resolveWith");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("theirs") << ResolveWith::Theirs << QByteArray("theirs\n");
        QTest::newRow("mine") << ResolveWith::Mine << QByteArray("ours\n");
    }

    void takesTheChosenSide()
    {
        QFETCH(const ResolveWith, resolveWith);
        QFETCH(const QByteArray, expected);
        const TestRepo repo;
        startConflictedMerge(repo);

        ProgressDlg dialog(repo.path());
        const Entries entries{entryFor(QStringLiteral("top.txt"), TGitPath::Action::Unmerged),
                              entryFor(QStringLiteral("dir/nested.txt"), TGitPath::Action::Unmerged)};
        QVERIFY(runResolveWith(repo, resolveWith, entries, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QCOMPARE(contentOf(repo, QStringLiteral("top.txt")), expected);
        QCOMPARE(contentOf(repo, QStringLiteral("dir/nested.txt")), expected);
        QCOMPARE(unmergedPaths(repo), QStringList());
        QVERIFY2(dialog.logText().contains(QStringLiteral("Resolved  dir/nested.txt")), qPrintable(dialog.logText()));
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void deleteModifyFollowsTheChosenSide_data()
    {
        QTest::addColumn<bool>("oursDeletes");
        QTest::addColumn<ResolveWith>("resolveWith");
        QTest::addColumn<QByteArray>("expected"); // Empty if the file should be gone
        QTest::newRow("we deleted, take mine") << true << ResolveWith::Mine << QByteArray();
        QTest::newRow("we deleted, take theirs") << true << ResolveWith::Theirs << QByteArray("theirs\n");
        QTest::newRow("they deleted, take theirs") << false << ResolveWith::Theirs << QByteArray();
        QTest::newRow("they deleted, take mine") << false << ResolveWith::Mine << QByteArray("ours\n");
    }

    void deleteModifyFollowsTheChosenSide()
    {
        QFETCH(const bool, oursDeletes);
        QFETCH(const ResolveWith, resolveWith);
        QFETCH(const QByteArray, expected);
        const TestRepo repo;
        startDeleteModifyMerge(repo, oursDeletes);
        const QString file = QStringLiteral("file.txt");

        ProgressDlg dialog(repo.path());
        QVERIFY(runResolveWith(repo, resolveWith, {entryFor(file, TGitPath::Action::Unmerged)}, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QCOMPARE(unmergedPaths(repo), QStringList());
        QCOMPARE(QFileInfo::exists(repo.filePath(file)), !expected.isEmpty());
        QCOMPARE(contentOf(repo, file), expected);
        const QByteArray indexed = repo.git().run({QStringLiteral("ls-files"), QStringLiteral("--"), file});
        QCOMPARE(indexed.isEmpty(), expected.isEmpty());
        QVERIFY2(dialog.logText().contains(QStringLiteral("Resolved  file.txt")), qPrintable(dialog.logText()));
    }

    void pathsNoLongerInConflictAreSkipped()
    {
        const TestRepo repo;
        startConflictedMerge(repo);
        // The list still calls it unmerged, as it would if the conflict was resolved elsewhere after the list loaded
        repo.run({QStringLiteral("add"), QStringLiteral("top.txt")});

        ProgressDlg dialog(repo.path());
        const GitStatusListEntry stale = entryFor(QStringLiteral("top.txt"), TGitPath::Action::Unmerged);
        QVERIFY(runResolveWith(repo, ResolveWith::Theirs, {stale}, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QVERIFY2(dialog.logText().contains(QStringLiteral("Skipped  top.txt")), qPrintable(dialog.logText()));
        QVERIFY(contentOf(repo, QStringLiteral("top.txt")).contains("<<<<<<<"));
    }

    void chosenSubmoduleSideIsLeftAlone()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("file.txt"), "file\n");
        repo.commitAll(QStringLiteral("base"));
        const QString revParse = QStringLiteral("rev-parse");
        const QByteArray blob = repo.git().run({revParse, QStringLiteral("HEAD:file.txt")}).trimmed();
        const QByteArray commit = repo.git().run({revParse, QStringLiteral("HEAD")}).trimmed();
        const QByteArray stages = "100644 " + blob + " 2\tmod\n160000 " + commit + " 3\tmod\n";
        const QStringList indexInfo{QStringLiteral("update-index"), QStringLiteral("--index-info")};
        std::ignore = repo.git().run(indexInfo, RunMode::ReadWrite, stages);

        ProgressDlg dialog(repo.path());
        const GitStatusListEntry submodule = entryFor(QStringLiteral("mod"), TGitPath::Action::Unmerged);
        QVERIFY(runResolveWith(repo, ResolveWith::Theirs, {submodule}, dialog));

        QCOMPARE(dialog.gitStatus(), 0);
        QVERIFY2(dialog.logText().contains(QStringLiteral("Skipped  mod")), qPrintable(dialog.logText()));
        QCOMPARE(unmergedPaths(repo), QStringList{QStringLiteral("mod")});
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void rebaseTitlesNameTheVersionObtained_data()
    {
        QTest::addColumn<int>("entry");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("branch being rebased") << 0 << QStringLiteral("feature") << QByteArray("feature\n");
        QTest::newRow("branch rebased onto") << 1 << QStringLiteral("main") << QByteArray("main\n");
    }

    void rebaseTitlesNameTheVersionObtained()
    {
        QFETCH(const int, entry);
        QFETCH(const QString, title);
        QFETCH(const QByteArray, expected);
        const TestRepo repo;
        const QString file = QStringLiteral("file.txt");
        const QString feature = QStringLiteral("feature");
        repo.write(file, "base\n");
        repo.commitAll(QStringLiteral("base"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), feature});
        repo.write(file, "feature\n");
        repo.commitAll(QStringLiteral("on feature"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(file, "main\n");
        repo.commitAll(QStringLiteral("on main"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), feature});
        try {
            // --apply: CTGitPath::IsRebaseActive looks for rebase-apply
            repo.run(QStringLiteral("rebase --quiet --apply main").split(QLatin1Char(' ')));
        } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch): The conflict is the point
        }

        const ResolveDlg dlg(repo.path(), {repo.path()});
        QVERIFY(waitLoaded(dlg));
        const QList<GitStatusListCtrl::ResolveUsingEntry> entries = dlg.listCtrl()->resolveUsingEntries();
        QCOMPARE(entries.size(), 2);
        QVERIFY2(entries.at(entry).title.startsWith(title), qPrintable(entries.at(entry).title));

        ProgressDlg dialog(repo.path());
        const GitStatusListEntry conflicted = entryFor(file, TGitPath::Action::Unmerged);
        QVERIFY(runResolveWith(repo, entries.at(entry).resolveWith, {conflicted}, dialog));
        QCOMPARE(dialog.gitStatus(), 0);
        QCOMPARE(contentOf(repo, file), expected);
    }
};

QTEST_MAIN(ResolveTest)

#include "resolvetest.moc"
