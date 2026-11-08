// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitstatus.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <tuple>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

/**
 * @param repo The repository.
 * @param ignored Whether to list ignored files.
 * @returns Its status, by path.
 */
QHash<QString, GitStatusEntry> statusByPath(const TestRepo& repo, IgnoredFiles ignored = IgnoredFiles::Exclude)
{
    QHash<QString, GitStatusEntry> byPath;
    for (const GitStatusEntry& entry : GitStatus::status(repo.git(), ignored)) {
        byPath.insert(entry.path, entry);
    }
    return byPath;
}

/**
 * @returns The expected entry for an ordinary change.
 */
GitStatusEntry changed(const QString& path, FileState index, FileState worktree, TGitPath::Actions actions)
{
    return {.kind = GitStatusKind::Changed, .path = path, .origPath = {}, .index = index, .worktree = worktree, .actions = actions};
}

/**
 * Commits some files, then leaves one path in each state.
 * @param repo An empty repository.
 */
void makeEveryState(const TestRepo& repo)
{
    const QStringList committed{QStringLiteral("modified.txt"),
                                QStringLiteral("staged.txt"),
                                QStringLiteral("both.txt"),
                                QStringLiteral("deleted.txt"),
                                QStringLiteral("staged deleted.txt"),
                                QStringLiteral("old name.txt"),
                                QStringLiteral("ünïcödé €.txt"),
                                QStringLiteral("dir with space/file with space.txt")};
    for (const QString& path : committed) {
        repo.write(path, "original " + path.toUtf8() + '\n');
    }
    repo.write(QStringLiteral(".gitignore"), "*.log\n");
    repo.commitAll(QStringLiteral("initial"));

    repo.write(QStringLiteral("untracked.txt"), "new\n");
    repo.write(QStringLiteral("new dir/untracked ü.txt"), "new\n");
    repo.write(QStringLiteral("line\nbreak.txt"), "new\n");
    repo.write(QStringLiteral("build.log"), "ignored\n");
    repo.write(QStringLiteral("added.txt"), "added\n");
    repo.run({QStringLiteral("add"), QStringLiteral("added.txt")});
    repo.write(QStringLiteral("modified.txt"), "changed\n");
    repo.write(QStringLiteral("staged.txt"), "changed\n");
    repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")});
    repo.write(QStringLiteral("both.txt"), "staged change\n");
    repo.run({QStringLiteral("add"), QStringLiteral("both.txt")});
    repo.write(QStringLiteral("both.txt"), "further change\n");
    repo.remove(QStringLiteral("deleted.txt"));
    repo.run({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("staged deleted.txt")});
    repo.run({QStringLiteral("mv"), QStringLiteral("old name.txt"), QStringLiteral("new näme.txt")});
    repo.write(QStringLiteral("ünïcödé €.txt"), "changed\n");
    repo.write(QStringLiteral("dir with space/file with space.txt"), "changed\n");
}

/**
 * @returns What makeEveryState() leaves, by path.
 */
QHash<QString, GitStatusEntry> expectedEveryState()
{
    const auto untracked = [](const QString& path) {
        return GitStatusEntry{.kind = GitStatusKind::Untracked, .path = path, .origPath = {}, .index = {}, .worktree = {}, .actions = Action::Unversioned};
    };
    return {
        {QStringLiteral("untracked.txt"), untracked(QStringLiteral("untracked.txt"))},
        {QStringLiteral("new dir/untracked ü.txt"), untracked(QStringLiteral("new dir/untracked ü.txt"))},
        {QStringLiteral("line\nbreak.txt"), untracked(QStringLiteral("line\nbreak.txt"))},
        {QStringLiteral("build.log"),
         {.kind = GitStatusKind::Ignored, .path = QStringLiteral("build.log"), .origPath = {}, .index = {}, .worktree = {}, .actions = Action::Ignore}},
        {QStringLiteral("added.txt"), changed(QStringLiteral("added.txt"), FileState::Added, FileState::Unmodified, Action::Added)},
        {QStringLiteral("modified.txt"), changed(QStringLiteral("modified.txt"), FileState::Unmodified, FileState::Modified, Action::Modified)},
        {QStringLiteral("staged.txt"), changed(QStringLiteral("staged.txt"), FileState::Modified, FileState::Unmodified, Action::Modified)},
        {QStringLiteral("both.txt"), changed(QStringLiteral("both.txt"), FileState::Modified, FileState::Modified, Action::Modified)},
        {QStringLiteral("deleted.txt"), changed(QStringLiteral("deleted.txt"), FileState::Unmodified, FileState::Deleted, Action::Deleted)},
        {QStringLiteral("staged deleted.txt"), changed(QStringLiteral("staged deleted.txt"), FileState::Deleted, FileState::Unmodified, Action::Deleted)},
        {QStringLiteral("new näme.txt"),
         {.kind = GitStatusKind::RenamedOrCopied,
          .path = QStringLiteral("new näme.txt"),
          .origPath = QStringLiteral("old name.txt"),
          .index = FileState::Renamed,
          .worktree = FileState::Unmodified,
          .actions = Action::Replaced}},
        {QStringLiteral("ünïcödé €.txt"), changed(QStringLiteral("ünïcödé €.txt"), FileState::Unmodified, FileState::Modified, Action::Modified)},
        {QStringLiteral("dir with space/file with space.txt"),
         changed(QStringLiteral("dir with space/file with space.txt"), FileState::Unmodified, FileState::Modified, Action::Modified)},
    };
}

