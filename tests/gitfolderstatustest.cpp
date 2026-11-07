// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitfolderstatus.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QTemporaryDir>
#include <QTest>

#include <tuple>

namespace
{

using namespace kgt;
using ItemVersion = KVersionControlPlugin::ItemVersion;

/**
 * One item and the state it should show.
 */
struct Expected {
    QString name; // The item's name in the folder
    bool isDir = false; // Whether it is a folder
    ItemVersion version = KVersionControlPlugin::NormalVersion; // The state it should show
};

/**
 * @returns An entry for a tracked file.
 */
GitStatusEntry tracked(const QString& path, FileState index, FileState worktree)
{
    return {.kind = GitStatusKind::Changed, .path = path, .origPath = {}, .index = index, .worktree = worktree, .actions = {}};
}

/**
 * @returns An entry of a kind with no index or worktree state (untracked, ignored).
 */
GitStatusEntry ofKind(GitStatusKind kind, const QString& path)
{
    return {.kind = kind, .path = path, .origPath = {}, .index = {}, .worktree = {}, .actions = {}};
}

/**
 * @returns A conflict entry.
 */
GitStatusEntry conflict(const QString& path)
{
    return {.kind = GitStatusKind::Unmerged, .path = path, .origPath = {}, .index = FileState::Unmerged, .worktree = FileState::Unmerged, .actions = {}};
}

/**
 * @param status A folder's state.
 * @param expected What each item should show.
 * @returns A description of each item that shows something else.
 */
QStringList mismatches(const GitFolderStatus& status, const QList<Expected>& expected)
{
    QStringList wrong;
    for (const Expected& item : expected) {
        const ItemVersion actual = status.itemVersion(item.name, item.isDir);
        if (actual != item.version) {
            wrong.append(QStringLiteral("%1: %2, expected %3").arg(item.name).arg(actual).arg(item.version));
        }
    }
    return wrong;
}

} // namespace

/**
 * GitFolderStatus: each git state's overlay, TortoiseGit's folder roll-up, and reading real repositories,
 * worktrees and submodules.
 */
class GitFolderStatusTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void mapsEachFileState()
    {
        const QList<std::pair<GitStatusEntry, ItemVersion>> cases{
            {ofKind(GitStatusKind::Untracked, QStringLiteral("f")), KVersionControlPlugin::UnversionedVersion},
            {ofKind(GitStatusKind::Ignored, QStringLiteral("f")), KVersionControlPlugin::IgnoredVersion},
            {tracked(QStringLiteral("f"), FileState::Added, FileState::Unmodified), KVersionControlPlugin::AddedVersion},
            {tracked(QStringLiteral("f"), FileState::Copied, FileState::Unmodified), KVersionControlPlugin::AddedVersion},
            {tracked(QStringLiteral("f"), FileState::Modified, FileState::Unmodified), KVersionControlPlugin::LocallyModifiedVersion},
            {tracked(QStringLiteral("f"), FileState::Renamed, FileState::Unmodified), KVersionControlPlugin::LocallyModifiedVersion},
            {tracked(QStringLiteral("f"), FileState::TypeChanged, FileState::Unmodified), KVersionControlPlugin::LocallyModifiedVersion},
            {tracked(QStringLiteral("f"), FileState::Unmodified, FileState::Modified), KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {tracked(QStringLiteral("f"), FileState::Modified, FileState::Modified), KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {tracked(QStringLiteral("f"), FileState::Added, FileState::Modified), KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {tracked(QStringLiteral("f"), FileState::Deleted, FileState::Unmodified), KVersionControlPlugin::RemovedVersion},
            {tracked(QStringLiteral("f"), FileState::Unmodified, FileState::Deleted), KVersionControlPlugin::MissingVersion},
            {conflict(QStringLiteral("f")), KVersionControlPlugin::ConflictingVersion},
        };
        QStringList wrong;
        for (const auto& [entry, version] : cases) {
            if (GitFolderStatus::fileVersion(entry) != version) {
                wrong.append(QStringLiteral("index %1 worktree %2").arg(static_cast<int>(entry.index)).arg(static_cast<int>(entry.worktree)));
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
    }

    void rollsUpFolders()
    {
        const QList<GitStatusEntry> entries{
            tracked(QStringLiteral("conflicted/a.txt"), FileState::Unmodified, FileState::Modified),
            conflict(QStringLiteral("conflicted/b.txt")),
            tracked(QStringLiteral("conflicted/c.txt"), FileState::Added, FileState::Unmodified),
            tracked(QStringLiteral("unstaged/a.txt"), FileState::Modified, FileState::Unmodified),
            tracked(QStringLiteral("unstaged/sub/b.txt"), FileState::Unmodified, FileState::Modified),
            tracked(QStringLiteral("staged/a.txt"), FileState::Modified, FileState::Unmodified),
            ofKind(GitStatusKind::Untracked, QStringLiteral("staged/new.txt")),
            tracked(QStringLiteral("added/a.txt"), FileState::Added, FileState::Unmodified),
            tracked(QStringLiteral("removed/a.txt"), FileState::Deleted, FileState::Unmodified),
            tracked(QStringLiteral("missing/a.txt"), FileState::Unmodified, FileState::Deleted),
            ofKind(GitStatusKind::Untracked, QStringLiteral("untracked/a.txt")),
            ofKind(GitStatusKind::Untracked, QStringLiteral("untracked/deeper/b.txt")),
            ofKind(GitStatusKind::Ignored, QStringLiteral("ignoredfiles/a.log")),
            ofKind(GitStatusKind::Untracked, QStringLiteral("mixed/a.txt")),
            ofKind(GitStatusKind::Ignored, QStringLiteral("mixed/b.log")),
            ofKind(GitStatusKind::Untracked, QStringLiteral("trackedwithnew/new.txt")),
            ofKind(GitStatusKind::Ignored, QStringLiteral("build/")),
            ofKind(GitStatusKind::Untracked, QStringLiteral("nested/")),
            tracked(QStringLiteral("deep/a/b/c/d.txt"), FileState::Unmodified, FileState::Modified),
            {.kind = GitStatusKind::RenamedOrCopied,
             .path = QStringLiteral("to/x.txt"),
             .origPath = QStringLiteral("from/x.txt"),
             .index = FileState::Renamed,
             .worktree = FileState::Unmodified,
             .actions = {}},
            tracked(QStringLiteral("submodule"), FileState::Unmodified, FileState::Modified),
            tracked(QStringLiteral("file.txt"), FileState::Unmodified, FileState::Modified),
        };
        const QSet<QString> trackedChildren{QStringLiteral("conflicted"),
                                            QStringLiteral("unstaged"),
                                            QStringLiteral("staged"),
                                            QStringLiteral("removed"),
                                            QStringLiteral("missing"),
                                            QStringLiteral("trackedwithnew"),
                                            QStringLiteral("clean"),
                                            QStringLiteral("from"),
                                            QStringLiteral("deep"),
                                            QStringLiteral("submodule"),
                                            QStringLiteral("file.txt"),
                                            QStringLiteral("clean.txt")};
        const GitFolderStatus status(QString(), entries, trackedChildren);

        const QList<Expected> expected{
            {.name = QStringLiteral("conflicted"), .isDir = true, .version = KVersionControlPlugin::ConflictingVersion},
            {.name = QStringLiteral("unstaged"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {.name = QStringLiteral("staged"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedVersion},
            {.name = QStringLiteral("added"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedVersion},
            {.name = QStringLiteral("removed"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedVersion},
            {.name = QStringLiteral("missing"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {.name = QStringLiteral("untracked"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
            {.name = QStringLiteral("ignoredfiles"), .isDir = true, .version = KVersionControlPlugin::IgnoredVersion},
            {.name = QStringLiteral("mixed"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
            {.name = QStringLiteral("trackedwithnew"), .isDir = true, .version = KVersionControlPlugin::NormalVersion},
            {.name = QStringLiteral("clean"), .isDir = true, .version = KVersionControlPlugin::NormalVersion},
            {.name = QStringLiteral("emptyuntracked"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
            {.name = QStringLiteral("build"), .isDir = true, .version = KVersionControlPlugin::IgnoredVersion},
            {.name = QStringLiteral("nested"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
            {.name = QStringLiteral("deep"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {.name = QStringLiteral("to"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedVersion},
            {.name = QStringLiteral("from"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedVersion},
            {.name = QStringLiteral("submodule"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {.name = QStringLiteral("file.txt"), .isDir = false, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
            {.name = QStringLiteral("clean.txt"), .isDir = false, .version = KVersionControlPlugin::NormalVersion},
            {.name = QStringLiteral(".git"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
        };
        const QStringList wrong = mismatches(status, expected);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
    }

    void keepsTheMoreImportantOfTwoEntries()
    {
        // git rm --cached lists the file both as deleted from the index and as untracked
        const GitFolderStatus status(QString(),
                                     {ofKind(GitStatusKind::Untracked, QStringLiteral("a.txt")),
                                      tracked(QStringLiteral("a.txt"), FileState::Deleted, FileState::Unmodified),
                                      ofKind(GitStatusKind::Untracked, QStringLiteral("b.txt"))},
                                     {});
        QCOMPARE(status.itemVersion(QStringLiteral("a.txt"), false), KVersionControlPlugin::RemovedVersion);
        QCOMPARE(status.itemVersion(QStringLiteral("b.txt"), false), KVersionControlPlugin::UnversionedVersion);
    }

    void looksOnlyBelowItsPrefix()
    {
        const GitFolderStatus status(QStringLiteral("src/"),
                                     {tracked(QStringLiteral("a.txt"), FileState::Unmodified, FileState::Modified),
                                      tracked(QStringLiteral("srcfile.txt"), FileState::Unmodified, FileState::Modified),
                                      tracked(QStringLiteral("src/a.txt"), FileState::Added, FileState::Unmodified),
                                      tracked(QStringLiteral("src/lib/b.txt"), FileState::Unmodified, FileState::Modified)},
                                     {QStringLiteral("lib")});
        QCOMPARE(status.itemVersion(QStringLiteral("a.txt"), false), KVersionControlPlugin::AddedVersion);
        QCOMPARE(status.itemVersion(QStringLiteral("lib"), true), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
        QCOMPARE(status.itemVersion(QStringLiteral("srcfile.txt"), false), KVersionControlPlugin::NormalVersion);
    }

    void knowsWhichFoldersHoldUnversionedFiles()
    {
        const GitFolderStatus status(QString(),
                                     {ofKind(GitStatusKind::Untracked, QStringLiteral("tracked/deeper/new.txt")),
                                      ofKind(GitStatusKind::Ignored, QStringLiteral("ignored/a.log")),
                                      tracked(QStringLiteral("modified/a.txt"), FileState::Unmodified, FileState::Modified)},
                                     {QStringLiteral("tracked"), QStringLiteral("modified")});
        QVERIFY(status.hasUnversioned(QStringLiteral("tracked")));
        QVERIFY(!status.hasUnversioned(QStringLiteral("ignored")));
        QVERIFY(!status.hasUnversioned(QStringLiteral("modified")));
        QVERIFY(!status.hasUnversioned(QStringLiteral("absent")));
    }

    void insideAnIgnoredFolderEverythingIsIgnored()
    {
        const GitFolderStatus status(QStringLiteral("build/out/"), {ofKind(GitStatusKind::Ignored, QStringLiteral("build/"))}, {});
        QCOMPARE(status.itemVersion(QStringLiteral("x.o"), false), KVersionControlPlugin::IgnoredVersion);
        QCOMPARE(status.itemVersion(QStringLiteral("sub"), true), KVersionControlPlugin::IgnoredVersion);
    }

    void fetchesARepository()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("clean.txt"), "clean\n");
        repo.write(QStringLiteral("modified.txt"), "original\n");
        repo.write(QStringLiteral("sub dir/ünï.txt"), "original\n");
        repo.write(QStringLiteral("sub dir/staged.txt"), "original\n");
        repo.write(QStringLiteral(".gitignore"), "build/\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.write(QStringLiteral("modified.txt"), "changed\n");
        repo.write(QStringLiteral("sub dir/ünï.txt"), "changed\n");
        repo.write(QStringLiteral("sub dir/staged.txt"), "changed\n");
        repo.run({QStringLiteral("add"), QStringLiteral("sub dir/staged.txt")});
        repo.write(QStringLiteral("sub dir/new.txt"), "new\n");
        repo.write(QStringLiteral("build/out/x.o"), "object\n");
        repo.write(QStringLiteral("untracked dir/a.txt"), "new\n");

        const GitFolderStatus root = GitFolderStatus::fetch(repo.path() + QLatin1Char('/'));
        QVERIFY(root.indexFile().endsWith(QStringLiteral("/.git/index")));
        const QStringList rootWrong =
            mismatches(root,
                       {{.name = QStringLiteral("clean.txt"), .isDir = false, .version = KVersionControlPlugin::NormalVersion},
                        {.name = QStringLiteral("modified.txt"), .isDir = false, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
                        {.name = QStringLiteral("sub dir"), .isDir = true, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
                        {.name = QStringLiteral("build"), .isDir = true, .version = KVersionControlPlugin::IgnoredVersion},
                        {.name = QStringLiteral("untracked dir"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion},
                        {.name = QStringLiteral(".git"), .isDir = true, .version = KVersionControlPlugin::UnversionedVersion}});
        QVERIFY2(rootWrong.isEmpty(), qPrintable(rootWrong.join(QStringLiteral("; "))));

        const GitFolderStatus sub = GitFolderStatus::fetch(repo.filePath(QStringLiteral("sub dir/")));
        const QStringList subWrong =
            mismatches(sub,
                       {{.name = QStringLiteral("ünï.txt"), .isDir = false, .version = KVersionControlPlugin::LocallyModifiedUnstagedVersion},
                        {.name = QStringLiteral("staged.txt"), .isDir = false, .version = KVersionControlPlugin::LocallyModifiedVersion},
                        {.name = QStringLiteral("new.txt"), .isDir = false, .version = KVersionControlPlugin::UnversionedVersion}});
        QVERIFY2(subWrong.isEmpty(), qPrintable(subWrong.join(QStringLiteral("; "))));

        const GitFolderStatus ignored = GitFolderStatus::fetch(repo.filePath(QStringLiteral("build/out/")));
        QCOMPARE(ignored.itemVersion(QStringLiteral("x.o"), false), KVersionControlPlugin::IgnoredVersion);
    }

    void failsOutsideAWorkingTree()
    {
        const QTemporaryDir notARepo;
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = GitFolderStatus::fetch(notARepo.path() + QLatin1Char('/')));

        const TestRepo repo;
        QVERIFY_THROWS_EXCEPTION(RuntimeError, std::ignore = GitFolderStatus::fetch(repo.filePath(QStringLiteral(".git/"))));
    }

    void handlesARepositoryWithoutCommits()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("staged/a.txt"), "a\n");
        repo.run({QStringLiteral("add"), QStringLiteral("staged")});
        repo.write(QStringLiteral("new.txt"), "new\n");
        const GitFolderStatus status = GitFolderStatus::fetch(repo.path() + QLatin1Char('/'));
        QCOMPARE(status.itemVersion(QStringLiteral("staged"), true), KVersionControlPlugin::LocallyModifiedVersion);
        QCOMPARE(status.itemVersion(QStringLiteral("new.txt"), false), KVersionControlPlugin::UnversionedVersion);
    }

    void handlesALinkedWorktree()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QTemporaryDir parent;
        const QString worktree = parent.filePath(QStringLiteral("worktree"));
        repo.run({QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("--quiet"), worktree});
        QVERIFY(QFileInfo(worktree + QStringLiteral("/.git")).isFile());

        const GitFolderStatus clean = GitFolderStatus::fetch(worktree + QLatin1Char('/'));
        QCOMPARE(clean.itemVersion(QStringLiteral("a.txt"), false), KVersionControlPlugin::NormalVersion);
        QCOMPARE(clean.itemVersion(QStringLiteral(".git"), false), KVersionControlPlugin::UnversionedVersion);
        // A linked worktree has its own index inside the main repository's .git/worktrees
        QVERIFY2(clean.indexFile().contains(QStringLiteral("/.git/worktrees/")), qPrintable(clean.indexFile()));

        QFile file(worktree + QStringLiteral("/a.txt"));
        QVERIFY(file.open(QIODevice::Append));
        file.write("more\n");
        file.close();
        const GitFolderStatus changed = GitFolderStatus::fetch(worktree + QLatin1Char('/'));
        QCOMPARE(changed.itemVersion(QStringLiteral("a.txt"), false), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
    }

    void handlesASubmodule()
    {
        const TestRepo library;
        library.write(QStringLiteral("lib.txt"), "lib\n");
        library.commitAll(QStringLiteral("library"));
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("-c"),
                  QStringLiteral("protocol.file.allow=always"),
                  QStringLiteral("submodule"),
                  QStringLiteral("--quiet"),
                  QStringLiteral("add"),
                  library.path(),
                  QStringLiteral("lib")});
        repo.commitAll(QStringLiteral("add submodule"));

        const GitFolderStatus parent = GitFolderStatus::fetch(repo.path() + QLatin1Char('/'));
        QCOMPARE(parent.itemVersion(QStringLiteral("lib"), true), KVersionControlPlugin::NormalVersion);

        // Inside, the submodule is a working tree of its own, whose .git is a file
        QVERIFY(QFileInfo(repo.filePath(QStringLiteral("lib/.git"))).isFile());
        repo.write(QStringLiteral("lib/lib.txt"), "changed\n");
        const GitFolderStatus inside = GitFolderStatus::fetch(repo.filePath(QStringLiteral("lib/")));
        QCOMPARE(inside.itemVersion(QStringLiteral("lib.txt"), false), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
        QCOMPARE(inside.itemVersion(QStringLiteral(".git"), false), KVersionControlPlugin::UnversionedVersion);

        const GitFolderStatus parentChanged = GitFolderStatus::fetch(repo.path() + QLatin1Char('/'));
        QCOMPARE(parentChanged.itemVersion(QStringLiteral("lib"), true), KVersionControlPlugin::LocallyModifiedUnstagedVersion);
    }
};

QTEST_GUILESS_MAIN(GitFolderStatusTest)

#include "gitfolderstatustest.moc"
