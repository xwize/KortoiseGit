// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"

#include "kortoisegitplugin_debug.hpp"

#include <KFileItem>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>

#include <dlfcn.h>

// NOLINTNEXTLINE(readability-identifier-naming) KPluginFactory names its factory class after the plugin target
K_PLUGIN_CLASS_WITH_JSON(kgt::ShellExt, "kortoisegitplugin.json")

namespace
{

using namespace Qt::StringLiterals;

constexpr auto procName = "kortoisegitproc"_L1;

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

/**
 * Finds kortoisegitproc. From the build tree the plugin is bin/dolphin/vcs/kortoisegitplugin.so and the app is
 * bin/kortoisegitproc, so that is tried first; then the install location, then $PATH.
 * @returns The executable's path, or an empty string if none was found.
 */
QString procPath()
{
    const QString pluginDir = appDirectory();
    const QStringList candidates{
        pluginDir.isEmpty() ? QString() : QDir(pluginDir).absoluteFilePath(QStringLiteral("../../") + procName),
        QDir(QStringLiteral(KGT_INSTALL_BINDIR)).absoluteFilePath(procName),
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (!candidate.isEmpty() && info.isFile() && info.isExecutable()) {
            return info.canonicalFilePath();
        }
    }
    return QStandardPaths::findExecutable(QString(procName));
}

/**
 * @param items Items from Dolphin's selection.
 * @returns The local paths of @p items; items without one (remote URLs) are left out.
 */
QStringList localPaths(const KFileItemList& items)
{
    QStringList paths;
    for (const KFileItem& item : items) {
        const QString path = item.localPath();
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

} // namespace

namespace kgt
{

ShellExt::ShellExt(QObject *parent, const QVariantList& args)
    : KVersionControlPlugin(parent)
    , m_showLogAction(new QAction(QIcon::fromTheme(QStringLiteral("view-history")), i18n("Show &log"), this))
{
    Q_UNUSED(args)
    connect(m_showLogAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("log"), m_showLogAction);
    });
}

QString ShellExt::fileName() const
{
    return QStringLiteral(".git");
}

bool ShellExt::beginRetrieval(const QString& directory)
{
    Q_UNUSED(directory)
    return true;
}

void ShellExt::endRetrieval()
{
}

KVersionControlPlugin::ItemVersion ShellExt::itemVersion(const KFileItem& item) const
{
    Q_UNUSED(item)
    return UnversionedVersion;
}

QList<QAction *> ShellExt::versionControlActions(const KFileItemList& items) const
{
    const QStringList paths = localPaths(items);
    if (paths.isEmpty()) {
        return {};
    }
    m_showLogAction->setData(paths);
    return {m_showLogAction};
}

QList<QAction *> ShellExt::outOfVersionControlActions(const KFileItemList& items) const
{
    Q_UNUSED(items)
    return {};
}

void ShellExt::runCommand(const QString& command, const QAction *action)
{
    const QStringList paths = action->data().toStringList();
    Q_ASSERT(!paths.isEmpty());

    const QString program = procPath();
    if (program.isEmpty()) {
        qCWarning(KGT_PLUGIN) << "kortoisegitproc not found next to the plugin, in" << KGT_INSTALL_BINDIR << "or in $PATH";
        Q_EMIT errorMessage(i18n("KortoiseGitProc launch failed: kortoisegitproc was not found"));
        return;
    }

    QStringList arguments{QStringLiteral("--command"), command};
    for (const QString& path : paths) {
        arguments << QStringLiteral("--path") << path;
    }

    // Run from the first item's folder, as TortoiseGitProc gets Explorer's current folder
    const QFileInfo first(paths.constFirst());
    const QString workingDirectory = first.isDir() ? first.absoluteFilePath() : first.absolutePath();

    qCDebug(KGT_PLUGIN) << "Starting" << program << arguments;
    if (!QProcess::startDetached(program, arguments, workingDirectory)) {
        qCWarning(KGT_PLUGIN) << "Failed to start" << program;
        Q_EMIT errorMessage(i18n("KortoiseGitProc launch failed"));
    }
}

} // namespace kgt

#include "shellext.moc"
