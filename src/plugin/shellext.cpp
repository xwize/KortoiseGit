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

#include <chrono>
#include <exception>
#include <utility>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto procName = "kortoisegitproc"_L1;
// One git command gives several index events, KDirWatch reporting the replaced file about half a second after
// it went: refresh once they have settled
constexpr std::chrono::milliseconds refreshDelay{600};
// CShellExt::InsertGitMenu's limit on the branch name in the Commit entry
constexpr qsizetype maxMenuBranchLength = 64;

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

using namespace kgt;

/**
 * TortoiseGit's git_wc_status_kind, as far as the context menu tells the states apart.
 */
enum class WcStatus {
    None, // Not in a working tree
    Unversioned,
    Ignored,
    Normal,
    Modified,
    Added,
    Deleted,
    Conflicted,
};

/**
 * Ported from the GitStatus::GetStatus call in CShellExt::Initialize: one item's own state, read from git for
 * that path alone. git status gives the full state (staged additions included), as TGitCache, TortoiseGit's
 * default, does.
 * @param path The item, absolute and clean.
 * @param isDir Whether it is a folder, whose state rolls up what is below it.
 * @param topDir The working tree holding it, from GitAdminDir::projectTopDir().
 * @returns Its state.
 */
WcStatus itemStatus(const QString& path, bool isDir, const QString& topDir)
{
    // GitStatus::GetDirStatus: the working tree root is at least normal. What lies below it changes none of our
    // entries for a folder, so git isn't run for the whole tree
    if (isDir && path == topDir) {
        return WcStatus::Normal;
    }
    const QFileInfo info(path);
    try {
        switch (GitFolderStatus::fetchItem(path).itemVersion(info.fileName(), isDir)) {
        case KVersionControlPlugin::UnversionedVersion:
            return WcStatus::Unversioned;
        case KVersionControlPlugin::IgnoredVersion:
            return WcStatus::Ignored;
        case KVersionControlPlugin::NormalVersion:
        case KVersionControlPlugin::UpdateRequiredVersion:
            return WcStatus::Normal;
        case KVersionControlPlugin::LocallyModifiedVersion:
        case KVersionControlPlugin::LocallyModifiedUnstagedVersion:
            return WcStatus::Modified;
        case KVersionControlPlugin::AddedVersion:
            return WcStatus::Added;
        case KVersionControlPlugin::RemovedVersion:
        case KVersionControlPlugin::MissingVersion:
            return WcStatus::Deleted;
        case KVersionControlPlugin::ConflictingVersion:
            return WcStatus::Conflicted;
        }
    } catch (const RuntimeError& e) {
        qCDebug(KGT_PLUGIN) << "No status for the menu of" << path << ":" << e.error().message;
    } catch (const std::exception& e) {
        qCWarning(KGT_PLUGIN) << "No status for the menu of" << path << ":" << e.what();
    }
    // CShellExt::Initialize: when the status can't be read, an item in a working tree counts as normal
    return WcStatus::Normal;
}

/**
 * What CShellExt::Initialize finds out about the selection: the ITEMIS_* states the menu entries depend on.
 */
struct MenuState {
    bool onlyOne = false; // ITEMIS_ONLYONE: one item
    bool folder = false; // ITEMIS_FOLDER
    bool inGit = false; // ITEMIS_INGIT: in a working tree and versioned
    bool folderInGit = false; // ITEMIS_FOLDERINGIT: a folder in a working tree, versioned or not
    bool inVersionedFolder = false; // ITEMIS_INVERSIONEDFOLDER: in a working tree
    bool wcRoot = false; // ITEMIS_WCROOT: a working tree's top folder
    bool bareRepo = false; // ITEMIS_BAREREPO
    bool inaccessible = false; // ITEMIS_INACCESSIBLE: a folder holding a .git that isn't a valid repository
    bool ignored = false; // ITEMIS_IGNORED
    bool normal = false; // ITEMIS_NORMAL: versioned and unchanged
    bool added = false; // ITEMIS_ADDED
    bool deleted = false; // ITEMIS_DELETED
    bool bisect = false; // ITEMIS_BISECT: a bisect is running
    bool mergeActive = false; // ITEMIS_MERGEACTIVE: a merge awaits its commit
};

/**
 * Ported from CShellExt::Initialize (with CTGitPath::GetAdminDirMask): the selection's state. As there, only the
 * first item's state is read; the others only count towards ITEMIS_ONLYONE.
 *
 * Dolphin passes the folder shown, when nothing is selected, as a selected item; TortoiseGit reads the folder
 * background separately (itemStatesFolder), with the same result for the entries we have.
 * @param paths The selected paths, none inside .git.
 * @returns Their state.
 */
