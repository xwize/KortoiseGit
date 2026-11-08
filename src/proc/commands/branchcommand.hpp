// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2009, 2015-2016, 2018-2020, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/BranchCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Creates a branch ("branch"): asks how in CreateBranchTagDlg, then runs git branch, as CAppUtils::CreateBranchTag
 * does. --rev preselects what to base it on and --name fills in its name.
 */
class BranchCommand : public Command
{
public:
    BranchCommand();

    /**
     * Shows CreateBranchTagDlg and, if accepted, creates the branch.
     * @returns True if the branch was created.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
