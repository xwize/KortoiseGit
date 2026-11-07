// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shellext.hpp"

#include "git/gitadmindir.hpp"
#include "kortoisegitplugin_debug.hpp"
#include "utils/runtimeerror.hpp"

#include <KFileItem>
#include <KLocalizedString>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
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
// CShellExt::InsertGitMenu's limit on the branch name in the Commit entry
constexpr qsizetype maxMenuBranchLength = 64;
// How many folders' states the context menu keeps: those Dolphin shows at once, expanded ones included
constexpr qsizetype maxMenuFolders = 64;

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
    , m_commitAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-commit")), i18n("Git &Commit..."), this))
    , m_createRepoAction(new QAction(QIcon::fromTheme(QStringLiteral("folder-git")), i18n("Git Create repositor&y here..."), this))
    , m_showLogAction(new QAction(QIcon::fromTheme(QStringLiteral("view-history")), i18n("Show &log"), this))
    , m_revertAction(new QAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Re&vert..."), this))
    , m_addAction(new QAction(QIcon::fromTheme(QStringLiteral("list-add")), i18n("&Add..."), this))
{
    Q_UNUSED(args)
    connect(m_createRepoAction, &QAction::triggered, this, [this] {
        watchForNewRepository(m_createRepoAction->data().toStringList().constFirst());
        runCommand(QStringLiteral("repocreate"), m_createRepoAction);
    });
    connect(m_commitAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("commit"), m_commitAction);
    });
    connect(m_showLogAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("log"), m_showLogAction);
    });
    m_revertAction->setToolTip(i18n("Reverts all changes you made since the last update"));
    connect(m_revertAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("revert"), m_revertAction);
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
    // Dolphin looks for a plugin again on itemVersionsChanged(), finding the new repository
    connect(&m_newRepoWatch, &KDirWatch::created, &m_refreshTimer, qOverload<>(&QTimer::start));
}

QString ShellExt::fileName() const
{
    return QStringLiteral(".git");
}

bool ShellExt::beginRetrieval(const QString& directory)
{
    m_folderStatus.reset();
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
        [this, directory, status = *m_folderStatus] {
            watchIndex(status.indexFile());
            setMenuStatus(directory, status);
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
    return m_folderStatus->itemVersion(item.name(), item.isDir());
}

QList<QAction *> ShellExt::versionControlActions(const KFileItemList& items) const
{
    const QStringList paths = localPaths(items);
    if (paths.isEmpty()) {
        return {};
    }
    QList<QAction *> actions;
    if (hasVersionedItem(items)) {
        m_commitAction->setText(commitMenuText(paths.constFirst()));
        m_commitAction->setData(paths);
        actions.append(m_commitAction);
    }
    m_showLogAction->setData(paths);
    actions.append(m_showLogAction);
    // TortoiseGit's menu order: Commit, Show log, ..., Revert, ..., Add
    if (hasRevertableItem(items)) {
        m_revertAction->setData(paths);
        actions.append(m_revertAction);
    }
    if (hasAddableItem(items)) {
        m_addAction->setData(paths);
        actions.append(m_addAction);
    }
    return actions;
}

QList<QAction *> ShellExt::outOfVersionControlActions(const KFileItemList& items) const
{
    // TortoiseGit's CreateRepo: one folder, not in a working tree, not a bare repository, and accessible
    if (items.size() != 1 || !items.constFirst().isDir()) {
        return {};
    }
    const QString folder = items.constFirst().localPath();
    if (folder.isEmpty() || !QFileInfo(folder).isReadable() || !GitAdminDir::worktreeAdminDirPath(folder).isEmpty() || GitAdminDir::isBareRepo(folder)) {
        return {};
    }
    m_createRepoAction->setData(QStringList{folder});
    return {m_createRepoAction};
}

void ShellExt::watchForNewRepository(const QString& folder)
{
    if (!m_watchedNewRepo.isEmpty()) {
        m_newRepoWatch.removeDir(m_watchedNewRepo);
    }
    m_watchedNewRepo = QDir(folder).filePath(QStringLiteral(".git"));
    m_newRepoWatch.addDir(m_watchedNewRepo);
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

void ShellExt::setMenuStatus(const QString& directory, const GitFolderStatus& status)
{
    const QString key = QDir::cleanPath(directory);
    // Forget the folders browsed long ago rather than grow without bound
    if (m_menuStatus.size() >= maxMenuFolders && !m_menuStatus.contains(key)) {
        m_menuStatus.clear();
    }
    m_menuStatus.insert_or_assign(key, status);
}

const GitFolderStatus *ShellExt::menuStatus(const QString& directory) const
{
    const auto found = m_menuStatus.constFind(directory);
    return found == m_menuStatus.cend() ? nullptr : &found.value();
}

bool ShellExt::hasVersionedItem(const KFileItemList& items) const
{
    return std::ranges::any_of(items, [this](const KFileItem& item) {
        const QFileInfo info(QDir::cleanPath(item.localPath()));
        const GitFolderStatus *status = menuStatus(info.absolutePath());
        if (!status) {
            return true;
        }
        const ItemVersion version = status->itemVersion(info.fileName(), item.isDir());
        return version != UnversionedVersion && version != IgnoredVersion;
    });
}

bool ShellExt::hasRevertableItem(const KFileItemList& items) const
{
    // TortoiseGit's Revert entry: a versioned file that is not unchanged, or a folder in a working tree
    return std::ranges::any_of(items, [this](const KFileItem& item) {
        const QFileInfo info(QDir::cleanPath(item.localPath()));
        if (info.fileName() == QLatin1String(".git")) {
            return false;
        }
        const GitFolderStatus *status = menuStatus(info.absolutePath());
        if (!status) {
            // Not listed yet, such as the folder Dolphin shows when nothing is selected
            return item.isDir();
        }
        const ItemVersion version = status->itemVersion(info.fileName(), item.isDir());
        if (version == UnversionedVersion || version == IgnoredVersion) {
            return false;
        }
        return item.isDir() || version != NormalVersion;
    });
}

bool ShellExt::hasAddableItem(const KFileItemList& items) const
{
    return std::ranges::any_of(items, [this](const KFileItem& item) {
        const QString path = QDir::cleanPath(item.localPath());
        // The folder Dolphin shows, when nothing is selected, is judged by its own listing
        if (const GitFolderStatus *own = item.isDir() ? menuStatus(path) : nullptr; own && own->hasUnversioned()) {
            return true;
        }
        const QFileInfo info(path);
        const GitFolderStatus *status = menuStatus(info.absolutePath());
        // ".git" is reported as unversioned so it gets no overlay, but there is nothing in it to add
        if (!status || info.fileName() == QLatin1String(".git")) {
            return false;
        }
        const ItemVersion version = status->itemVersion(info.fileName(), item.isDir());
        return version == UnversionedVersion || version == IgnoredVersion || (item.isDir() && status->hasUnversioned(info.fileName()));
    });
}

QString ShellExt::commitMenuText(const QString& path)
{
    QString branch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(path));
    if (branch.isEmpty()) {
        return i18n("Git &Commit...");
    }
    if (branch.size() > maxMenuBranchLength) {
        branch = branch.left(maxMenuBranchLength) + QStringLiteral("...");
    }
    // EscapeAccellerators: a '&' in the branch name is not a shortcut
    branch.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return i18nc("@action:inmenu %1 is the current branch", "Git &Commit -> \"%1\"...", branch);
}

} // namespace kgt
