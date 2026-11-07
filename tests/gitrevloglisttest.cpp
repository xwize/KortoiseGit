// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/gitrevloglist.hpp"
#include "testrepo.hpp"

#include <QTest>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

/**
 * @param repo The repository.
 * @param paths The path filter.
 * @returns git log's output, parsed in one go.
 */
QList<GitRevLoglist> parseLog(const TestRepo& repo, const QStringList& paths = {})
{
    QByteArray output = repo.git().run(GitRevLoglist::logArguments(paths));
    Result<QList<GitRevLoglist>> revs = GitRevLoglist::takeRecords(output);
    if (!revs) {
        qWarning() << revs.error().message;
        return {};
    }
    if (!output.isEmpty()) {
        qWarning() << "Left over:" << output;
    }
    return *revs;
}

/**
 * @param repo The repository.
 * @param rev A commit.
 * @returns Its changed files.
 */
GitRevFiles parseFiles(const TestRepo& repo, const GitRevLoglist& rev)
{
    const Result<GitRevFiles> files = GitRevLoglist::parseFiles(repo.git().run(GitRevLoglist::filesArguments(rev)));
    if (!files) {
        qWarning() << files.error().message;
        return {};
    }
    return *files;
}

} // namespace

/**
 * Reading git log and git diff-tree output, on throwaway repositories with merges and odd messages.
 */
class GitRevLoglistTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void parsesHistoryWithMergesAndOddMessages()
    {
        TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("First"));
        repo.write(QStringLiteral("a.txt"), "b\n");
        repo.commitAll(QStringLiteral("Subject line\n\nBody paragraph one.\n\nBody, \"quoted\" & <tagged>.\n\n\n"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("side")});
        repo.write(QStringLiteral("side.txt"), "side\n");
        repo.commitAll(QStringLiteral("Ünïcödé 日本語 \t tabs"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.run({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("--allow-empty"), QStringLiteral("--allow-empty-message"), QStringLiteral("-m"), QString()});
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("--no-ff"), QStringLiteral("-m"), QStringLiteral("Merge side"), QStringLiteral("side")});
        repo.run({QStringLiteral("tag"), QStringLiteral("-a"), QStringLiteral("v1.0"), QStringLiteral("-m"), QStringLiteral("Release")});
        repo.run({QStringLiteral("tag"), QStringLiteral("light"), QStringLiteral("HEAD~1")});

        const QList<GitRevLoglist> revs = parseLog(repo);
        QCOMPARE(revs.size(), 5);

        const GitRevLoglist& merge = revs.at(0);
        QCOMPARE(merge.subject, QStringLiteral("Merge side"));
        QCOMPARE(merge.parents.size(), 2);
        QCOMPARE(merge.authorName, QStringLiteral("Test"));
        QCOMPARE(merge.authorEmail, QStringLiteral("test@example.com"));
        QVERIFY(merge.authorDate.isValid());
        QCOMPARE(merge.hash, QString::fromLatin1(repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).trimmed()));
        QCOMPARE(merge.refs,
                 (QList<GitRef>{
                     {.type = GitRef::Type::LocalBranch, .name = QStringLiteral("main"), .isCurrent = true},
                     {.type = GitRef::Type::Tag, .name = QStringLiteral("v1.0"), .isCurrent = false},
                 }));

        // Topological order: every commit comes before its parents
        for (qsizetype i = 0; i < revs.size(); ++i) {
            for (const QString& parent : revs.at(i).parents) {
                const auto parentIndex = std::ranges::find(revs, parent, &GitRevLoglist::hash) - revs.cbegin();
                QVERIFY(parentIndex > i);
            }
        }

        const auto find = [&revs](const QString& subject) {
            return *std::ranges::find(revs, subject, &GitRevLoglist::subject);
        };
        const GitRevLoglist empty = find(QString());
        QVERIFY(empty.body.isEmpty());
        QCOMPARE(empty.refs, (QList<GitRef>{{.type = GitRef::Type::Tag, .name = QStringLiteral("light"), .isCurrent = false}}));
        const GitRevLoglist unicode = find(QStringLiteral("Ünïcödé 日本語 \t tabs"));
        QCOMPARE(unicode.refs, (QList<GitRef>{{.type = GitRef::Type::LocalBranch, .name = QStringLiteral("side"), .isCurrent = false}}));
        const GitRevLoglist multi = find(QStringLiteral("Subject line"));
        QCOMPARE(multi.body, QStringLiteral("Body paragraph one.\n\nBody, \"quoted\" & <tagged>."));
        const GitRevLoglist first = find(QStringLiteral("First"));
        QVERIFY(first.parents.isEmpty());
        QVERIFY(first.refs.isEmpty());
    }

    void parsesOutputInPieces()
    {
        TestRepo repo;
        for (int i = 0; i < 20; ++i) {
            repo.write(QStringLiteral("f.txt"), QByteArray::number(i));
            repo.commitAll(QStringLiteral("Commit %1\n\nBody %1").arg(i));
        }
        const QByteArray output = repo.git().run(GitRevLoglist::logArguments());
        const QList<GitRevLoglist> whole = parseLog(repo);
        QCOMPARE(whole.size(), 20);

        // As GitLogList reads it while git runs: small chunks that split records and fields anywhere
        constexpr qsizetype chunkSize = 7;
        QList<GitRevLoglist> pieces;
        QByteArray buffer;
        for (qsizetype pos = 0; pos < output.size(); pos += chunkSize) {
            buffer.append(output.mid(pos, chunkSize));
            Result<QList<GitRevLoglist>> revs = GitRevLoglist::takeRecords(buffer);
            QVERIFY(revs);
            pieces.append(*revs);
        }
        QVERIFY(buffer.isEmpty());
        QCOMPARE(pieces, whole);
    }

    void limitsToPath()
    {
        TestRepo repo;
        repo.write(QStringLiteral("dir/a.txt"), "1");
        repo.commitAll(QStringLiteral("Add a"));
        repo.write(QStringLiteral("b.txt"), "1");
        repo.commitAll(QStringLiteral("Add b"));
        repo.write(QStringLiteral("dir/a.txt"), "2");
        repo.commitAll(QStringLiteral("Change a"));

        QCOMPARE(parseLog(repo).size(), 3);
        const QList<GitRevLoglist> revs = parseLog(repo, {QStringLiteral("dir")});
        QCOMPARE(revs.size(), 2);
        QCOMPARE(revs.at(0).subject, QStringLiteral("Change a"));
        QCOMPARE(revs.at(1).subject, QStringLiteral("Add a"));
    }

    void rejectsBadRecord()
    {
        QByteArray buffer("not a hash\0p\0n\0e\0123\0\0s\0b\0\0", 26);
        QVERIFY(!GitRevLoglist::takeRecords(buffer));
    }

    void parsesRefs_data()
    {
        QTest::addColumn<QString>("decoration");
        QTest::addColumn<QList<GitRef>>("refs");

        QTest::newRow("empty") << QString() << QList<GitRef>();
        QTest::newRow("current branch, remote and tag")
            << QStringLiteral("HEAD -> refs/heads/main, refs/remotes/origin/main, tag: refs/tags/v1.0")
            << QList<GitRef>{
                   {.type = GitRef::Type::LocalBranch, .name = QStringLiteral("main"), .isCurrent = true},
                   {.type = GitRef::Type::RemoteBranch, .name = QStringLiteral("origin/main"), .isCurrent = false},
                   {.type = GitRef::Type::Tag, .name = QStringLiteral("v1.0"), .isCurrent = false},
               };
        QTest::newRow("detached HEAD and stash")
            << QStringLiteral("HEAD, refs/heads/feature/x, refs/stash")
            << QList<GitRef>{
                   {.type = GitRef::Type::Head, .name = QStringLiteral("HEAD"), .isCurrent = true},
                   {.type = GitRef::Type::LocalBranch, .name = QStringLiteral("feature/x"), .isCurrent = false},
                   {.type = GitRef::Type::Stash, .name = QStringLiteral("stash"), .isCurrent = false},
               };
        QTest::newRow("other") << QStringLiteral("refs/notes/commits")
                               << QList<GitRef>{{.type = GitRef::Type::Other, .name = QStringLiteral("refs/notes/commits"), .isCurrent = false}};
    }

    void parsesRefs()
    {
        QFETCH(QString, decoration);
        QFETCH(QList<GitRef>, refs);
        QCOMPARE(GitRevLoglist::parseRefs(decoration), refs);
    }

    void parsesChangedFiles()
    {
        TestRepo repo;
        repo.write(QStringLiteral("keep.txt"), "one\ntwo\nthree\n");
        repo.write(QStringLiteral("gone.txt"), "gone\n");
        repo.write(QStringLiteral("old name.txt"), "a long enough line to be found as a rename\nand another one\n");
        repo.commitAll(QStringLiteral("Root"));
        repo.write(QStringLiteral("keep.txt"), "one\n2\nthree\nfour\n");
        repo.remove(QStringLiteral("gone.txt"));
        repo.run({QStringLiteral("mv"), QStringLiteral("old name.txt"), QStringLiteral("new name.txt")});
        repo.write(QStringLiteral("bin.dat"), QByteArray("\0\1\2", 3));
        repo.commitAll(QStringLiteral("Change"));

        const QList<GitRevLoglist> revs = parseLog(repo);
        QCOMPARE(revs.size(), 2);

        const GitRevFiles root = parseFiles(repo, revs.at(1));
        QCOMPARE(root.status.size(), 3);
        for (const GitStatusEntry& entry : root.status) {
            QCOMPARE(entry.actions, TGitPath::Actions(Action::Added));
        }
        QCOMPARE(root.numStat.size(), 3);

        const GitRevFiles change = parseFiles(repo, revs.at(0));
        QHash<QString, GitStatusEntry> byPath;
        for (const GitStatusEntry& entry : change.status) {
            byPath.insert(entry.path, entry);
        }
        QCOMPARE(byPath.size(), 4);
        QCOMPARE(byPath.value(QStringLiteral("keep.txt")).actions, TGitPath::Actions(Action::Modified));
        QCOMPARE(byPath.value(QStringLiteral("gone.txt")).actions, TGitPath::Actions(Action::Deleted));
        QCOMPARE(byPath.value(QStringLiteral("bin.dat")).actions, TGitPath::Actions(Action::Added));
        const GitStatusEntry renamed = byPath.value(QStringLiteral("new name.txt"));
        QCOMPARE(renamed.actions, TGitPath::Actions(Action::Replaced));
        QCOMPARE(renamed.origPath, QStringLiteral("old name.txt"));

        QHash<QString, NumStatEntry> stats;
        for (const NumStatEntry& stat : change.numStat) {
            stats.insert(stat.path, stat);
        }
        QCOMPARE(stats.value(QStringLiteral("keep.txt")).added, QStringLiteral("2"));
        QCOMPARE(stats.value(QStringLiteral("keep.txt")).removed, QStringLiteral("1"));
        QCOMPARE(stats.value(QStringLiteral("bin.dat")).added, QStringLiteral("-"));
        QCOMPARE(stats.value(QStringLiteral("new name.txt")).origPath, QStringLiteral("old name.txt"));
    }

    void parsesMergeAgainstFirstParent()
    {
        TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("Root"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("-c"), QStringLiteral("side")});
        repo.write(QStringLiteral("side.txt"), "side\n");
        repo.commitAll(QStringLiteral("Side"));
        repo.run({QStringLiteral("switch"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.write(QStringLiteral("main.txt"), "main\n");
        repo.commitAll(QStringLiteral("Main"));
        repo.run({QStringLiteral("merge"), QStringLiteral("--quiet"), QStringLiteral("--no-ff"), QStringLiteral("-m"), QStringLiteral("Merge"), QStringLiteral("side")});

        const GitRevFiles files = parseFiles(repo, parseLog(repo).constFirst());
        QCOMPARE(files.status.size(), 1);
        QCOMPARE(files.status.constFirst().path, QStringLiteral("side.txt"));
    }
};

QTEST_GUILESS_MAIN(GitRevLoglistTest)

#include "gitrevloglisttest.moc"
