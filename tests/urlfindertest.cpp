// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "utils/urlfinder.hpp"
#include "logdlg.hpp"

#include <QTest>

namespace
{

using namespace kgt;

/**
 * @returns The text of each range of msg.
 */
QStringList texts(const QString& msg, const QList<TextRange>& ranges)
{
    QStringList result;
    for (const TextRange& range : ranges) {
        result.append(msg.mid(range.start, range.end - range.start));
    }
    return result;
}

} // namespace

class UrlFinderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void isUrlOrEmail_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("expected");
        QTest::newRow("http") << QStringLiteral("http://example.com") << true;
        QTest::newRow("https") << QStringLiteral("https://example.com/a?b=c#d") << true;
        QTest::newRow("git") << QStringLiteral("git://example.com/repo.git") << true;
        QTest::newRow("ftp") << QStringLiteral("ftp://example.com") << true;
        QTest::newRow("file") << QStringLiteral("file:///tmp/x") << true;
        QTest::newRow("mailto") << QStringLiteral("mailto:a@b.org") << true;
        QTest::newRow("prefix only") << QStringLiteral("https://") << false;
        QTest::newRow("other scheme") << QStringLiteral("ssh://host/path") << false;
        QTest::newRow("email") << QStringLiteral("someone@example.com") << true;
        QTest::newRow("git ssh path") << QStringLiteral("git@example.com:repo.git") << false;
        QTest::newRow("dot right after @") << QStringLiteral("a@.com") << false;
        QTest::newRow("no dot after @") << QStringLiteral("a.b@localhost") << false;
        QTest::newRow("@ first") << QStringLiteral("@example.com") << false;
        QTest::newRow("word") << QStringLiteral("hello") << false;
    }

    void isUrlOrEmail()
    {
        QFETCH(const QString, text);
        QFETCH(const bool, expected);
        QCOMPARE(kgt::isUrlOrEmail(text), expected);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void findUrlMatches_data()
    {
        QTest::addColumn<QString>("msg");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("plain") << QStringLiteral("see https://example.com/x for more") << QStringList{QStringLiteral("https://example.com/x")};
        QTest::newRow("trailing dot") << QStringLiteral("Read https://example.com/x.") << QStringList{QStringLiteral("https://example.com/x")};
        QTest::newRow("trailing punctuation") << QStringLiteral("Really http://example.com/?!;:") << QStringList{QStringLiteral("http://example.com/")};
        QTest::newRow("in parentheses") << QStringLiteral("(http://example.com/a)") << QStringList{QStringLiteral("http://example.com/a")};
        QTest::newRow("angle brackets") << QStringLiteral("Link: <https://example.com/a b>.") << QStringList{QStringLiteral("https://example.com/a b")};
        QTest::newRow("unclosed angle bracket") << QStringLiteral("<https://example.com/a\nnext") << QStringList{QStringLiteral("https://example.com/a")};
        QTest::newRow("email in brackets") << QStringLiteral("A U Thor <author@example.com>\n") << QStringList{QStringLiteral("author@example.com")};
        QTest::newRow("email at end of sentence") << QStringLiteral("Mail author@example.com.") << QStringList{QStringLiteral("author@example.com")};
        QTest::newRow("git ssh path") << QStringLiteral("Cloned git@example.com:repo.git today") << QStringList{};
        QTest::newRow("unknown scheme") << QStringLiteral("ssh://host/path") << QStringList{};
        QTest::newRow("several") << QStringLiteral("http://a.org and ftp://b.org\nc@d.org")
                                 << QStringList{QStringLiteral("http://a.org"), QStringLiteral("ftp://b.org"), QStringLiteral("c@d.org")};
        QTest::newRow("none") << QStringLiteral("Nothing to see here.") << QStringList{};
    }

    void findUrlMatches()
    {
        QFETCH(const QString, msg);
        QFETCH(const QStringList, expected);
        QCOMPARE(texts(msg, kgt::findUrlMatches(msg)), expected);
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void findGitHashPositions_data()
    {
        QTest::addColumn<QString>("msg");
        QTest::addColumn<QStringList>("expected");
        const QString full = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        QTest::newRow("full hash") << QStringLiteral("x\nReverts ") + full + QStringLiteral(".") << QStringList{full};
        QTest::newRow("short hash") << QStringLiteral("x\nFixes 1a2b3c4d (oops)") << QStringList{QStringLiteral("1a2b3c4d")};
        QTest::newRow("too short") << QStringLiteral("x\nsee 1a2b3c4 and deadbee") << QStringList{};
        QTest::newRow("hex words") << QStringLiteral("x\nadded a faded decade") << QStringList{};
        QTest::newRow("too long") << QStringLiteral("x\n") + full + QStringLiteral("8") << QStringList{};
        QTest::newRow("followed by letter") << QStringLiteral("x\n1a2b3c4dz") << QStringList{};
        QTest::newRow("followed by @") << QStringLiteral("x\n1a2b3c4d@example.com") << QStringList{};
        QTest::newRow("uppercase") << QStringLiteral("x\n1A2B3C4D") << QStringList{};
        QTest::newRow("describe") << QStringLiteral("x\nv1.0-3-g1a2b3c4d") << QStringList{QStringLiteral("1a2b3c4d")};
        QTest::newRow("signed-off-by") << QStringLiteral("x\nSigned-off-by: 1a2b3c4d\n2b3c4d5e") << QStringList{QStringLiteral("2b3c4d5e")};
        QTest::newRow("change-id") << QStringLiteral("x\nChange-Id: I1a2b3c4d5e6f") << QStringList{};
        QTest::newRow("header line skipped") << QStringLiteral("SHA-1: ") + full + QStringLiteral("\nsubject") << QStringList{};
    }

    void findGitHashPositions()
    {
        QFETCH(const QString, msg);
        QFETCH(const QStringList, expected);
        QCOMPARE(texts(msg, kgt::findGitHashPositions(msg, msg.indexOf(QLatin1Char('\n')))), expected);
    }
};

QTEST_GUILESS_MAIN(UrlFinderTest)

#include "urlfindertest.moc"
