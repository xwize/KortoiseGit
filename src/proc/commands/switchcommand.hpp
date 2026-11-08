// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/SwitchCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Switches the working tree to another branch, tag or commit ("switch"): asks what in GitSwitchDlg, then runs
 * git checkout in ProgressDlg, as CAppUtils::Switch and PerformSwitch do. --rev preselects a ref.
 */
class SwitchCommand : public Command
{
public:
    SwitchCommand();

    /**
     * Shows GitSwitchDlg and, if accepted, the checkout's progress.
     * @returns True if git checkout succeeded.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
