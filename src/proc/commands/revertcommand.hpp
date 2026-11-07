// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RevertCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Discards local changes: the Revert dialog lets the user pick the changed files below the paths and reverts them.
 */
class RevertCommand : public Command
{
public:
    RevertCommand();

    /**
     * Shows the Revert dialog until the user reverts or cancels.
     * @returns True if files were reverted, false if the user cancelled.
     * @throws RuntimeError if the paths are not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
