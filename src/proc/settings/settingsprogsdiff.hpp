// SPDX-FileCopyrightText: 2003-2007, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2011, 2013-2016 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingsProgsDiff.h (CSettingsProgsDiff), adapted as settingsprogstool.hpp explains.
 */

#pragma once

#include "settings/settingsprogstool.hpp"

namespace kgt
{

/**
 * The Settings dialog's "Diff Viewer" page: the tool Diff starts through git difftool, the diff.tool setting.
 */
class SettingsProgsDiff : public SettingsProgsTool
{
    Q_OBJECT

public:
    /**
     * @param parent Parent widget.
     */
    explicit SettingsProgsDiff(QWidget *parent = nullptr);
};

} // namespace kgt
