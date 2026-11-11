// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2009, 2016-2018, 2020, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/SettingsCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Shows the Settings dialog ("settings"), on the page --page names. A path in a working tree or a bare repository
 * adds that repository's pages; without one the current directory decides.
 */
class SettingsCommand : public Command
{
public:
    SettingsCommand();

    /**
     * Shows the Settings dialog until the user closes it.
     * @returns True.
     */
    [[nodiscard]] bool execute() override;

    /**
     * CSettings::AddPropPages' hasLocalRepo, GitAdminDir::IsWorkingTreeOrBareRepo.
     * @param path A file or folder.
     * @returns The root of the working tree holding it, or the bare repository it is; empty if it is in neither.
     */
    [[nodiscard]] static QString repository(const QString& path);
};

} // namespace kgt
