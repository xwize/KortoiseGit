// SPDX-FileCopyrightText: 2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2021-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CloneCommand.cpp. git-svn, PuTTY keys, libgit2 cloning, the
 * --url and --exactpath options and the Retry, Show log and Explore buttons after the clone are left out for now.
 */

#include "commands/clonecommand.hpp"

#include "clonedlg.hpp"
#include "progressdlg.hpp"

#include <QDir>
#include <QFileInfo>

namespace kgt
{

CloneCommand::CloneCommand()
    : Command(PathRequirement::PathRequired)
{
}

bool CloneCommand::execute()
{
    CloneDlg dlg(QDir::cleanPath(paths().constFirst()));
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    const CloneOptions options = dlg.options();
    ProgressDlg progress(existingDirectoryForClone(options.directory));
    progress.addGitCmd(cloneArguments(options));
    progress.exec();
    return progress.gitStatus() == 0;
}

QStringList CloneCommand::cloneArguments(const CloneOptions& options)
{
    QStringList arguments{QStringLiteral("clone"), QStringLiteral("--progress")};
    if (options.recursive) {
        arguments.append(QStringLiteral("--recursive"));
    }
    if (options.bare) {
        arguments.append(QStringLiteral("--bare"));
    }
    if (options.noCheckout) {
        arguments.append(QStringLiteral("--no-checkout"));
    }
    if (options.branch) {
        arguments << QStringLiteral("--branch") << options.branchName;
    }
    if (options.origin) {
        arguments << QStringLiteral("--origin") << options.originName;
    }
    if (options.depth) {
        arguments << QStringLiteral("--depth") << QString::number(options.depthValue);
    }
    arguments << QStringLiteral("-v") << QStringLiteral("--") << options.url << options.directory;
    return arguments;
}

QString CloneCommand::existingDirectoryForClone(const QString& path)
{
    QString directory = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!QFileInfo(directory).isDir()) {
        if (directory == QDir::rootPath()) {
            return QDir::tempPath();
        }
        directory = QFileInfo(directory).absolutePath();
    }
    return directory;
}

} // namespace kgt
