// SPDX-FileCopyrightText: 2003-2008, 2013, 2018, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Utils/URLFinder.h

#include "urlfinder.hpp"

#include <QStringView>

#include <array>

namespace kgt
{

bool isValidUrlChar(QChar chr)
{
    // IsValidURLChar
    static constexpr QStringView extra = u"_/;?&=%:.#-+|><!@~";
    return chr.isLetterOrNumber() || extra.contains(chr);
}

bool pathIsUrl(const QString& text)
{
    // ::PathIsURL: Windows takes a single letter before the colon for a drive, so a scheme needs two
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (colon < 2) {
        return false;
    }
    if (!text.at(0).isLetter() || text.at(0).unicode() > 0x7f) {
        return false;
    }
    for (qsizetype i = 1; i < colon; ++i) {
        const QChar chr = text.at(i);
        const bool asciiAlnum = chr.unicode() <= 0x7f && chr.isLetterOrNumber();
        if (!asciiAlnum && chr != QLatin1Char('+') && chr != QLatin1Char('-') && chr != QLatin1Char('.')) {
            return false;
        }
    }
    return true;
}

bool isUrlOrEmail(const QString& text)
{
    // IsUrlOrEmail
    if (!pathIsUrl(text)) {
        const qsizetype atpos = text.indexOf(QLatin1Char('@'));
        if (atpos <= 0) {
            return false;
        }
        // a dot must follow after the @, but not directly after it
        if (text.indexOf(QLatin1Char('.'), atpos) <= atpos + 1) {
            return false;
        }
        // do not detect git@example.com:something as an email address
        return text.indexOf(QLatin1Char(':'), atpos) < 0;
    }
    static constexpr std::array prefixes{u"http://", u"https://", u"git://", u"ftp://", u"file://", u"mailto:"};
    for (const char16_t *prefix : prefixes) {
        const QStringView view(prefix);
        if (text.startsWith(view) && text.size() != view.size()) {
            return true;
        }
    }
    return false;
}

namespace
{

/**
 * The part of FindURLMatches for a URL put within <>: skips the leading '<'s and runs up to the first '>' or new
 * line, so the URL may hold characters IsValidURLChar refuses.
 * @param msg The text.
 * @param starturl Where the run starts, at a '<'; moved past the '<'s.
 * @returns Where the URL ends.
 */
qsizetype findBracketedUrlEnd(const QString& msg, qsizetype& starturl)
{
    while (msg.at(starturl) == QLatin1Char('<')) {
        ++starturl;
    }
    qsizetype i = starturl;
    while (i < msg.size() && msg.at(i) != QLatin1Char('\r') && msg.at(i) != QLatin1Char('\n') && msg.at(i) != QLatin1Char('>')) {
        ++i;
    }
    return i;
}

/**
 * The trailing punctuation FindURLMatches leaves out of a URL.
 * @returns How many characters before end are such punctuation, keeping at least one character after starturl.
 */
qsizetype trailingPunctuation(const QString& msg, qsizetype starturl, qsizetype end)
{
    static constexpr QStringView trailing = u".-?;:><!";
    qsizetype skipTrailing = 0;
    while (end - skipTrailing - 1 > starturl && trailing.contains(msg.at(end - skipTrailing - 1))) {
        ++skipTrailing;
    }
    return skipTrailing;
}

} // namespace

QList<TextRange> findUrlMatches(const QString& msg)
{
    // FindURLMatches
    QList<TextRange> result;
    const qsizetype len = msg.size();
    qsizetype starturl = -1;

    for (qsizetype i = 0; i <= len; ++i) {
        if (i < len && isValidUrlChar(msg.at(i))) {
            if (starturl < 0) {
                starturl = i;
            }
            continue;
        }

        if (starturl < 0) {
            continue;
        }

        qsizetype skipTrailing = 0;
        // try to detect and do not strip URLs put within <>
        if (msg.at(starturl) == QLatin1Char('<') && i < len) {
            i = findBracketedUrlEnd(msg, starturl);
        } else {
            skipTrailing = trailingPunctuation(msg, starturl, i);
        }

        if (isUrlOrEmail(msg.mid(starturl, i - starturl - skipTrailing))) {
            result.append({.start = starturl, .end = i - skipTrailing});
        }
        starturl = -1;
    }
    return result;
}

} // namespace kgt
