// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/FilterHelper.cpp

#include "filterhelper.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace
{

/**
 * @param regexp A regular expression.
 * @param caseSensitive Whether it respects case.
 * @returns It compiled. '^' and '$' match at line ends, as the filter's tooltip promises and the text being searched
 *          has one field per line (CFilterHelper::ValidateRegexp's std::wregex does so on MSVC).
 */
QRegularExpression compiledRegexp(const QString& regexp, bool caseSensitive)
{
    QRegularExpression::PatternOptions options = QRegularExpression::MultilineOption;
    if (!caseSensitive) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    return QRegularExpression(regexp, options);
}

/**
 * Reads a quoted word starting at the opening quote, the "escaped string?" branch of CFilterHelper's constructor.
 * @param text The filter text.
 * @param[in,out] pos On entry the opening quote; on return just past the word.
 * @returns The word, with "" turned into ".
 */
QString quotedWord(const QString& text, qsizetype& pos)
{
    QString word;
    const qsizetype length = text.size();
    while (++pos < length) {
        if (text.at(pos) != QLatin1Char('"')) {
            word.append(text.at(pos));
            continue;
        }
        // A doubled quote stands for one
        ++pos;
        if (pos < length && text.at(pos) == QLatin1Char('"')) {
            word.append(QLatin1Char('"'));
            continue;
        }
        if (pos >= length || text.at(pos) == QLatin1Char(' ')) {
            break;
        }
        word.append(QLatin1Char('"'));
        word.append(text.at(pos));
    }
    ++pos;
    return word;
}

/**
 * Reads the next space-separated word, as CString::Tokenize(L" ", pos) does.
 * @param text The filter text.
 * @param[in,out] pos Where to start; on return just past the word's end, or past the text.
 * @returns The word, or an empty string at the end.
 */
QString plainWord(const QString& text, qsizetype& pos)
{
    while (pos < text.size() && text.at(pos) == QLatin1Char(' ')) {
        ++pos;
    }
    const qsizetype start = pos;
    while (pos < text.size() && text.at(pos) != QLatin1Char(' ')) {
        ++pos;
    }
    const QString word = text.mid(start, pos - start);
    ++pos;
    return word;
}

} // namespace

namespace kgt
{

FilterHelper::FilterHelper(const QString& filter, bool filterWithRegex, unsigned int selectedFilters, bool caseSensitive)
    : m_selectedFilters(selectedFilters)
    , m_caseSensitive(caseSensitive)
{
    QString filterText = filter;
    // A leading '!' negates the filter
    if (filterText.startsWith(QLatin1Char('!'))) {
        m_negate = true;
        filterText.remove(0, 1);
    }
    if (filterWithRegex && !filterText.isEmpty()) {
        if (QRegularExpression pattern = compiledRegexp(filterText, m_caseSensitive); pattern.isValid()) {
            m_patterns.append(std::move(pattern));
        }
        return;
    }
    parse(filterText);
}

bool FilterHelper::isValidRegexp(const QString& regexp)
{
    return compiledRegexp(regexp, true).isValid();
}

bool FilterHelper::isFilterActive() const
{
    return !m_conditions.isEmpty() || !m_patterns.isEmpty();
}

unsigned int FilterHelper::selectedFilters() const
{
    return m_selectedFilters;
}

void FilterHelper::parse(const QString& filterText)
{
    const qsizetype length = filterText.size();
    qsizetype pos = 0;
    while (pos < length) {
        while (pos < length && filterText.at(pos) == QLatin1Char(' ')) {
            ++pos;
        }
        Prefix prefix = Prefix::And;
        if (pos < length && filterText.at(pos) == QLatin1Char('-')) {
            prefix = Prefix::AndNot;
            ++pos;
        } else if (pos < length && filterText.at(pos) == QLatin1Char('+')) {
            prefix = Prefix::Or;
            ++pos;
        }
        if (pos < length && filterText.at(pos) == QLatin1Char('"')) {
            addSubString(quotedWord(filterText, pos), prefix);
        }
        // As in TortoiseGit, the text after a quoted word is read as a word with the same prefix
        addSubString(plainWord(filterText, pos), prefix);
    }
}

void FilterHelper::addSubString(QString token, Prefix prefix)
{
    if (token.isEmpty()) {
        return;
    }
    if (!m_caseSensitive) {
        token = token.toLower();
    }
    m_conditions.append({.subString = token, .prefix = prefix, .nextOrIndex = 0});
    // Update the nextOrIndex of the conditions before this Or
    const qsizetype newPos = m_conditions.size() - 1;
    if (prefix == Prefix::Or) {
        for (auto it = std::next(m_conditions.rbegin()); it != m_conditions.rend() && it->nextOrIndex == 0; ++it) {
            it->nextOrIndex = newPos;
        }
    }
}

bool FilterHelper::match(const QString& text) const
{
    // Empty text does not match
    if (text.isEmpty()) {
        return false;
    }
    const QString haystack = m_caseSensitive ? text : text.toLower();
    if (!m_patterns.isEmpty()) {
        return std::ranges::all_of(m_patterns, [&haystack](const QRegularExpression& pattern) {
            return pattern.match(haystack).hasMatch();
        });
    }
    bool currentValue = true;
    for (qsizetype i = 0; i < m_conditions.size(); ++i) {
        const Condition& condition = m_conditions.at(i);
        bool found = haystack.contains(condition.subString);
        if (condition.prefix == Prefix::Or) {
            currentValue = currentValue || found;
        } else {
            found = condition.prefix == Prefix::AndNot ? !found : found;
            currentValue = currentValue && found;
        }
        if (!currentValue) {
            // Not a match: skip to the next "+" alternative
            if (condition.nextOrIndex == 0) {
                return false;
            }
            i = condition.nextOrIndex - 1;
        }
    }
    return true;
}

bool FilterHelper::calculateFinalResult(bool result) const
{
    return result != m_negate;
}

} // namespace kgt
