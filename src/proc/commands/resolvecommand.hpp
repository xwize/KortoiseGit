// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/ResolveCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Marks conflicted files as resolved: the Resolve dialog picks them, and the progress dialog runs git add on them.
 */
class ResolveCommand : public Command
{
public:
    ResolveCommand();

    /**
     * Shows the Resolve dialog and resolves the checked files.
     * @returns True if they were resolved, false if the user cancelled, nothing was checked or git failed.
     * @throws RuntimeError if the paths are not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
