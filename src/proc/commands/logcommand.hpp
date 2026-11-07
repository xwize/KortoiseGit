// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/LogCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the log dialog for the given paths.
 */
class LogCommand : public Command
{
public:
    LogCommand();

    /**
     * Shows the log dialog until the user closes it.
     * @returns True.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
