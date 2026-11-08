// SPDX-FileCopyrightText: 2003-2012, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2013-2017, 2019-2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Utils/MiscUI/HistoryCombo.h (CHistoryCombo). So far only the entry handling the dialogs
 * share; a QComboBox is the widget itself.
 */

#pragma once

#include <QString>

class QComboBox;

namespace kgt
{

/**
 * What CHistoryCombo adds to a combo box.
 */
class HistoryCombo
{
public:
    HistoryCombo() = delete;

    /**
     * Ported from CHistoryCombo::AddString: adds an entry once and selects it. An existing entry after @p pos moves
     * there; one at or before it stays where it is.
     * @param combo The combo box.
     * @param text The entry; nothing happens if it is empty.
     * @param pos Where to put it; negative for the end, TortoiseGit's default.
     */
    static void addString(QComboBox *combo, const QString& text, int pos = -1);
};

} // namespace kgt
