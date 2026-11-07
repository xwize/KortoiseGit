// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitstatus.hpp"
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
 * A throwaway repository on branch main, with git's identity and configuration fixed by the test case.
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
     * @param arguments The arguments after "git".
     */
    void git(const QStringList& arguments) const
    {
        std::ignore = m_git.run(arguments);
    }

    /**
     * Creates or overwrites a file, and any directories above it.
     * @param path Relative to the root.
     * @param content What the file holds.
     */
    void write(const QString& path, const QByteArray& content) const
    {
        const QString fullPath = m_dir.filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(fullPath).absolutePath()));
        QFile file(fullPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(content), content.size());
    }

    /**
     * @param path Relative to the root.
     */
    void remove(const QString& path) const
    {
        QVERIFY(QFile::remove(m_dir.filePath(path)));
    }

    /**
     * Stages and commits everything.
     * @param message The commit message.
     */
    void commitAll(const QString& message) const
    {
        git({QStringLiteral("add"), QStringLiteral("--all")});
        git({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), message});
    }

    /**
     * @param ignored Whether to list ignored files.
     * @returns The status, by path.
     */
    [[nodiscard]] QHash<QString, GitStatusEntry> status(IgnoredFiles ignored = IgnoredFiles::Exclude) const
    {
        QHash<QString, GitStatusEntry> byPath;
        for (const GitStatusEntry& entry : GitStatus::status(m_git, ignored)) {
            byPath.insert(entry.path, entry);
        }
        return byPath;
    }

private:
    QTemporaryDir m_dir;
    Git m_git;
};

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
    repo.git({QStringLiteral("add"), QStringLiteral("added.txt")});
    repo.write(QStringLiteral("modified.txt"), "changed\n");
    repo.write(QStringLiteral("staged.txt"), "changed\n");
    repo.git({QStringLiteral("add"), QStringLiteral("staged.txt")});
    repo.write(QStringLiteral("both.txt"), "staged change\n");
    repo.git({QStringLiteral("add"), QStringLiteral("both.txt")});
    repo.write(QStringLiteral("both.txt"), "further change\n");
    repo.remove(QStringLiteral("deleted.txt"));
    repo.git({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("staged deleted.txt")});
    repo.git({QStringLiteral("mv"), QStringLiteral("old name.txt"), QStringLiteral("new näme.txt")});
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
        // Keep the user's own git configuration (signing, hooks, renames off) out of the tests
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
    }

    void reportsEachState()
    {
        const TestRepo repo;
        makeEveryState(repo);
        const QHash<QString, GitStatusEntry> status = repo.status(IgnoredFiles::Include);
        const QHash<QString, GitStatusEntry> expected = expectedEveryState();
        const QStringList wrong = differingPaths(status, expected);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral(", "))));
    }

    void excludesIgnoredByDefault()
    {
        const TestRepo repo;
        repo.write(QStringLiteral(".gitignore"), "*.log\n");
        repo.write(QStringLiteral("build.log"), "ignored\n");
        const QHash<QString, GitStatusEntry> status = repo.status();
        QVERIFY(!status.contains(QStringLiteral("build.log")));
        QVERIFY(status.contains(QStringLiteral(".gitignore")));
    }

    void cleanTreeHasNoEntries()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("file.txt"), "content\n");
        repo.commitAll(QStringLiteral("initial"));
        QVERIFY(repo.status(IgnoredFiles::Include).isEmpty());
    }

    void reportsConflict()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("conflict ü.txt"), "base\n");
        repo.commitAll(QStringLiteral("base"));
        repo.git({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("other")});
        repo.write(QStringLiteral("conflict ü.txt"), "other\n");
        repo.commitAll(QStringLiteral("other"));
        repo.git({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(QStringLiteral("conflict ü.txt"), "main\n");
        repo.commitAll(QStringLiteral("main"));
        // The merge stops on the conflict with a non-zero exit
        QVERIFY_THROWS_EXCEPTION(RuntimeError, repo.git({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("other")}));

        const QHash<QString, GitStatusEntry> status = repo.status();
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
};

QTEST_GUILESS_MAIN(GitStatusTest)

#include "gitstatustest.moc"
