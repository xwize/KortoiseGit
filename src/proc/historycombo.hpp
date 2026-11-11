// SPDX-FileCopyrightText: 2003-2012, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2013-2017, 2019-2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Utils/MiscUI/HistoryCombo.h (CHistoryCombo): the histories of the editable combo boxes.
 * TortoiseGit keeps each under Software\TortoiseGit\History in the registry; here they are groups of the "History"
 * group in KConfig, under the same names.
 */

#pragma once

#include <KConfigGroup>

#include <QString>

class QComboBox;

namespace kgt
{

/**
 * Loads and saves the history of a QComboBox, as CHistoryCombo does for itself.
 */
class HistoryCombo
{
public:
    HistoryCombo() = delete;

    /**
     * @returns The group holding every history, TortoiseGit's Software\TortoiseGit\History.
     */
    [[nodiscard]] static KConfigGroup allHistories();

    /**
     * @param name A history, such as TortoiseGit's History\PullURLS.
     * @returns Where it is kept.
     */
    [[nodiscard]] static KConfigGroup historyGroup(const QString& name);

    /**
     * Ported from CHistoryCombo::LoadHistory.
     * @param combo An editable combo box.
     * @param group The history's group, from historyGroup().
     * @param key The key prefix TortoiseGit uses, such as "url"; here the key of one list.
     */
    static void loadHistory(QComboBox *combo, const KConfigGroup& group, const QString& key);

    /**
     * Ported from CHistoryCombo::SaveHistory: the text shown goes first, without duplicates.
     * @param combo An editable combo box.
     * @param group The history's group, from historyGroup().
     * @param key The key prefix TortoiseGit uses, such as "url"; here the key of one list.
     */
    static void saveHistory(const QComboBox *combo, KConfigGroup group, const QString& key);

    /**
     * Ported from CHistoryCombo::AddString: appends an entry unless it is there already, and selects it.
     * @param combo A combo box.
     * @param text The entry; nothing happens if it is empty.
     */
    static void addString(QComboBox *combo, const QString& text);
};

} // namespace kgt
