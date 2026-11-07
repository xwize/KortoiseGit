// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CreateRepositoryCommand.h.
 */

#pragma once

#include "commands/command.hpp"

#include <QString>

class QWidget;

namespace kgt
{

/**
 * Creates a repository in a folder ("repocreate"): warns about unsuitable folders, asks in CreateRepoDlg whether
 * it is bare, runs git init and reports the result.
 */
class CreateRepositoryCommand : public Command
{
public:
    CreateRepositoryCommand();

    /**
     * Runs the checks, the dialog and git init.
     * @returns True if a repository was created.
     */
    [[nodiscard]] bool execute() override;

    /**
     * Ported from CheckSpecialFolder: the file system root and the user's home, desktop and documents folders,
     * and the system's own folders, are not meant to be repositories.
     * @param folder An absolute folder.
     * @returns Whether it is one of those.
     */
    [[nodiscard]] static bool isSpecialFolder(const QString& folder);

    /**
     * @param folder An absolute folder.
     * @returns The admin directory of the repository the folder is or is inside (a working tree's .git, or the
     *          folder itself when it is a bare repository), or an empty string if there is none.
     */
    [[nodiscard]] static QString existingRepository(const QString& folder);

    /**
     * Runs git init, creating the folder if needed (GIT_REPOSITORY_INIT_MKPATH).
     * @param folder An absolute folder.
     * @param bare Whether to create a bare repository.
     * @throws RuntimeError (category Git) if git fails.
     */
    static void initRepository(const QString& folder, bool bare);
};

} // namespace kgt
