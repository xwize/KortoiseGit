// SPDX-FileCopyrightText: 2007-2010, 2015, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2016, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/Settings/SettingsPropPage.cpp

#include "settings/settingsproppage.hpp"

namespace kgt
{

SettingsPropPage::SettingsPropPage(QWidget *parent)
    : QWidget(parent)
{
}

bool SettingsPropPage::apply()
{
    return true;
}

bool SettingsPropPage::isModified() const
{
    return m_modified;
}

void SettingsPropPage::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    Q_EMIT modifiedChanged(modified);
}

} // namespace kgt
