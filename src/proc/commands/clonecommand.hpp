// SPDX-FileCopyrightText: 2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2021-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CloneCommand.h.
 */

#pragma once

#include "commands/command.hpp"

#include <QString>
#include <QStringList>

namespace kgt
{

struct CloneOptions;

/**
 * Clones ("clone"): asks what to clone where in CloneDlg, then runs git clone in ProgressDlg.
 */
class CloneCommand : public Command
{
public:
    CloneCommand();

    /**
     * Shows CloneDlg, starting at --path (or the current directory), and, if accepted, the clone's progress.
     * @returns True if git clone succeeded.
     */
    [[nodiscard]] bool execute() override;

    /**
     * The command line CloneCommand::Execute builds: git clone --progress, the options, -v, then the URL and
     * directory after "--".
     * @param options What to clone where.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList cloneArguments(const CloneOptions& options);

    /**
     * Ported from GetExistingDirectoryForClone: where git runs, since the clone's directory may not exist yet.
     * @param path The directory to clone into.
     * @returns It, or its nearest existing ancestor, or the temporary directory if none exists.
     */
    [[nodiscard]] static QString existingDirectoryForClone(const QString& path);
};

} // namespace kgt
