// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlgfilter.hpp"
#include "git/gitrevloglist.hpp"

#include <QHash>
#include <QTest>

namespace
{

using namespace kgt;

/**
 * @returns A commit with something distinctive in every field the filter reads.
 */
GitRevLoglist sampleRev()
{
    GitRevLoglist rev;
    rev.hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
    rev.subject = QStringLiteral("Fix the Parser");
    rev.body = QStringLiteral("Longer explanation");
    rev.authorName = QStringLiteral("Ann Author");
    rev.authorEmail = QStringLiteral("ann@example.com");
    rev.committerName = QStringLiteral("Carl Committer");
    rev.committerEmail = QStringLiteral("carl@example.org");
    rev.notes = QStringLiteral("reviewed note");
    rev.refs = {{.type = GitRef::Type::LocalBranch, .name = QStringLiteral("feature/x"), .isCurrent = false},
                {.type = GitRef::Type::Tag, .name = QStringLiteral("v1.0"), .isCurrent = false},
                {.type = GitRef::Type::Head, .name = QStringLiteral("HEAD"), .isCurrent = true}};
    return rev;
}

/**
 * @returns The sample commit's changed paths.
 */
QStringList samplePaths()
{
    return {QStringLiteral("src/parser.cpp"), QStringLiteral("docs/readme.md")};
}

/**
 * @returns The text of the sample commit's annotated tag.
 */
QHash<QString, QString> sampleTagInfo()
{
    return {{QStringLiteral("refs/tags/v1.0"), QStringLiteral("tag v1.0\n\nRelease notes\n")}};
}

/**
 * @param text The filter text.
 * @param selected The fields to look at.
 * @param regex Whether the text is a regular expression.
 * @param caseSensitive Whether matching respects case.
 * @returns Whether the sample commit matches.
 */
bool matches(const QString& text, unsigned int selected, bool regex = false, bool caseSensitive = false)
{
    const LogDlgFilter filter(text, regex, selected, caseSensitive);
    return filter(sampleRev(), samplePaths(), sampleTagInfo());
}

} // namespace

/**
 * CLogDlgFilter and CFilterHelper: each field, regular expressions, case and the search syntax.
 */
class LogDlgFilterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void matchesField_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<unsigned int>("field");
        QTest::addColumn<unsigned int>("otherField");

        const auto add = [](const char *name, const char *text, unsigned int field, unsigned int other) {
            QTest::newRow(name) << QString::fromUtf8(text) << field << other;
        };
        const unsigned int subject = LogFilterSubject;
        add("subject", "parser", subject, LogFilterAuthors);
        add("messages covers subject", "parser", LogFilterMessages, LogFilterAuthors);
        add("messages covers body", "explanation", LogFilterMessages, subject);
        add("author name", "ann author", LogFilterAuthors, subject);
        add("committer name", "carl", LogFilterAuthors, subject);
        add("author email", "ann@example", LogFilterEmails, subject);
        add("committer email", "example.org", LogFilterEmails, subject);
        add("hash", "0123456789ab", LogFilterRevs, subject);
        add("notes", "reviewed", LogFilterNotes, subject);
        add("full ref name", "refs/heads/feature", LogFilterRefName, subject);
        add("tag ref name", "refs/tags/v1.0", LogFilterRefName, subject);
        add("tag info", "release notes", LogFilterAnnotatedTag, subject);
        add("paths", "docs/readme", LogFilterPaths, subject);
    }

    void matchesField()
    {
        QFETCH(const QString, text);
        QFETCH(const unsigned int, field);
        QFETCH(const unsigned int, otherField);
        QVERIFY(matches(text, field));
        QVERIFY(!matches(text, otherField));
    }

    void headIsNotARefName()
    {
        QVERIFY(!matches(QStringLiteral("HEAD"), LogFilterRefName, false, true));
    }

    void ignoresCaseUnlessAsked()
    {
        QVERIFY(matches(QStringLiteral("PARSER"), LogFilterSubject));
        QVERIFY(!matches(QStringLiteral("parser"), LogFilterSubject, false, true));
        QVERIFY(matches(QStringLiteral("Parser"), LogFilterSubject, false, true));
    }

    void wordSyntax()
    {
        const unsigned int field = LogFilterSubject;
        QVERIFY(matches(QStringLiteral("fix parser"), field)); // All words must be found
        QVERIFY(!matches(QStringLiteral("fix absent"), field));
        QVERIFY(!matches(QStringLiteral("fix -parser"), field)); // "-word" must not be found
        QVERIFY(matches(QStringLiteral("fix -absent"), field));
        QVERIFY(matches(QStringLiteral("absent +parser"), field)); // "+word" starts an alternative
        QVERIFY(!matches(QStringLiteral("absent +missing"), field));
        QVERIFY(matches(QStringLiteral("\"the parser\""), field)); // Quoted text is one word
        QVERIFY(!matches(QStringLiteral("\"parser the\""), field));
        QVERIFY(!matches(QStringLiteral("!parser"), field)); // A leading '!' negates the filter
        QVERIFY(matches(QStringLiteral("!absent"), field));
    }

    void regularExpressions()
    {
        const unsigned int field = LogFilterSubject | LogFilterAuthors;
        QVERIFY(matches(QStringLiteral("fi[xy] .*parser"), field, true));
        QVERIFY(!matches(QStringLiteral("fi[ab]"), field, true));
        QVERIFY(!matches(QStringLiteral("parser"), field, true, true));
        QVERIFY(matches(QStringLiteral("Parser"), field, true, true));
        QVERIFY(matches(QStringLiteral("^Ann"), field, true)); // '^' matches at each field's line
        QVERIFY(matches(QStringLiteral("Parser$"), field, true));
        QVERIFY(!matches(QStringLiteral("!Parser"), field, true));
    }

    void invalidRegularExpressionFiltersForNothing()
    {
        QVERIFY(!FilterHelper::isValidRegexp(QStringLiteral("(unclosed")));
        QVERIFY(FilterHelper::isValidRegexp(QStringLiteral("(closed)")));
        const LogDlgFilter filter(QStringLiteral("(unclosed"), true, LogFilterAll, false);
        QVERIFY(!filter.isFilterActive());
        QVERIFY(filter(sampleRev(), samplePaths(), sampleTagInfo()));
    }

    void saysWhichDataItNeeds()
    {
        const LogDlgFilter empty(QString(), false, LogFilterAll, false);
        QVERIFY(!empty.isFilterActive());
        QVERIFY(!empty.needsPaths());
        QVERIFY(!empty.needsTagInfo());
        const LogDlgFilter subjectOnly(QStringLiteral("x"), false, LogFilterSubject, false);
        QVERIFY(!subjectOnly.needsPaths());
        QVERIFY(!subjectOnly.needsTagInfo());
        const LogDlgFilter all(QStringLiteral("x"), false, LogFilterAll, false);
        QVERIFY(all.needsPaths());
        QVERIFY(all.needsTagInfo());
    }
};

QTEST_GUILESS_MAIN(LogDlgFilterTest)

#include "logdlgfiltertest.moc"
