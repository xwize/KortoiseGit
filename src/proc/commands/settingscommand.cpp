// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2009, 2016-2018, 2020, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/SettingsCommand.cpp.
 */

#include "commands/settingscommand.hpp"

#include "git/gitadmindir.hpp"
#include "settings/settings.hpp"

#include <QDir>
#include <QFileInfo>

namespace kgt
{

SettingsCommand::SettingsCommand()
    : Command(PathRequirement::PathRequired)
{
}

bool SettingsCommand::execute()
{
    Settings dlg(repository(paths().constFirst()));
    dlg.setDefaultPage(value(QStringLiteral("page")));
    dlg.exec();
    return true;
}

QString SettingsCommand::repository(const QString& path)
{
    const QFileInfo info(path);
    const QString folder = QDir::cleanPath(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    if (GitAdminDir::isBareRepo(folder)) {
        return folder;
    }
    return GitAdminDir::projectTopDir(folder);
}

} // namespace kgt
