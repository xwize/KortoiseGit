// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/FilterHelper.h (CFilterHelper): parses a filter box's text and matches
 * it against strings. Only the sub-string form is ported so far; the regex form comes with the log dialog's filter.
 */

#pragma once

#include <QList>
#include <QString>

namespace kgt
{

/**
 * Which parts of an entry a filter looks at, TortoiseGit's LOGFILTER_* bits.
 */
enum LogFilter : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
    LogFilterAuthors = 0x0004, // LOGFILTER_AUTHORS
    LogFilterRevs = 0x0008, // LOGFILTER_REVS
    LogFilterSubject = 0x0040, // LOGFILTER_SUBJECT
    LogFilterRefName = 0x0080, // LOGFILTER_REFNAME
    LogFilterToggle = 0x8000, // LOGFILTER_TOGGLE: a menu command, not a filter
    LogFilterAll = 0xFFFF, // LOGFILTER_ALL
};

/**
 * A parsed filter. Words separated by spaces must all be found; "-word" must not be found; "+word" starts an
 * alternative; "quoted text" is one word, with "" for a quote. A leading '!' negates the whole filter. Matching
 * ignores case unless asked not to.
 */
class FilterHelper
{
public:
    FilterHelper() = default;

    /**
     * @param filter The filter box's text.
     * @param selectedFilters The LogFilter bits saying which parts of an entry to look at.
     * @param caseSensitive Whether matching respects case.
     */
    FilterHelper(const QString& filter, unsigned int selectedFilters, bool caseSensitive);

    /**
     * @returns Whether there is something to filter for.
     */
    [[nodiscard]] bool isFilterActive() const;

    /**
     * @returns The LogFilter bits.
     */
    [[nodiscard]] unsigned int selectedFilters() const;

protected:
    /**
     * Ported from CFilterHelper::Match.
     * @param text The entry's text, its parts separated by newlines.
     * @returns Whether it matches, before negation.
     */
    [[nodiscard]] bool match(const QString& text) const;

    /**
     * @param result A match result.
     * @returns It, negated if the filter starts with '!'.
     */
    [[nodiscard]] bool calculateFinalResult(bool result) const;

private:
    /**
     * How a word's match combines with the result so far.
     */
    enum class Prefix {
        And, // No prefix: must be found
        Or, // '+': starts an alternative
        AndNot, // '-': must not be found
    };

    /**
     * One word of the filter, TortoiseGit's SCondition.
     */
    struct Condition {
        QString subString; // The word, in lower case unless matching respects case
        Prefix prefix = Prefix::And; // How it combines
        qsizetype nextOrIndex = 0; // Index of the next Or condition, 0 if none follows
    };

    /**
     * Ported from CFilterHelper::AddSubString.
     * @param token The word.
     * @param prefix How it combines.
     */
    void addSubString(QString token, Prefix prefix);

    /**
     * Splits the filter text into conditions, the constructor's loop in TortoiseGit.
     * @param filterText The text without a leading '!'.
     */
    void parse(const QString& filterText);

    QList<Condition> m_conditions;
    unsigned int m_selectedFilters = LogFilterAll;
    bool m_caseSensitive = false;
    bool m_negate = false;
};

} // namespace kgt
