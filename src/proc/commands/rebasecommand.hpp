// SPDX-FileCopyrightText: 2009-2012, 2015-2016, 2018-2019, 2021, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RebaseCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Rebases a branch ("rebase") in RebaseDlg. --upstream preselects the upstream and --force checks "Force Rebase".
 */
class RebaseCommand : public Command
{
public:
    RebaseCommand();

    /**
     * Shows RebaseDlg.
     * @returns True if the rebase finished and the dialog was closed with Done.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
