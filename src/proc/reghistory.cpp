// SPDX-FileCopyrightText: 2003-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2019 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Utils/RegHistory.cpp

#include "reghistory.hpp"

namespace kgt
{

RegHistory::RegHistory(qsizetype maxHistoryItems)
    : m_maxHistoryItems(maxHistoryItems)
{
}

void RegHistory::setMaxHistoryItems(qsizetype maxItems)
{
    m_maxHistoryItems = maxItems;
}

qsizetype RegHistory::load(const KConfigGroup& section, const QString& keyPrefix)
{
    m_section = section;
    m_keyPrefix = keyPrefix;
    m_entries = section.readEntry(keyPrefix, QStringList()).mid(0, m_maxHistoryItems);
    m_entries.removeAll(QString());
    return m_entries.size();
}

bool RegHistory::save()
{
    if (!m_section.isValid() || m_keyPrefix.isEmpty()) {
        return false;
    }
    // CRegHistory::Save removes the values past the maximum; Load never reads them anyway
    m_section.writeEntry(m_keyPrefix, m_entries.mid(0, m_maxHistoryItems));
    m_section.sync();
    return true;
}

bool RegHistory::addEntry(const QString& text)
{
    if (text.isEmpty()) {
        return false;
    }
    if (m_section.isValid() && !m_keyPrefix.isEmpty()) {
        // Refresh the history from the configuration
        load(m_section, m_keyPrefix);
    }
    const qsizetype pos = m_entries.indexOf(text);
    if (pos >= 0) {
        m_entries.move(pos, 0);
        return false;
    }
    m_entries.prepend(text);
    return true;
}

void RegHistory::removeEntry(qsizetype pos)
{
    m_entries.removeAt(pos);
}

qsizetype RegHistory::count() const
{
    return m_entries.size();
}

const QString& RegHistory::entry(qsizetype pos) const
{
    return m_entries.at(pos);
}

bool RegHistory::isEmpty() const
{
    return m_entries.isEmpty();
}

} // namespace kgt
