// SPDX-FileCopyrightText: 2003-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2019 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Utils/RegHistory.h (CRegHistory): a list of recently used strings, newest first, such as
 * the commit dialog's log messages. TortoiseGit keeps it as numbered registry values (logmsgs0, logmsgs1, ...); here it
 * is one list entry of a KConfig group, as HistoryCombo keeps its histories.
 */

#pragma once

#include <KConfigGroup>

#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * A history of strings stored in KConfig, newest first and without duplicates.
 */
class RegHistory
{
public:
    /**
     * @param maxHistoryItems How many entries are kept at most.
     */
    explicit RegHistory(qsizetype maxHistoryItems = 25);

    /**
     * Ported from CRegHistory::SetMaxHistoryItems.
     * @param maxItems How many entries are kept at most.
     */
    void setMaxHistoryItems(qsizetype maxItems);

    /**
     * Ported from CRegHistory::Load: reads the history, and remembers where it is kept for addEntry() and save().
     * @param section The group it is kept in, such as History/commit.
     * @param keyPrefix The key TortoiseGit numbers its values with, such as "logmsgs"; here the key of the list.
     * @returns How many entries were loaded.
     */
    qsizetype load(const KConfigGroup& section, const QString& keyPrefix);

    /**
     * Ported from CRegHistory::Save: writes the history back where load() read it.
     * @returns Whether it was saved; false if nothing was loaded first.
     */
    bool save();

    /**
     * Ported from CRegHistory::AddEntry: puts the text first, reloading the history first so that other processes'
     * entries aren't lost. Text that is already there moves to the front.
     * @param text The entry; nothing happens if it is empty.
     * @returns Whether it is a new entry.
     */
    bool addEntry(const QString& text);

    /**
     * Ported from CRegHistory::RemoveEntry.
     * @param pos The entry's index.
     */
    void removeEntry(qsizetype pos);

    /**
     * @returns How many entries there are.
     */
    [[nodiscard]] qsizetype count() const;

    /**
     * @param pos An index below count().
     * @returns The entry.
     */
    [[nodiscard]] const QString& entry(qsizetype pos) const;

    /**
     * @returns Whether there are no entries.
     */
    [[nodiscard]] bool isEmpty() const;

private:
    KConfigGroup m_section;
    QString m_keyPrefix;
    QStringList m_entries;
    qsizetype m_maxHistoryItems;
};

} // namespace kgt
