// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"

#include "kortoisegitplugin_debug.hpp"
#include "utils/runtimeerror.hpp"

#include <KFileItem>
#include <KLocalizedString>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <exception>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto procName = "kortoisegitproc"_L1;
// One git command gives several index events, KDirWatch reporting the replaced file about half a second after
// it went: refresh once they have settled
constexpr std::chrono::milliseconds refreshDelay{600};

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
    , m_addAction(new QAction(QIcon::fromTheme(QStringLiteral("list-add")), i18n("&Add..."), this))
{
    Q_UNUSED(args)
    connect(m_showLogAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("log"), m_showLogAction);
    });
    m_addAction->setToolTip(i18n("Adds file(s) to Git control"));
    connect(m_addAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("add"), m_addAction);
    });

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(refreshDelay);
    connect(&m_refreshTimer, &QTimer::timeout, this, &KVersionControlPlugin::itemVersionsChanged);
    // git replaces the index by renaming index.lock over it, which KDirWatch may report as any of these
    connect(&m_indexWatch, &KDirWatch::dirty, &m_refreshTimer, qOverload<>(&QTimer::start));
    connect(&m_indexWatch, &KDirWatch::created, &m_refreshTimer, qOverload<>(&QTimer::start));
    connect(&m_indexWatch, &KDirWatch::deleted, &m_refreshTimer, qOverload<>(&QTimer::start));
}

QString ShellExt::fileName() const
{
    return QStringLiteral(".git");
}

bool ShellExt::beginRetrieval(const QString& directory)
{
    m_folderStatus.reset();
    m_retrievalDirectory = QDir::cleanPath(directory);
    {
        const QMutexLocker locker(&m_addableMutex);
        m_addable.remove(m_retrievalDirectory);
    }
    try {
        m_folderStatus = GitFolderStatus::fetch(directory);
    } catch (const RuntimeError& e) {
        // Every category means the same here: no overlays. Nothing may propagate into Dolphin
        qCDebug(KGT_PLUGIN) << "No overlays for" << directory << ":" << e.error().message;
        return false;
    } catch (const std::exception& e) {
        qCWarning(KGT_PLUGIN) << "No overlays for" << directory << ":" << e.what();
        return false;
    }
    QMetaObject::invokeMethod(
        this,
        [this, indexFile = m_folderStatus->indexFile()] {
            watchIndex(indexFile);
        },
        Qt::QueuedConnection);
    return true;
}

void ShellExt::endRetrieval()
{
}

KVersionControlPlugin::ItemVersion ShellExt::itemVersion(const KFileItem& item) const
{
    if (!m_folderStatus) {
        return UnversionedVersion;
    }
    const QString name = item.name();
    const ItemVersion version = m_folderStatus->itemVersion(name, item.isDir());
    const bool hasUnversioned = version == UnversionedVersion || (item.isDir() && m_folderStatus->hasUnversioned(name));
    // ".git" is reported as unversioned so it gets no overlay, but there is nothing in it to add
    if (name != QLatin1String(".git") && (hasUnversioned || version == IgnoredVersion)) {
        const QMutexLocker locker(&m_addableMutex);
        AddableItems& addable = m_addable.try_emplace(m_retrievalDirectory).first->second;
        addable.names.insert(name);
        addable.hasUnversioned = addable.hasUnversioned || hasUnversioned;
    }
    return version;
}

QList<QAction *> ShellExt::versionControlActions(const KFileItemList& items) const
{
    const QStringList paths = localPaths(items);
    if (paths.isEmpty()) {
        return {};
    }
    m_showLogAction->setData(paths);
    if (!canAdd(items)) {
        return {m_showLogAction};
    }
    m_addAction->setData(paths);
    return {m_showLogAction, m_addAction};
}

QList<QAction *> ShellExt::outOfVersionControlActions(const KFileItemList& items) const
{
    Q_UNUSED(items)
    return {};
}

bool ShellExt::canAdd(const KFileItemList& items) const
{
    const QMutexLocker locker(&m_addableMutex);
    return std::ranges::any_of(items, [this](const KFileItem& item) {
        const QString path = QDir::cleanPath(item.localPath());
        const QFileInfo info(path);
        // The item as a child of its folder, or a folder Dolphin shows (with nothing selected) that holds new files
        return m_addable.value(info.absolutePath()).names.contains(info.fileName()) || (item.isDir() && m_addable.value(path).hasUnversioned);
    });
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

void ShellExt::watchIndex(const QString& indexFile)
{
    if (indexFile == m_watchedIndex) {
        return;
    }
    if (!m_watchedIndex.isEmpty()) {
        m_indexWatch.removeFile(m_watchedIndex);
    }
    m_indexWatch.addFile(indexFile);
    m_watchedIndex = indexFile;
}

} // namespace kgt