MenuState menuState(const QStringList& paths)
{
    Q_ASSERT(!paths.isEmpty());
    MenuState state;
    const QString& first = paths.constFirst();
    const bool isDir = QFileInfo(first).isDir();

    WcStatus status = WcStatus::None;
    if (const QString topDir = GitAdminDir::projectTopDir(first); !topDir.isEmpty()) {
        status = itemStatus(first, isDir, topDir);
        // GetAdminDirMask: every folder in a working tree counts as one in git, whatever its own state
        state.folder = isDir;
        state.inGit = true;
        state.inVersionedFolder = true;
        state.folderInGit = isDir;
        state.wcRoot = isDir && first == topDir;
        // CTGitPath::IsBisectActive and IsMergeActive
        const QDir adminDir(GitAdminDir::worktreeAdminDirPath(first));
        state.bisect = adminDir.exists(QStringLiteral("BISECT_START"));
        state.mergeActive = adminDir.exists(QStringLiteral("MERGE_HEAD"));
    }
    if (status == WcStatus::Unversioned || status == WcStatus::Ignored || status == WcStatus::None) {
        state.inGit = false;
    }
    state.ignored = status == WcStatus::Ignored;
    state.normal = status == WcStatus::Normal;
    state.added = status == WcStatus::Added;
    state.deleted = status == WcStatus::Deleted;

    if (GitAdminDir::isBareRepo(first)) {
        state = MenuState{.bareRepo = true};
    }

    state.onlyOne = paths.size() == 1;
    if (state.onlyOne && isDir) {
        state.folder = true;
        state.inaccessible = !state.bareRepo && !state.inVersionedFolder && QFileInfo::exists(QDir(first).filePath(QStringLiteral(".git")));
    }
    return state;
}

/**
 * Which of our entries TortoiseGit's menu shows.
 */
struct MenuEntries {
    bool clone = false;
    bool pull = false;
    bool fetch = false;
    bool push = false;
    bool commit = false;
    bool log = false;
    bool refBrowse = false;
    bool rebase = false;
    bool mergeAbort = false;
    bool revert = false;
    bool switchTo = false;
    bool merge = false;
    bool createRepo = false;
    bool add = false;
};

/**
 * @param state The selection's state.
 * @returns MenuInfo's conditions for Abort Merge and Merge, which only differ while a merge is in progress.
 */
std::pair<bool, bool> mergeEntries(const MenuState& state)
{
    return {
        (state.inGit || state.folderInGit) && state.mergeActive,
        state.folderInGit && state.onlyOne && !state.bisect && !state.mergeActive,
    };
}

/**
 * The entries' conditions from MenuInfo (src/TortoiseShell/MenuInfo.cpp), matched as CShellExt::ShouldInsertItem
 * does. Its ITEMIS_EXTENDED alternatives (Explorer's Shift+right-click) are left out: Dolphin has no extended menu.
 * @param state The selection's state.
 * @returns The entries shown.
 */
