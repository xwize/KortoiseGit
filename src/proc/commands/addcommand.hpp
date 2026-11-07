// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2008, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/AddCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Adds files to the index. When every path is a file they are added straight away; otherwise the Add dialog lets
 * the user pick from the unversioned files below the paths.
 */
class AddCommand : public Command
{
public:
    AddCommand();

    /**
     * Adds the files, showing the Add dialog if any path is a folder, and git's error if git add fails.
     * @returns True if files were added, false if the user cancelled or git failed.
     * @throws RuntimeError if the paths are not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
