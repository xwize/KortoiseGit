// SPDX-FileCopyrightText: 2009, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/RefLogCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the RefLog dialog, which is also TortoiseGit's Stash List when started on refs/stash.
 */
class RefLogCommand : public Command
{
public:
    RefLogCommand();

    /**
     * Shows the dialog until the user closes it. --ref is the ref it starts on, HEAD without one.
     * @returns True, once the dialog is closed.
     * @throws RuntimeError if the path is in neither a working tree nor a bare repository.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
