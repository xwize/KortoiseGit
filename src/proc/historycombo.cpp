// SPDX-FileCopyrightText: 2003-2012, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2013-2017, 2019-2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Utils/MiscUI/HistoryCombo.cpp

#include "historycombo.hpp"

#include <QComboBox>

namespace kgt
{

void HistoryCombo::addString(QComboBox *combo, const QString& text, int pos)
{
    if (text.isEmpty()) {
        return;
    }
    const int position = pos < 0 ? combo->count() : pos;
    const int existing = combo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (existing >= 0) {
        if (existing <= position) {
            combo->setCurrentIndex(existing);
            return;
        }
        combo->removeItem(existing);
    }
    combo->insertItem(position, text);
    combo->setCurrentIndex(position);
}

} // namespace kgt
