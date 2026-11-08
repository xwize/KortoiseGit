// SPDX-FileCopyrightText: 2003-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Utils/PathUtils.cpp (GetAppDirectory)

#include "pathutils.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <dlfcn.h>

namespace
{

/**
 * The directory holding this plugin's .so, like TortoiseGit's CPathUtils::GetAppDirectory(g_hmodThisDll).
 * @returns The absolute directory, or an empty string if the dynamic loader can't say.
 */
QString appDirectory()
{
    static const char anchor = 0;
    Dl_info info{};
    if (dladdr(&anchor, &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return QFileInfo(QFile::decodeName(info.dli_fname)).absolutePath();
}

} // namespace

namespace kgt
{

QString findSiblingExecutable(const QString& name)
{
    const QString pluginDir = appDirectory();
    const QStringList candidates{
        pluginDir.isEmpty() ? QString() : QDir(pluginDir).absoluteFilePath(QStringLiteral("../../") + name),
        QDir(QStringLiteral(KGT_INSTALL_BINDIR)).absoluteFilePath(name),
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (!candidate.isEmpty() && info.isFile() && info.isExecutable()) {
            return info.canonicalFilePath();
        }
    }
    return QStandardPaths::findExecutable(name);
}

} // namespace kgt
