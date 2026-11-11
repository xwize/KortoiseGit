// SPDX-FileCopyrightText: 2003-2007 TortoiseSVN
// SPDX-FileCopyrightText: 2009, 2011, 2013-2016, 2019-2020 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/Settings/SettingsProgsMerge.cpp and its IDD_SETTINGSPROGSMERGE resource

#include "settings/settingsprogsmerge.hpp"

#include "apputils.hpp"

#include <KLocalizedString>

namespace kgt
{

SettingsProgsMerge::SettingsProgsMerge(QWidget *parent)
    : SettingsProgsTool(
          {
              .configKey = QStringLiteral("merge.tool"),
              .description = i18n("Configure the program used to resolve conflicted files."),
              .defaultHint = i18n("By default, Edit conflicts uses KDiff3, otherwise Meld."),
              .tools = AppUtils::mergeTools(),
          },
          parent)
{
}

} // namespace kgt