MenuEntries menuEntries(const MenuState& state)
{
    // Clone and CreateRepo: a folder in no repository, or one git ignores
    const bool outsideRepository = state.folder && !state.inGit && !state.folderInGit && !state.bareRepo && !state.inaccessible;
    const bool ignoredFolder = state.folder && state.ignored;
    const auto [mergeAbort, merge] = mergeEntries(state);
    return {
        .clone = outsideRepository || ignoredFolder,
        .pull = ((state.folderInGit && state.onlyOne) || state.wcRoot) && !state.bisect && !state.mergeActive,
        .fetch = (state.folderInGit && state.onlyOne) || state.bareRepo || state.wcRoot,
        // MenuInfo also offers Push on a bare repository, which our PushCommand can't push from yet
        .push = (state.folderInGit && state.onlyOne) || state.wcRoot,
        .commit = state.inGit || state.folderInGit,
        .log = (state.onlyOne && !state.added && (state.inGit || state.folderInGit)) || state.bareRepo,
        .refBrowse = (state.folderInGit && state.onlyOne) || state.bareRepo,
        .rebase = state.folderInGit && state.onlyOne && !state.bisect && !state.mergeActive,
        .mergeAbort = mergeAbort,
        .revert = (state.inGit && !state.normal) || state.folderInGit,
        .switchTo = state.folderInGit && state.onlyOne,
        .merge = merge,
        .createRepo = outsideRepository || ignoredFolder,
        .add =
            (state.inVersionedFolder && !state.inGit) || (state.inGit && state.folder) || state.ignored || (state.deleted && !state.folder && !state.onlyOne),
    };
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
    , m_pullAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-pull")), i18n("&Pull..."), this))
    , m_fetchAction(new QAction(QIcon::fromTheme(QStringLiteral("download")), i18n("F&etch..."), this))
    , m_pushAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-push")), i18n("Pus&h..."), this))
    , m_cloneAction(new QAction(QIcon::fromTheme(QStringLiteral("folder-download")), i18n("Git Clone..."), this))
    , m_refBrowseAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-branch")), i18n("Browse References"), this))
    , m_rebaseAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-rebase")), i18n("Rebase..."), this))
    , m_switchAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-branch")), i18n("S&witch/Checkout..."), this))
    , m_mergeAction(new QAction(QIcon::fromTheme(QStringLiteral("vcs-merge")), i18n("&Merge..."), this))
    , m_mergeAbortAction(new QAction(QIcon::fromTheme(QStringLiteral("dialog-cancel")), i18n("Abort &Merge"), this))
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
    m_cloneAction->setToolTip(i18n("Clone a repository"));
    connect(m_cloneAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("clone"), m_cloneAction);
    });
    m_pullAction->setToolTip(i18n("Pull (fetch and merge) commits from a remote branch."));
    connect(m_pullAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("pull"), m_pullAction);
    });
    m_fetchAction->setToolTip(i18n("Fetch commits from a remote repository."));
    connect(m_fetchAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("fetch"), m_fetchAction);
    });
    m_pushAction->setToolTip(i18n("Push commits to a remote repository."));
    connect(m_pushAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("push"), m_pushAction);
    });
    m_refBrowseAction->setToolTip(i18n("Opens the Reference Browser dialog."));
    connect(m_refBrowseAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("refbrowse"), m_refBrowseAction);
    });
    // TortoiseGit's description of Rebase is its menu text (IDS_MENUREBASE)
    m_rebaseAction->setToolTip(i18n("Rebase..."));
    connect(m_rebaseAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("rebase"), m_rebaseAction);
    });
    m_switchAction->setToolTip(i18n("Switch/Checkout working tree to another branch/tag"));
    connect(m_switchAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("switch"), m_switchAction);
    });
    m_mergeAction->setToolTip(i18n("Merges another branch"));
    connect(m_mergeAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("merge"), m_mergeAction);
    });
    m_mergeAbortAction->setToolTip(i18n("Aborts a running merge."));
    connect(m_mergeAbortAction, &QAction::triggered, this, [this] {
        runCommand(QStringLiteral("merge"), m_mergeAbortAction, {QStringLiteral("--abort")});
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
    return m_folderStatus->itemVersion(item.name(), item.isDir());
}

QList<QAction *> ShellExt::versionControlActions(const KFileItemList& items) const
{
    return menuActions(localPaths(items));
}

QList<QAction *> ShellExt::outOfVersionControlActions(const KFileItemList& items) const
{
    // Dolphin decides which of the two calls it makes, by the folder it shows; TortoiseGit has one menu, decided
    // by the items, so both give the same
    return menuActions(localPaths(items));
}

QList<QAction *> ShellExt::menuActions(const QStringList& selected) const
{
    // CShellExt::Initialize: items in a .git directory get no menu
    QStringList paths;
    for (const QString& path : selected) {
        if (!GitAdminDir::isAdminDirPath(path)) {
            paths.append(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
        }
    }
    if (paths.isEmpty()) {
        return {};
    }
    const MenuEntries entries = menuEntries(menuState(paths));

    // In MenuInfo's order
    QList<QAction *> actions;
    const auto add = [&actions, &paths](bool shown, QAction *action) {
        if (shown) {
            action->setData(paths);
            actions.append(action);
        }
    };
    add(entries.clone, m_cloneAction);
    add(entries.pull, m_pullAction);
    add(entries.fetch, m_fetchAction);
    add(entries.push, m_pushAction);
    if (entries.commit) {
        m_commitAction->setText(commitMenuText(paths.constFirst()));
    }
    add(entries.commit, m_commitAction);
    add(entries.log, m_showLogAction);
    add(entries.refBrowse, m_refBrowseAction);
    add(entries.rebase, m_rebaseAction);
    add(entries.mergeAbort, m_mergeAbortAction);
    add(entries.revert, m_revertAction);
    add(entries.switchTo, m_switchAction);
    add(entries.merge, m_mergeAction);
    add(entries.createRepo, m_createRepoAction);
    add(entries.add, m_addAction);
    return actions;
}

void ShellExt::watchForNewRepository(const QString& folder)
{
    if (!m_watchedNewRepo.isEmpty()) {
        m_newRepoWatch.removeDir(m_watchedNewRepo);
    }
    m_watchedNewRepo = QDir(folder).filePath(QStringLiteral(".git"));
    m_newRepoWatch.addDir(m_watchedNewRepo);
}

void ShellExt::runCommand(const QString& command, const QAction *action, const QStringList& switches)
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
    arguments << switches;

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
