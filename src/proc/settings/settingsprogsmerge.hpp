// SPDX-FileCopyrightText: 2003-2007 TortoiseSVN
// SPDX-FileCopyrightText: 2009, 2011, 2013-2016, 2019-2020 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingsProgsMerge.h (CSettingsProgsMerge), adapted as settingsprogstool.hpp explains.
 */

#pragma once

#include "settings/settingsprogstool.hpp"

namespace kgt
{

/**
 * The Settings dialog's "Merge Tool" page, under "Diff Viewer": the tool Edit conflicts starts through git mergetool,
 * the merge.tool setting.
 */
class SettingsProgsMerge : public SettingsProgsTool
{
    Q_OBJECT

public:
    /**
     * @param parent Parent widget.
     */
    explicit SettingsProgsMerge(QWidget *parent = nullptr);
};

} // namespace kgt
