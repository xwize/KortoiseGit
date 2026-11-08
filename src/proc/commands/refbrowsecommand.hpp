// SPDX-FileCopyrightText: 2009, 2012-2013, 2016, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RefBrowseCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the Browse References dialog ("refbrowse") for the repository holding the path.
 */
class RefBrowseCommand : public Command
{
public:
    RefBrowseCommand();

    /**
     * Shows BrowseRefsDlg until the user closes it.
     * @returns True.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
