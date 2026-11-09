// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "git/gitrevloglist.hpp"
#include "reflogdlg.hpp"
#include "refloglist.hpp"
#include "testrepo.hpp"

#include <QDateTime>
#include <QTest>

using namespace kgt;

namespace
{

/**
 * @returns A commit's hash.
 */
QByteArray firstHash()
{
    return QByteArray(40, 'a');
}

/**
 * @returns Another commit's hash.
 */
QByteArray secondHash()
{
    return QByteArray(40, 'b');
}

/**
 * @param hash A commit.
 * @param seconds Its time.
 * @returns A reflog entry.
 */
GitRevLoglist entryOf(const QByteArray& hash, qint64 seconds)
{
    GitRevLoglist entry;
    entry.hash = QString::fromLatin1(hash);
    entry.commitDate = QDateTime::fromSecsSinceEpoch(seconds);
    return entry;
}

} // namespace

/**
 * The decisions of the RefLog dialog and its list that don't need the dialog.
 */
class RefLogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void parsesEntries()
    {
        QByteArray output = firstHash() + " refs/stash@{1700000000 +0100}: On main: fix: colon\n";
        output += "gpg: a signature check's line\n";
        output += secondHash() + " refs/stash@{1600000000 +0000}: update by push\n";
        const QList<GitRevLoglist> entries = GitRevLoglist::parseRefLog(output, QStringLiteral("refs/stash"));

        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).ref, QStringLiteral("refs/stash@{0}"));
        QCOMPARE(entries.at(0).hash, QString::fromLatin1(firstHash()));
        QCOMPARE(entries.at(0).refAction, QStringLiteral("On main"));
        QCOMPARE(entries.at(0).subject, QStringLiteral("fix: colon"));
        QCOMPARE(entries.at(0).commitDate.toSecsSinceEpoch(), 1700000000);
        // No ": " in the message: all of it is the action
        QCOMPARE(entries.at(1).ref, QStringLiteral("refs/stash@{1}"));
        QCOMPARE(entries.at(1).refAction, QStringLiteral("update by push"));
        QVERIFY(entries.at(1).subject.isEmpty());
    }

    void readsARealRefLogNewestFirst()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("first: one"));
        repo.write(QStringLiteral("a.txt"), "b\n");
        repo.commitAll(QStringLiteral("second"));

        const QString head = QStringLiteral("HEAD");
        const QByteArray output = repo.git().run(GitRevLoglist::refLogArguments(head), RunMode::ReadOnly);
        const QList<GitRevLoglist> entries = GitRevLoglist::parseRefLog(output, QStringLiteral("HEAD"));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).ref, QStringLiteral("HEAD@{0}"));
        QCOMPARE(entries.at(0).refAction, QStringLiteral("commit"));
        QCOMPARE(entries.at(0).subject, QStringLiteral("second"));
        QCOMPARE(entries.at(1).refAction, QStringLiteral("commit (initial)"));
        QCOMPARE(entries.at(1).subject, QStringLiteral("first: one"));
    }

    void hasRefLogFollowsTheReflogFile()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        const QString stash = QStringLiteral("refs/stash");
        QVERIFY(repo.git().hasRefLog(QStringLiteral("HEAD")));
        // A tag has no reflog; nor does a ref that doesn't exist
        QVERIFY(!repo.git().hasRefLog(QStringLiteral("refs/tags/v1")));
        QVERIFY(!repo.git().hasRefLog(stash));

        repo.write(QStringLiteral("a.txt"), "b\n");
        repo.run({QStringLiteral("stash"), QStringLiteral("push")});
        // A packed ref has no file of its own, but still its reflog
        repo.run({QStringLiteral("pack-refs"), QStringLiteral("--all")});
        QVERIFY(repo.git().hasRefLog(stash));

        // git reflog show would fail here
        repo.run({QStringLiteral("stash"), QStringLiteral("clear")});
        QVERIFY(!repo.git().hasRefLog(stash));
    }

    void refListHasPeeledTagsOnce()
    {
        QByteArray output = firstHash() + " refs/tags/v10\n" + secondHash() + " refs/tags/v10^{}\n";
        output += firstHash() + " refs/tags/v9\n" + firstHash() + " refs/heads/main\n";
        QStringList expected{QStringLiteral("refs/heads/main"), QStringLiteral("refs/tags/v9")};
        expected.append(QStringLiteral("refs/tags/v10"));
        QCOMPARE(Git::parseRefList(output), expected);
    }

    void outOfDateView()
    {
        const QList<GitRevLoglist> shown{entryOf(firstHash(), 200), entryOf(secondHash(), 100)};
        QVERIFY(!RefLogList::isOutOfDate(shown, shown));
        QVERIFY(!RefLogList::isOutOfDate({}, {}));
        // A count
        QVERIFY(RefLogList::isOutOfDate(shown, {shown.constFirst()}));
        QVERIFY(RefLogList::isOutOfDate({}, shown));
        // The first entry's date and hash, the last one's hash
        QVERIFY(RefLogList::isOutOfDate(shown, {entryOf(firstHash(), 999), entryOf(secondHash(), 100)}));
        QVERIFY(RefLogList::isOutOfDate(shown, {entryOf(secondHash(), 200), entryOf(secondHash(), 100)}));
        QVERIFY(RefLogList::isOutOfDate(shown, {entryOf(firstHash(), 200), entryOf(firstHash(), 100)}));
        // The last entry's date isn't compared, as in TortoiseGit
        QVERIFY(!RefLogList::isOutOfDate(shown, {entryOf(firstHash(), 200), entryOf(secondHash(), 999)}));
    }

    void deletesFromTheBottomUp()
    {
        const QStringList selected{QStringLiteral("refs/stash@{0}"), QStringLiteral("refs/stash@{2}")};
        const QList<QStringList> expected{
            {QStringLiteral("stash"), QStringLiteral("drop"), QStringLiteral("--"), QStringLiteral("stash@{2}")},
            {QStringLiteral("stash"), QStringLiteral("drop"), QStringLiteral("--"), QStringLiteral("stash@{0}")},
        };
        QCOMPARE(RefLogList::deleteCommands(selected), expected);
    }

    void normalisesDeletedRefs()
    {
        const auto single = [](const QString& ref) {
            return RefLogList::deleteCommands({ref}).constFirst();
        };
        const QString reflog = QStringLiteral("reflog");
        const QString del = QStringLiteral("delete");
        const QString dashes = QStringLiteral("--");
        const QString heads = QStringLiteral("heads/main@{1}");
        QCOMPARE(single(QStringLiteral("refs/heads/main@{1}")), (QStringList{reflog, del, dashes, heads}));
        QCOMPARE(single(QStringLiteral("HEAD@{3}")), (QStringList{reflog, del, dashes, QStringLiteral("HEAD@{3}")}));
        // The "@" that TortoiseGit puts back if it is missing
        const QStringList drop{QStringLiteral("stash"), QStringLiteral("drop"), dashes, QStringLiteral("stash@{1}")};
        QCOMPARE(single(QStringLiteral("stash{1}")), drop);
    }

    void reallyDeletesTheOnesSelected()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        for (const char *const message : {"oldest", "middle", "newest"}) {
            repo.write(QStringLiteral("a.txt"), message);
            const QString text = QString::fromLatin1(message);
            repo.run({QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("-m"), text});
        }

        const QStringList selected{QStringLiteral("refs/stash@{0}"), QStringLiteral("refs/stash@{2}")};
        for (const QStringList& command : RefLogList::deleteCommands(selected)) {
            repo.run(command);
        }

        const QString stash = QStringLiteral("refs/stash");
        const QByteArray left = repo.git().run(GitRevLoglist::refLogArguments(stash), RunMode::ReadOnly);
        const QList<GitRevLoglist> entries = GitRevLoglist::parseRefLog(left, stash);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.constFirst().subject, QStringLiteral("middle"));
    }

    void normalisesStashApply()
    {
        const QStringList apply{QStringLiteral("stash"), QStringLiteral("apply")};
        QCOMPARE(AppUtils::stashApplyArguments({}), apply);
        const QString expected = QStringLiteral("stash@{2}");
        QCOMPARE(AppUtils::stashApplyArguments(QStringLiteral("refs/stash@{2}")), apply + QStringList{expected});
        QCOMPARE(AppUtils::stashApplyArguments(QStringLiteral("stash{2}")), apply + QStringList{expected});
        QCOMPARE(AppUtils::stashApplyArguments(expected), apply + QStringList{expected});
    }

    void findWrapsRound()
    {
        GitRevLoglist commit = entryOf(firstHash(), 200);
        commit.refAction = QStringLiteral("commit");
        GitRevLoglist upper = entryOf(secondHash(), 100);
        upper.subject = QStringLiteral("Fix");
        GitRevLoglist lower = entryOf(secondHash(), 50);
        lower.subject = QStringLiteral("fix");
        const QList<GitRevLoglist> entries{commit, upper, lower};

        const auto find = [&entries](const QString& text, Qt::CaseSensitivity sensitivity, qsizetype start) {
            return RefLogDlg::findEntry(entries, RefLogFind{.text = text, .sensitivity = sensitivity, .start = start});
        };
        const QString fix = QStringLiteral("fix");
        QCOMPARE(find(fix, Qt::CaseInsensitive, 0), 1);
        QCOMPARE(find(fix, Qt::CaseSensitive, 0), 2);
        QCOMPARE(find(QStringLiteral("commit"), Qt::CaseSensitive, 1), 0);
        QCOMPARE(find(fix, Qt::CaseInsensitive, 99), 1);
        QCOMPARE(find(QStringLiteral("absent"), Qt::CaseInsensitive, 0), -1);
        QCOMPARE(RefLogDlg::findEntry({}, RefLogFind{.text = fix}), -1);
    }

    void copiesTortoiseGitsFormats()
    {
        GitRevLoglist commit = entryOf(firstHash(), 200);
        commit.refAction = QStringLiteral("commit");
        commit.subject = QStringLiteral("first");
        const GitRevLoglist plain = entryOf(secondHash(), 100);
        const QList<GitRevLoglist> entries{commit, plain};

        const QString hashes = QString::fromLatin1(firstHash() + '\n' + secondHash());
        QCOMPARE(RefLogList::copyText(RefLogList::CopyFormat::Hash, entries), hashes);
        const QString messages = RefLogList::copyText(RefLogList::CopyFormat::Messages, entries);
        QCOMPARE(messages, QStringLiteral("* commit: first\n\n\n* \n\n\n"));
        const QString full = RefLogList::copyText(RefLogList::CopyFormat::Full, {commit});
        QVERIFY(full.startsWith(QStringLiteral("Revision: ")));
        QVERIFY(full.endsWith(QStringLiteral("Message: commit: first\n\n")));
    }
};

QTEST_MAIN(RefLogTest)
#include "reflogtest.moc"
