// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2018, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CommitCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the commit dialog for the given paths.
 */
class CommitCommand : public Command
{
public:
    CommitCommand();

    /**
     * Shows the commit dialog until the user commits or cancels.
     * @returns True if a commit was made.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
