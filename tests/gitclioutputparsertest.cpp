// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitclioutputparser.hpp"

#include <QTest>

namespace
{

using namespace kgt;

/**
 * Applies the parser's changes to a log window's text, as ProgressDlg::updateCmdOutput does.
 * @param[in,out] log The log's text.
 * @param out The changes.
 */
void apply(QByteArray& log, const EmittedLines& out)
{
    if (out.erasePreviousLineWithLength > 0) {
        log.truncate(log.lastIndexOf('\n') + 1);
    }
    log.append(out.text);
}

/**
 * Feeds chunks one at a time, processing after each, as the dialog's refresh timer does.
 * @param chunks The output as it arrives.
 * @returns The log's text afterwards.
 */
QByteArray feed(const QList<QByteArray>& chunks)
{
    GitCliOutputParser parser;
    QByteArray log;
    for (const QByteArray& chunk : chunks) {
        parser.appendChunk(chunk);
        apply(log, parser.processPending());
    }
    return log;
}

} // namespace

/**
 * GitCliOutputParser: git's console output, with progress lines redrawn in place.
 */
class GitCliOutputParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainLinesPassThrough()
    {
        QCOMPARE(feed({"To /tmp/remote.git\n * [new branch]      main -> main\n"}), QByteArray("To /tmp/remote.git\n * [new branch]      main -> main\n"));
    }

    void progressLineIsReplacedInPlace()
    {
        GitCliOutputParser parser;
        QByteArray log;
        parser.appendChunk("Writing objects:  33% (1/3)\r");
        apply(log, parser.processPending());
        // The newest progress line is shown at once
        QCOMPARE(log, QByteArray("Writing objects:  33% (1/3)"));

        parser.appendChunk("Writing objects:  66% (2/3)\r");
        apply(log, parser.processPending());
        QCOMPARE(log, QByteArray("Writing objects:  66% (2/3)"));

        parser.appendChunk("Writing objects: 100% (3/3), done.\nTotal 3\n");
        apply(log, parser.processPending());
        QCOMPARE(log, QByteArray("Writing objects: 100% (3/3), done.\nTotal 3\n"));
    }

    void progressSplitAcrossChunks()
    {
        QCOMPARE(feed({"Counting obj", "ects:  50% (1/2)\rCounting objects: 10", "0% (2/2)\rCounting objects: 100% (2/2), done.\n"}),
                 QByteArray("Counting objects: 100% (2/2), done.\n"));
    }

    void shorterLineOverlaysLongerOne()
    {
        // As on a console, a shorter line leaves the rest of the longer one visible
        QCOMPARE(feed({"abcdef\rxy\n"}), QByteArray("xycdef\n"));
    }

    void remoteProgressLines()
    {
        QCOMPARE(feed({"remote: Resolving deltas:  50% (1/2)\rremote: Resolving deltas: 100% (2/2)\rremote: Resolving deltas: 100% (2/2), done.\n"}),
                 QByteArray("remote: Resolving deltas: 100% (2/2), done.\n"));
    }

    void nulBecomesNewline()
    {
        QCOMPARE(feed({QByteArray("a\0b\n", 4)}), QByteArray("a\nb\n"));
    }

    void longLinesAreTruncated()
    {
        const QByteArray log = feed({QByteArray(QByteArray(qsizetype{10} * 1024, 'x') + '\n')});
        const QByteArray expected = QByteArray(qsizetype{8} * 1024, 'x') + QByteArray("... [line truncated at 8 KiB]");
        QVERIFY(log.startsWith(expected));
        QVERIFY(log.endsWith('\n'));
    }

    void limitStopsOutput()
    {
        GitCliOutputParser parser(10);
        parser.appendChunk("0123456789\n0123456789\n0123456789\n");
        const EmittedLines out = parser.processPending();
        QVERIFY(out.limited);
    }
};

QTEST_GUILESS_MAIN(GitCliOutputParserTest)

#include "gitclioutputparsertest.moc"
