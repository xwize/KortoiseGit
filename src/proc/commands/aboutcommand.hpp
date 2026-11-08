// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/AboutCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the About dialog ("about"). It needs no path and no repository.
 */
class AboutCommand : public Command
{
public:
    AboutCommand();

    /**
     * Shows AboutDlg until the user closes it.
     * @returns True.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
