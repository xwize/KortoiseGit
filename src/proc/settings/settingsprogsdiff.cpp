// SPDX-FileCopyrightText: 2003-2007, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2011, 2013-2016 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/Settings/SettingsProgsDiff.cpp and its IDD_SETTINGSPROGSDIFF resource

#include "settings/settingsprogsdiff.hpp"

#include "gitdiff.hpp"

#include <KLocalizedString>

namespace kgt
{

SettingsProgsDiff::SettingsProgsDiff(QWidget *parent)
    : SettingsProgsTool(
          {
              .configKey = QStringLiteral("diff.tool"),
              .description = i18n("Configure the program used for comparing different revisions of files"),
              .defaultHint = i18n("By default, Diff uses your merge tool if one is chosen on the Merge Tool page, otherwise Meld, then KDiff3, then "
                                  "any other tool git finds."),
              .tools = GitDiff::defaultTools(),
          },
          parent)
{
}

} // namespace kgt
