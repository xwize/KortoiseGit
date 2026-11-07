// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2013, 2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/AddCommand.cpp. TortoiseGit adds in its progress dialog
 * (CGitProgressDlg with AddProgressCommand), which is not ported: files are added silently and only an error is
 * shown.
 */

#include "commands/addcommand.hpp"

#include "adddlg.hpp"
#include "git/git.hpp"
#include "git/massivegittask.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QFileInfo>
#include <QMessageBox>

#include <algorithm>

namespace kgt
{

AddCommand::AddCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool AddCommand::execute()
{
    const QString workingTree = Git::workingTreeRoot(paths().constFirst());
    // CTGitPathList::AreAllPathsFiles: there is nothing to choose from, so no dialog
    const bool allFiles = std::ranges::none_of(paths(), [](const QString& path) {
        return QFileInfo(path).isDir();
    });
    if (!allFiles) {
        AddDlg dlg(workingTree, paths());
        return dlg.exec() == QDialog::Accepted;
    }
    MassiveGitTask task({QStringLiteral("add"), QStringLiteral("-f")});
    task.setPaths(AddDlg::relativePaths(workingTree, paths()));
    try {
        task.run(Git(workingTree));
    } catch (const RuntimeError& e) {
        switch (e.error().category) {
        case ErrorCategory::Git:
            QMessageBox::critical(nullptr, i18n("KortoiseGit"), e.error().message);
            return false;
        default:
            throw;
        }
    }
    return true;
}

} // namespace kgt