/**
 * @param output Malformed git status output.
 * @returns Whether parsing it gives a Parse error.
 */
bool isRejected(const QByteArray& output)
{
    const Result<QList<GitStatusEntry>> entries = GitStatus::parse(output);
    return !entries && entries.error().category == ErrorCategory::Parse;
}

/**
 * @param actual The status, by path.
 * @param expected What it should be, by path.
 * @returns Each path that is missing, unexpected or different.
 */
QStringList differingPaths(const QHash<QString, GitStatusEntry>& actual, const QHash<QString, GitStatusEntry>& expected)
{
    QStringList paths;
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        if (!actual.contains(it.key()) || actual.value(it.key()) != it.value()) {
            paths.append(it.key());
        }
    }
    for (auto it = actual.cbegin(); it != actual.cend(); ++it) {
        if (!expected.contains(it.key())) {
            paths.append(QStringLiteral("unexpected ") + it.key());
        }
    }
    return paths;
}

} // namespace

/**
 * GitStatus on real temporary repositories (one record per state), and its parser on malformed output.
 */
class GitStatusTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void reportsEachState()
    {
        const TestRepo repo;
        makeEveryState(repo);
        const QHash<QString, GitStatusEntry> status = statusByPath(repo, IgnoredFiles::Include);
        const QHash<QString, GitStatusEntry> expected = expectedEveryState();
        const QStringList wrong = differingPaths(status, expected);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral(", "))));
    }

    void excludesIgnoredByDefault()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "*.log\n");
        repo.write(QStringLiteral("build.log"), "ignored\n");
        const QHash<QString, GitStatusEntry> status = statusByPath(repo);
        QVERIFY(!status.contains(QStringLiteral("build.log")));
        QVERIFY(status.contains(QStringLiteral(".gitignore")));
    }

    void cleanTreeHasNoEntries()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("file.txt"), "content\n");
        repo.commitAll(QStringLiteral("initial"));
        QVERIFY(statusByPath(repo, IgnoredFiles::Include).isEmpty());
    }

    void reportsConflict()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("conflict ü.txt"), "base\n");
        repo.commitAll(QStringLiteral("base"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("other")});
        repo.write(QStringLiteral("conflict ü.txt"), "other\n");
        repo.commitAll(QStringLiteral("other"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(QStringLiteral("conflict ü.txt"), "main\n");
        repo.commitAll(QStringLiteral("main"));
        // The merge stops on the conflict with a non-zero exit
        QVERIFY_THROWS_EXCEPTION(RuntimeError, repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("other")}));

        const QHash<QString, GitStatusEntry> status = statusByPath(repo);
        QCOMPARE(status.size(), 1);
        const GitStatusEntry expected{.kind = GitStatusKind::Unmerged,
                                      .path = QStringLiteral("conflict ü.txt"),
                                      .origPath = {},
                                      .index = FileState::Unmerged,
                                      .worktree = FileState::Unmerged,
                                      .actions = Action::Unmerged};
        QVERIFY(status.value(QStringLiteral("conflict ü.txt")) == expected);
    }

    void parsesEmptyOutput()
    {
        const Result<QList<GitStatusEntry>> entries = GitStatus::parse(QByteArray());
        QVERIFY(entries);
        QVERIFY(entries->isEmpty());
    }

    void skipsHeaders()
    {
        const Result<QList<GitStatusEntry>> entries = GitStatus::parse(QByteArrayLiteral("# branch.oid abc\0? a b\0"));
        QVERIFY(entries);
        QCOMPARE(entries->size(), 1);
        QCOMPARE(entries->first().path, QStringLiteral("a b"));
    }

    void rejectsMalformedRecords()
    {
        const QList<QByteArray> malformed{
            QByteArrayLiteral("x path\0"), // Unknown type
            QByteArrayLiteral("?path\0"), // No space after the type
            QByteArrayLiteral("? \0"), // No path
            QByteArrayLiteral("1 .M N... 100644 100644\0"), // Too few fields
            QByteArrayLiteral("1 .Z N... 100644 100644 100644 abc abc path\0"), // Unknown status letter
            QByteArrayLiteral("1 .MM N... 100644 100644 100644 abc abc path\0"), // Three status letters
            QByteArrayLiteral("2 R. N... 100644 100644 100644 abc abc R100 new\0"), // Rename without its source
        };
        QVERIFY(std::ranges::all_of(malformed, isRejected));
    }

    void againstParentListsLastCommitsFiles()
    {
        const TestRepo repo;
        for (const char *path : {"a.txt", "b.txt", "c.txt", "gone.txt", "old.txt"}) {
            repo.write(QString::fromLatin1(path), QByteArray("several lines of\ncontent so that\na rename is found\nin ") + path + '\n');
        }
        repo.commitAll(QStringLiteral("base"));
        repo.write(QStringLiteral("a.txt"), "changed\n");
        repo.write(QStringLiteral("c.txt"), "changed\n");
        repo.remove(QStringLiteral("gone.txt"));
        repo.write(QStringLiteral("new.txt"), "new\n");
        repo.run({QStringLiteral("mv"), QStringLiteral("old.txt"), QStringLiteral("renamed.txt")});
        repo.commitAll(QStringLiteral("last"));
        // a.txt: the index goes back to HEAD~1, the working tree keeps HEAD's change
        repo.run({QStringLiteral("reset"), QStringLiteral("--quiet"), QStringLiteral("HEAD~1"), QStringLiteral("--"), QStringLiteral("a.txt")});
        // c.txt: back to HEAD~1 in both, so nothing differs from it
        repo.run({QStringLiteral("checkout"), QStringLiteral("HEAD~1"), QStringLiteral("--"), QStringLiteral("c.txt")});
        repo.write(QStringLiteral("b.txt"), "changed\n");
        repo.write(QStringLiteral("untracked.txt"), "untracked\n");

        const QList<GitStatusEntry> status = GitStatus::status(repo.git());
        const Result<QList<GitStatusEntry>> merged =
            GitStatus::withIndexDiff(status, repo.git().run(GitStatus::indexDiffArguments(QStringLiteral("HEAD~1")), RunMode::ReadOnly));
        QVERIFY(merged);

        const QList<GitStatusEntry> expected{
            changed(QStringLiteral("a.txt"), FileState::Unmodified, FileState::Modified, Action::Modified),
            changed(QStringLiteral("b.txt"), FileState::Unmodified, FileState::Modified, Action::Modified),
            changed(QStringLiteral("gone.txt"), FileState::Deleted, FileState::Unmodified, Action::Deleted),
            changed(QStringLiteral("new.txt"), FileState::Added, FileState::Unmodified, Action::Added),
            GitStatusEntry{.kind = GitStatusKind::RenamedOrCopied,
                           .path = QStringLiteral("renamed.txt"),
                           .origPath = QStringLiteral("old.txt"),
                           .index = FileState::Renamed,
                           .worktree = FileState::Unmodified,
                           .actions = Action::Replaced},
            GitStatusEntry{.kind = GitStatusKind::Untracked,
                           .path = QStringLiteral("untracked.txt"),
                           .origPath = {},
                           .index = FileState::Unmodified,
                           .worktree = FileState::Unmodified,
                           .actions = Action::Unversioned},
        };
        QCOMPARE(*merged, expected);
    }
};

QTEST_GUILESS_MAIN(GitStatusTest)

#include "gitstatustest.moc"
