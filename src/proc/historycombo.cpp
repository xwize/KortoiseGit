// SPDX-FileCopyrightText: 2003-2012, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2013-2017, 2019-2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Utils/MiscUI/HistoryCombo.cpp

#include "historycombo.hpp"

#include <KSharedConfig>

#include <QComboBox>
#include <QStringList>

namespace
{

// CHistoryCombo's MAX_HISTORY_ITEMS
constexpr qsizetype maxHistoryItems = 25;

} // namespace

namespace kgt
{

KConfigGroup HistoryCombo::allHistories()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("History"));
}

KConfigGroup HistoryCombo::historyGroup(const QString& name)
{
    return allHistories().group(name);
}

void HistoryCombo::loadHistory(QComboBox *combo, const KConfigGroup& group, const QString& key)
{
    combo->addItems(group.readEntry(key, QStringList()));
}

void HistoryCombo::saveHistory(const QComboBox *combo, KConfigGroup group, const QString& key)
{
    const QString current = combo->currentText().trimmed();
    if (current.isEmpty()) {
        return;
    }
    QStringList entries = group.readEntry(key, QStringList());
    entries.removeAll(current);
    entries.prepend(current);
    group.writeEntry(key, entries.mid(0, maxHistoryItems));
    group.sync();
}

void HistoryCombo::addString(QComboBox *combo, const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    int index = combo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (index < 0) {
        combo->addItem(text);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

} // namespace kgt
