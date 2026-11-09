// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitStatusListCtrl.cpp

#include "gitstatuslistctrl.hpp"

#include "apputils.hpp"
#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "gitdiff.hpp"
#include "progresscommands/revertprogresscommand.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KIO/ApplicationLauncherJob>
#include <KIO/DeleteOrTrashJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KJob>
#include <KLocalizedString>

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QSortFilterProxyModel>
#include <QUrl>

#include <algorithm>
#include <iterator>
#include <tuple>
#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

// The first status header when the branch has no commit yet: there is no HEAD
constexpr QByteArrayView initialBranchHeader = "# branch.oid (initial)";

/**
 * Takes a finished call out of its owner. It is deleted from the event loop, since it may still be emitting.
 * @param[in,out] call The owner; left empty.
 * @returns The call, valid until control returns to the event loop.
 */
GitCall *takeFinished(std::unique_ptr<GitCall>& call)
{
    GitCall *finished = call.release();
    Q_ASSERT(finished);
    finished->deleteLater();
    return finished;
}

/**
 * @param entry An entry.
 * @returns Whether git tracks the file, so it can be compared with HEAD.
 */
bool isTracked(const GitStatusListEntry& entry)
{
    return !(entry.actions & (Action::Unversioned | Action::Ignore));
}

/**
 * @param entry An entry.
 * @returns Whether the file is gone from the working tree, so it can't be opened.
 */
bool isGone(const GitStatusListEntry& entry)
{
    return (entry.actions & (Action::Deleted | Action::Missing)) != 0;
}

/**
 * @param entry The entry the context menu was opened on.
 * @returns Whether the menu offers Delete (CGitStatusListCtrl::OnContextMenuList).
 */
bool offersDelete(const GitStatusListEntry& entry)
{
    return (entry.actions & (Action::Unversioned | Action::Ignore | Action::Missing)) != 0;
}

/**
 * Shows an error without a nested event loop, window-modal.
 * @param parent The window to block.
 * @param message What went wrong.
 */
void showError(QWidget *parent, const QString& message)
{
    auto *box = new QMessageBox(QMessageBox::Critical, i18n("KortoiseGit"), message, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

/**
 * Sorts the list within its groups, keeping the groups themselves in order whichever way it is sorted, as the
 * Windows list view does.
 */
class GitStatusListProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const auto *model = qobject_cast<const GitStatusListModel *>(sourceModel());
        if (model && model->hasGroups() && !left.parent().isValid()) {
            return (sortOrder() == Qt::AscendingOrder) == (left.row() < right.row());
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

/**
 * The length of a hash in a group header (CGit::GetShortHASHLength).
 */
constexpr qsizetype shortHashLength = 8;

constexpr qsizetype maxPathSpecLength = 128LL * 1024; // Well under Linux's usual 2 MiB for the arguments and the environment

} // namespace

namespace kgt
{

GitStatusListCtrl::GitStatusListCtrl(QWidget *parent)
    : QTreeView(parent)
    , m_model(new GitStatusListModel(this))
    , m_proxy(new GitStatusListProxy(this))
    , m_emptyString(i18n("File list is empty"))
    , m_busyString(i18n("Please wait..."))
{
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(GitStatusListModel::SortRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    setModel(m_proxy);

    setRootIsDecorated(false);
    // A list view's groups are neither indented nor collapsible
    setIndentation(0);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(GitStatusListModel::PathColumn, QHeaderView::Stretch);
    // Unsorted until the user clicks a header, as in TortoiseGit: git's order, tracked files first
    header()->setSortIndicator(-1, Qt::AscendingOrder);
    setSortingEnabled(true);

    // After the view's own reset, which has forgotten which rows span and are open
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &GitStatusListCtrl::showGroups);
    connect(this, &QAbstractItemView::doubleClicked, this, &GitStatusListCtrl::onDoubleClicked);
    connect(m_model, &GitStatusListModel::checkedCountChanged, this, &GitStatusListCtrl::updateCheckedCount);
}

GitStatusListCtrl::~GitStatusListCtrl() = default;

void GitStatusListCtrl::setWorkingTree(const QString& root)
{
    m_workingTree = root;
}

const QString& GitStatusListCtrl::workingTree() const
{
    return m_workingTree;
}

void GitStatusListCtrl::setPathFilter(const QStringList& paths)
{
    m_pathFilter = paths;
    m_pathFilterSet = QSet<QString>(paths.cbegin(), paths.cend());
}

void GitStatusListCtrl::setShowFlags(GitStatusListModel::ShowFlags show, GitStatusListModel::ShowFlags check)
{
    m_model->setShowFlags(show, check);
    const bool loadedBefore = m_statusCall || m_indexDiffCall || m_numStatCall || !m_model->allEntries().isEmpty();
    if (loadedBefore && show.testFlag(GitStatusListModel::Show::Ignored) && !m_hasIgnored) {
        reload();
    }
}

void GitStatusListCtrl::setIncludeStaged(bool include)
{
    m_includeStaged = include;
}

void GitStatusListCtrl::setAmend(bool amend)
{
    m_amend = amend;
}

bool GitStatusListCtrl::isInitialRepo() const
{
    return m_isInitialRepo;
}

void GitStatusListCtrl::setHasCheckboxes(bool hasCheckboxes)
{
    m_model->setHasCheckboxes(hasCheckboxes);
}

void GitStatusListCtrl::setContextMenus(ContextMenus menus)
{
    m_contextMenus = menus;
}

void GitStatusListCtrl::setCurrentVersion(const QString& revision, const QStringList& parents)
{
    m_currentVersion = revision;
    m_parentVersions = parents;
    // PrepareGroups' header for each parent: "Diff with parent 1: <short hash>"
    QStringList headers;
    if (parents.size() > 1) {
        for (qsizetype i = 0; i < parents.size(); ++i) {
            headers.append(i18n("Diff with parent %1", i + 1) + QStringLiteral(": ") + parents.at(i).left(shortHashLength));
        }
    }
    m_model->setParentHeaders(headers);
}

const QString& GitStatusListCtrl::currentVersion() const
{
    return m_currentVersion;
}

void GitStatusListCtrl::updateWithGitPathList(const QList<GitStatusListEntry>& entries)
{
    m_statusCall.reset();
    m_indexDiffCall.reset();
    m_numStatCall.reset();
    m_errorMessage.clear();
    m_isBusy = false;
    m_model->setEntries(entries);
    viewport()->update();
}

void GitStatusListCtrl::clear()
{
    updateWithGitPathList({});
}

void GitStatusListCtrl::setBusy(bool busy)
{
    m_isBusy = busy;
    viewport()->update();
}

void GitStatusListCtrl::setBusyString(const QString& text)
{
    m_busyString = text;
    viewport()->update();
}

void GitStatusListCtrl::reload()
{
    if (!m_currentVersion.isEmpty()) {
        return;
    }
    Q_ASSERT(!m_workingTree.isEmpty());
    if (!m_hasDirectFiles) {
        // CGitStatusListCtrl::GetStatus remembers the files the user picked, so they can be checked
        QStringList directFiles;
        const QDir root(m_workingTree);
        std::ranges::copy_if(m_pathFilter, std::back_inserter(directFiles), [&root](const QString& path) {
            return !QFileInfo(root.filePath(path)).isDir();
        });
        m_model->setDirectFiles(directFiles);
        m_hasDirectFiles = true;
    }
    m_indexDiffCall.reset();
    m_numStatCall.reset();
    m_errorMessage.clear();
    m_hasIgnored = m_model->showFlags().testFlag(GitStatusListModel::Show::Ignored);
    // CGitStatusListCtrl::GetStatus
    m_isRevertTheirMy = AppUtils::isRebaseActive(m_workingTree);

    QStringList arguments = GitStatus::arguments(m_hasIgnored ? IgnoredFiles::Include : IgnoredFiles::Exclude);
    arguments.append(QStringLiteral("--branch"));
    if (const QStringList spec = pathSpec(); !spec.isEmpty()) {
        // A file may be named like a pathspec, such as ":x" or "*"
        arguments.prepend(QStringLiteral("--literal-pathspecs"));
        arguments.append(spec);
    }
    m_statusCall = Git(m_workingTree).runAsync(arguments, RunMode::ReadOnly);
    connect(m_statusCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onStatusFinished);
    viewport()->update();
}

bool GitStatusListCtrl::isBusy() const
{
    return m_isBusy || m_statusCall || m_indexDiffCall || m_numStatCall || isModifying();
}

void GitStatusListCtrl::setSelectButton(QCheckBox *button)
{
    m_selectButton = button;
    if (button) {
        button->setTristate(true);
        connect(button, &QCheckBox::clicked, this, &GitStatusListCtrl::onSelectButtonClicked);
    }
    updateCheckedCount();
}

void GitStatusListCtrl::setStatLabel(QLabel *label)
{
    m_statLabel = label;
    updateCheckedCount();
}

GitStatusListModel *GitStatusListCtrl::statusModel() const
{
    return m_model;
}

QList<GitStatusListEntry> GitStatusListCtrl::checkedEntries() const
{
    return m_model->checkedEntries();
}

bool GitStatusListCtrl::hasConflicts() const
{
    return std::ranges::any_of(m_model->allEntries(), [](const GitStatusListEntry& entry) {
        return (entry.actions & Action::Unmerged) != 0;
    });
}

void GitStatusListCtrl::selectFirstConflict()
{
    clearSelection();
    for (int row = 0; row < m_model->listedCount(); ++row) {
        if (m_model->entry(row).actions & Action::Unmerged) {
            const QModelIndex shown = m_proxy->mapFromSource(m_model->entryIndex(row));
            selectionModel()->select(shown, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            scrollTo(shown);
            setFocus();
            return;
        }
    }
}

bool GitStatusListCtrl::isModifying() const
{
    return m_revertCommand || m_resolveCall || m_resolveCommand || m_isDeleting || m_addCommand;
}

QString GitStatusListCtrl::absolutePath(const GitStatusListEntry& entry) const
{
    return m_workingTree + QLatin1Char('/') + entry.path;
}

void GitStatusListCtrl::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F5 || event->matches(QKeySequence::Refresh)) {
        reload();
        return;
    }
    if (event->key() == Qt::Key_Delete) {
        // CGitStatusListCtrl::OnKeyDown: unlike the menu, not for a missing file
        const GitStatusListEntry *focused = focusedEntry();
        if (focused && (focused->actions & (Action::Unversioned | Action::Ignore))) {
            deleteSelectedFiles();
            return;
        }
    }
    QTreeView::keyPressEvent(event);
}

void GitStatusListCtrl::contextMenuEvent(QContextMenuEvent *event)
{
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty()) {
        return;
    }
    QMenu menu(this);
    addConflictEntries(menu, static_cast<int>(selected.size()));
    addAddEntries(menu);
    QList<GitStatusListEntry> tracked;
    std::copy_if(selected.cbegin(), selected.cend(), std::back_inserter(tracked), isTracked);
    if (!tracked.isEmpty()) {
        QAction *compare = menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Compare with b&ase"), this, [this, tracked] {
            for (const GitStatusListEntry& entry : tracked) {
                startDiff(entry);
            }
        });
        // Edit conflicts stays the default for a single conflicted file
        if (!menu.defaultAction()) {
            menu.setDefaultAction(compare);
        }
        menu.addSeparator();
    }
    // The entry the menu was opened on decides, as TortoiseGit's wcStatus does
    const GitStatusListEntry *focused = focusedEntry();
    if (focused && isTracked(*focused) && m_currentVersion.isEmpty() && m_contextMenus.testFlag(ContextMenu::Revert) && !m_revertCommand) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Re&vert..."), this, [this] {
            // After the menu has closed, so the confirmation isn't nested in it
            QMetaObject::invokeMethod(this, &GitStatusListCtrl::revertSelected, Qt::QueuedConnection);
        });
    }
    // Open and Explore act on the working tree's file, which is not what a commit's list shows
    if (selected.size() == 1 && m_currentVersion.isEmpty()) {
        const GitStatusListEntry& entry = selected.constFirst();
        if (!isGone(entry)) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("&Open"), this, [this, entry] {
                open(entry);
            });
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open with..."), this, [this, entry] {
                openWith(entry);
            });
        }
        if (!(entry.actions & Action::Deleted)) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Explore to"), this, [this, entry] {
                explore(entry);
            });
        }
    }
    if (focused && offersDelete(*focused) && !m_isDeleting) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("&Delete"), this, [this] {
            // After the menu has closed, so the confirmation isn't nested in it
            QMetaObject::invokeMethod(this, &GitStatusListCtrl::deleteSelectedFiles, Qt::QueuedConnection);
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void GitStatusListCtrl::setEmptyString(const QString& text)
{
    m_emptyString = text;
    viewport()->update();
}

void GitStatusListCtrl::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);
    if (m_model->rowCount() > 0) {
        return;
    }
    // TortoiseGit writes the list's state into the empty list (IDS_STATUSLIST_BUSYMSG, IDS_STATUSLIST_EMPTYMSG)
    QString message;
    if (isBusy()) {
        message = m_busyString;
    } else if (!m_errorMessage.isEmpty()) {
        message = m_errorMessage;
    } else {
        message = m_emptyString;
    }
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, message);
}

void GitStatusListCtrl::onStatusFinished()
{
    const GitCall *call = takeFinished(m_statusCall);
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        failLoad(e.error());
        return;
    }
    Result<QList<GitStatusEntry>> status = GitStatus::parse(output);
    if (!status) {
        failLoad(status.error());
        return;
    }
    m_pendingStatus = std::move(*status);
    m_isInitialRepo = output.startsWith(initialBranchHeader);
    if (m_amend && !m_isInitialRepo) {
        // CGit::GetWorkingTreeChanges compares the index with HEAD~1; git status only knows HEAD
        m_indexDiffCall = Git(m_workingTree).runAsync(GitStatus::indexDiffArguments(baseRevision()), RunMode::ReadOnly);
        connect(m_indexDiffCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onIndexDiffFinished);
        return;
    }
    loadNumStat();
}

void GitStatusListCtrl::onIndexDiffFinished()
{
    const GitCall *call = takeFinished(m_indexDiffCall);
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        failLoad(e.error());
        return;
    }
    Result<QList<GitStatusEntry>> status = GitStatus::withIndexDiff(m_pendingStatus, output);
    if (!status) {
        failLoad(status.error());
        return;
    }
    m_pendingStatus = std::move(*status);
    loadNumStat();
}

void GitStatusListCtrl::loadNumStat()
{
    if (!isPathFilterGivenToGit()) {
        // The status covered the whole working tree: keep what the filter selects, and everything staged if asked
        m_pendingStatus.removeIf([this](const GitStatusEntry& entry) {
            return !isInPathFilter(entry) && !(m_includeStaged && entry.index != FileState::Unmodified);
        });
    }
    if (m_isInitialRepo) {
        // Nothing to count lines against; TortoiseGit lists an initial commit's files without counts too
        finishLoad({});
        return;
    }
    const QStringList numStatPaths = isPathFilterGivenToGit() ? m_pathFilter : QStringList();
    m_numStatCall = Git(m_workingTree).runAsync(GitNumStat::arguments(baseRevision(), numStatPaths), RunMode::ReadOnly);
    connect(m_numStatCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onNumStatFinished);
}

void GitStatusListCtrl::onNumStatFinished()
{
    const GitCall *call = takeFinished(m_numStatCall);
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        failLoad(e.error());
        return;
    }
    Result<QList<NumStatEntry>> numStat = GitNumStat::parse(output);
    if (!numStat) {
        failLoad(numStat.error());
        return;
    }
    finishLoad(*numStat);
}

void GitStatusListCtrl::finishLoad(const QList<NumStatEntry>& numStat)
{
    m_model->setEntries(GitStatusListModel::makeEntries(m_pendingStatus, numStat));
    m_pendingStatus.clear();
    viewport()->update();
    Q_EMIT loaded();
}

void GitStatusListCtrl::failLoad(const Error& error)
{
    m_pendingStatus.clear();
    m_model->setEntries({});
    m_errorMessage = error.message;
    viewport()->update();
    Q_EMIT loadFailed(error);
}

void GitStatusListCtrl::updateCheckedCount()
{
    const int checked = m_model->checkedCount();
    const int total = m_model->listedCount();
    if (m_statLabel) {
        m_statLabel->setText(i18n("%1 files selected, %2 files total", checked, total));
    }
    if (m_selectButton) {
        Qt::CheckState state = Qt::PartiallyChecked;
        if (checked == 0) {
            state = Qt::Unchecked;
        } else if (checked == total) {
            state = Qt::Checked;
        }
        m_selectButton->setCheckState(state);
    }
    Q_EMIT checkedCountChanged();
}

void GitStatusListCtrl::onSelectButtonClicked()
{
    /**
     * Windows cycles a three-state box from checked to indeterminate to unchecked, and TortoiseGit forces
     * indeterminate on to unchecked (CAddDlg::OnBnClickedSelectall): so a click unchecks everything if anything is
     * checked, and checks everything otherwise.
     */
    m_model->setAllChecked(m_model->checkedCount() == 0);
}

void GitStatusListCtrl::showGroups()
{
    // Group headers span the row, and every group is open
    if (!m_model->hasGroups()) {
        return;
    }
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        setFirstColumnSpanned(row, {}, true);
    }
    expandAll();
}

void GitStatusListCtrl::onDoubleClicked(const QModelIndex& index)
{
    const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(index));
    if (!entry) {
        return;
    }
    if (!isTracked(*entry)) {
        // CGitStatusListCtrl::StartDiff compares an unversioned file with nothing
        if (!QFileInfo(absolutePath(*entry)).isDir()) {
            std::ignore = GitDiff::start(this, {.workingDirectory = m_workingTree, .arguments = GitDiff::diffNullArguments(entry->path)});
        }
        return;
    }
    if (entry->actions & Action::Unmerged) {
        editConflict(*entry);
        return;
    }
    startDiff(*entry);
}

void GitStatusListCtrl::addConflictEntries(QMenu& menu, int selectedCount)
{
    // The entry the menu was opened on decides, as TortoiseGit's wcStatus does
    const GitStatusListEntry *focused = focusedEntry();
    if (!focused || !(focused->actions & Action::Unmerged) || !m_currentVersion.isEmpty()) {
        return;
    }
    if (selectedCount == 1 && m_contextMenus.testFlag(ContextMenu::Conflict)) {
        const GitStatusListEntry entry = *focused;
        QAction *edit = menu.addAction(QIcon::fromTheme(QStringLiteral("merge")), i18n("&Edit conflicts"), this, [this, entry] {
            editConflict(entry);
        });
        menu.setDefaultAction(edit);
    }
    if (m_contextMenus.testFlag(ContextMenu::Resolve) && !m_resolveCall && !m_resolveCommand) {
        addResolveEntries(menu);
    }
    if (m_contextMenus & (ContextMenu::Conflict | ContextMenu::Resolve)) {
        menu.addSeparator();
    }
}

void GitStatusListCtrl::addResolveEntries(QMenu& menu)
{
    const QIcon icon = QIcon::fromTheme(QStringLiteral("dialog-ok-apply"));
    menu.addAction(icon, i18n("Resolved"), this, [this] {
        // After the menu has closed, so the confirmation isn't nested in it
        QMetaObject::invokeMethod(this, &GitStatusListCtrl::resolveSelected, Qt::QueuedConnection);
    });
    const QList<ResolveUsingEntry> entries = resolveUsingEntries();
    for (const auto& [title, resolveWith] : entries) {
        // A branch may have & in its name, which a menu would take for a shortcut
        QString escaped = title;
        escaped.replace(QLatin1Char('&'), QStringLiteral("&&"));
        menu.addAction(icon, i18n("Resolve conflict using \"%1\"", escaped), this, [this, resolveWith] {
            // After the menu has closed, so the confirmation isn't nested in it
            QMetaObject::invokeMethod(
                this,
                [this, resolveWith] {
                    resolveSelectedUsing(resolveWith);
                },
                Qt::QueuedConnection);
        });
    }
}

void GitStatusListCtrl::addAddEntries(QMenu& menu)
{
    // CGitStatusListCtrl::OnContextMenuList: the entry the menu was opened on decides
    const GitStatusListEntry *focused = focusedEntry();
    if (!focused || !(focused->actions & (Action::Unversioned | Action::Ignore)) || !m_contextMenus.testFlag(ContextMenu::Add) || m_addCommand) {
        return;
    }
    const auto addEntry = [this, &menu](const QString& text, AddProgressCommand::FileMode fileMode) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")), text, this, [this, fileMode] {
            // After the menu has closed, so the progress dialog isn't nested in it
            QMetaObject::invokeMethod(
                this,
                [this, fileMode] {
                    addSelected(fileMode);
                },
                Qt::QueuedConnection);
        });
    };
    addEntry(i18n("Add"), AddProgressCommand::FileMode::Unchanged);
    if (QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier) && !QFileInfo(absolutePath(*focused)).isDir()) {
        addEntry(i18n("Add as Executable (+x)"), AddProgressCommand::FileMode::Executable);
        addEntry(i18n("Add as Symlink"), AddProgressCommand::FileMode::Symlink);
    }
}

void GitStatusListCtrl::addSelected(AddProgressCommand::FileMode fileMode)
{
    // The IDGITLC_ADD handler of CGitStatusListCtrl::OnContextMenuList
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty() || m_addCommand) {
        return;
    }
    QStringList paths;
    paths.reserve(selected.size());
    for (const GitStatusListEntry& entry : selected) {
        paths.append(entry.path);
    }
    m_addCommand = std::make_unique<AddProgressCommand>(fileMode);
    m_addCommand->setPathList(paths);
    connect(m_addCommand.get(), &AddProgressCommand::finished, this, &GitStatusListCtrl::onAddFinished);
    Q_EMIT checkedCountChanged();
    m_addCommand->run(m_workingTree, this);
}

void GitStatusListCtrl::onAddFinished()
{
    // Deleted from the event loop: it is still emitting
    AddProgressCommand *command = m_addCommand.release();
    command->deleteLater();
    if (const std::optional<Error>& error = command->error()) {
        showError(this, error->message);
    }
    // Reset the unchecked status, so the files take the default for their new status, and RefreshParent
    m_model->resetCheckStates(command->pathList());
    reload();
}

void GitStatusListCtrl::editConflict(const GitStatusListEntry& entry)
{
    // After the menu or double click, so the Conflict dialog isn't nested in it and the entry can't change under it
    QMetaObject::invokeMethod(
        this,
        [this, path = entry.path] {
            // IDGITLC_EDITCONFLICT in CGitStatusListCtrl::OnContextMenuList
            if (AppUtils::conflictEdit(Git(m_workingTree), path, this)) {
                reload();
            }
        },
        Qt::QueuedConnection);
}

void GitStatusListCtrl::resolveSelected()
{
    if (m_resolveCall || m_resolveCommand) {
        return;
    }
    QStringList paths;
    for (const GitStatusListEntry& entry : selectedEntries()) {
        if (entry.actions & Action::Unmerged) {
            paths.append(entry.path);
        }
    }
    if (paths.isEmpty() || !confirmResolve()) {
        return;
    }
    // ResolveProgressCommand: git add -f on the conflicted files (CAppUtils::RemoveTempMergeFile has nothing to remove here)
    MassiveGitTask addTask({QStringLiteral("add"), QStringLiteral("-f")});
    addTask.setPaths(paths);
    m_resolveCall = addTask.runAsync(Git(m_workingTree));
    connect(m_resolveCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onResolveFinished);
    setCursor(Qt::BusyCursor);
}

void GitStatusListCtrl::onResolveFinished()
{
    const GitCall *call = takeFinished(m_resolveCall);
    unsetCursor();
    try {
        std::ignore = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        showError(this, e.error().message);
    }
    // TortoiseGit marks the entries modified, or refreshes the parent after an error; reloading does both
    reload();
    Q_EMIT resolved();
}

bool GitStatusListCtrl::confirmResolve()
{
    // IDS_PROC_RESOLVE
    const QString question = i18n("Are you sure you want to mark the conflicted file(s) as resolved?");
    const QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No;
    return QMessageBox::question(this, i18n("KortoiseGit"), question, buttons) == QMessageBox::Yes;
}

QList<GitStatusListCtrl::ResolveUsingEntry> GitStatusListCtrl::resolveUsingEntries()
{
    ConflictTitles titles;
    try {
        titles = AppUtils::getConflictTitles(Git(m_workingTree), m_isRevertTheirMy);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        showError(this, e.error().message);
        return {};
    }
    // During a rebase CGitStatusListCtrl::OnContextMenuList swaps the two entries' command ids and its handler swaps
    // them back, so each title always gives its own stage
    return {
        {.title = titles.theirsText, .resolveWith = ResolveWith::Theirs},
        {.title = titles.mineText, .resolveWith = ResolveWith::Mine},
    };
}

void GitStatusListCtrl::resolveSelectedUsing(ResolveWith resolveWith)
{
    // The IDGITLC_RESOLVETHEIRS and IDGITLC_RESOLVEMINE handler of CGitStatusListCtrl::OnContextMenuList
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty() || m_resolveCall || m_resolveCommand || !confirmResolve()) {
        return;
    }
    m_resolveCommand = std::make_unique<ResolveProgressCommand>(resolveWith);
    m_resolveCommand->setPathList(selected);
    const ResolveProgressCommand *command = m_resolveCommand.get();
    connect(command, &ResolveProgressCommand::conflictInfoRead, this, &GitStatusListCtrl::onConflictInfoRead);
    setCursor(Qt::BusyCursor);
    Q_EMIT checkedCountChanged();
    m_resolveCommand->readConflictInfo(m_workingTree);
}

void GitStatusListCtrl::onConflictInfoRead()
{
    // Deleted from the event loop: it is still emitting
    ResolveProgressCommand *command = m_resolveCommand.release();
    command->deleteLater();
    unsetCursor();
    if (const std::optional<Error>& error = command->error()) {
        showError(this, error->message);
    } else {
        ProgressDlg dialog(m_workingTree, this);
        command->setUp(m_workingTree, dialog);
        // AUTOCLOSE_IF_NO_ERRORS. Queued: with every file skipped the commands end while the dialog is being shown
        connect(
            &dialog,
            &ProgressDlg::commandsFinished,
            &dialog,
            [&dialog] {
                if (dialog.gitStatus() == 0) {
                    dialog.accept();
                }
            },
            Qt::QueuedConnection);
        dialog.exec();
    }
    // RefreshParent
    reload();
    Q_EMIT resolved();
}

const GitStatusListEntry *GitStatusListCtrl::focusedEntry() const
{
    // CGitStatusListCtrl::OnContextMenuList's selIndex: the selection mark, or else the first selected entry
    const QModelIndex current = currentIndex();
    if (current.isValid() && selectionModel()->isRowSelected(current.row(), current.parent())) {
        if (const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(current))) {
            return entry;
        }
    }
    const QModelIndexList rows = selectionModel()->selectedRows();
    return rows.isEmpty() ? nullptr : m_model->entry(m_proxy->mapToSource(rows.constFirst()));
}

QList<GitStatusListEntry> GitStatusListCtrl::selectedEntries() const
{
    QList<GitStatusListEntry> entries;
    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& row : rows) {
        if (const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(row))) {
            entries.append(*entry);
        }
    }
    return entries;
}

void GitStatusListCtrl::startDiff(const GitStatusListEntry& entry)
{
    // Ported from CGitStatusListCtrl::StartDiff: a rename or copy is compared with its source
    const QString path2 = entry.origPath.isEmpty() ? entry.path : entry.origPath;
    QStringList arguments;
    if (!m_currentVersion.isEmpty()) {
        const QString parent = parentRevision(entry.parentNo);
        if (parent.isEmpty()) {
            QMessageBox::warning(this, i18n("KortoiseGit"), i18n("Could not start git difftool."));
            return;
        }
        arguments = GitDiff::diffArguments(entry.path, path2, m_currentVersion, parent);
    } else if (m_isInitialRepo || entry.actions.testFlag(TGitPath::Action::Added)) {
        arguments = GitDiff::diffNullArguments(entry.path);
    } else {
        arguments = GitDiff::diffArguments(entry.path, path2, {}, baseRevision());
    }
    std::ignore = GitDiff::start(this, {.workingDirectory = m_workingTree, .arguments = arguments});
}

QString GitStatusListCtrl::parentRevision(int parentNo) const
{
    // The parent the change is against (m_CurrentVersion^N in CGitStatusListCtrl::StartDiff)
    const QString parent = m_parentVersions.value(parentNo);
    if (!parent.isEmpty()) {
        return parent;
    }
    // A root commit is compared with the empty tree, whose hash depends on the repository's hash algorithm
    try {
        return QString::fromLatin1(
            Git(m_workingTree)
                .run({QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"), QStringLiteral("--stdin")}, RunMode::ReadOnly)
                .trimmed());
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

void GitStatusListCtrl::open(const GitStatusListEntry& entry)
{
    // KIO jobs delete themselves when they finish
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(absolutePath(entry)));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

void GitStatusListCtrl::openWith(const GitStatusListEntry& entry)
{
    // Without a service, the job asks which application to use
    auto *job = new KIO::ApplicationLauncherJob();
    job->setUrls({QUrl::fromLocalFile(absolutePath(entry))});
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

void GitStatusListCtrl::explore(const GitStatusListEntry& entry) const
{
    KIO::highlightInFileManager({QUrl::fromLocalFile(absolutePath(entry))});
}

void GitStatusListCtrl::revertSelected()
{
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty() || m_revertCommand) {
        return;
    }
    // If at least one item is a modified file, ask first (IDGITLC_REVERT)
    const bool confirm = std::ranges::any_of(selected, [this](const GitStatusListEntry& entry) {
        return (entry.actions & Action::Modified) && !QFileInfo(absolutePath(entry)).isDir();
    });
    if (confirm) {
        const QString question = i18n("Are you sure you want to revert %1 item(s)? You will lose ALL changes since the last update!", selected.size());
        if (QMessageBox::question(this, i18n("KortoiseGit"), question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }
    // While amending, files go back to HEAD~1 (IDGITLC_REVERT's revertToCommit)
    m_revertCommand = std::make_unique<RevertProgressCommand>(baseRevision());
    m_revertCommand->setPathList(selected);
    connect(m_revertCommand.get(), &RevertProgressCommand::finished, this, &GitStatusListCtrl::onRevertFinished);
    setCursor(Qt::BusyCursor);
    Q_EMIT checkedCountChanged();
    m_revertCommand->run(m_workingTree);
}

void GitStatusListCtrl::onRevertFinished()
{
    // Deleted from the event loop: it is still emitting, maybe from inside run()
    RevertProgressCommand *command = m_revertCommand.release();
    command->deleteLater();
    unsetCursor();
    if (const std::optional<Error>& error = command->error()) {
        showError(this, error->message);
    }
    // TortoiseGit drops the reverted entries or refreshes the parent dialog; reloading does both
    reload();
    Q_EMIT reverted();
}

void GitStatusListCtrl::deleteSelectedFiles()
{
    // Ported from CGitStatusListCtrl::DeleteSelectedFiles
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty() || m_isDeleting) {
        return;
    }
    // GetAsyncKeyState(VK_SHIFT), read now rather than when the menu was opened
    const bool permanently = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
    m_deletingPaths.clear();
    QList<QUrl> urls;
    for (const GitStatusListEntry& entry : selected) {
        m_deletingPaths.append(entry.path);
        // A dangling symlink is still there to delete, though exists() follows it
        const QFileInfo file(absolutePath(entry));
        if (file.exists() || file.isSymLink()) {
            urls.append(QUrl::fromLocalFile(file.filePath()));
        }
    }
    m_isDeleting = true;
    Q_EMIT checkedCountChanged();
    if (urls.isEmpty()) {
        // Nothing on disk to confirm or delete, as SHFileOperation's empty list
        removeDeletedFromIndex();
        return;
    }
    using AskUser = KIO::AskUserActionInterface;
    // SHFileOperation always asks; KIO's default confirmation doesn't for Trash unless the user turned that on
    auto *job = new KIO::DeleteOrTrashJob(urls, permanently ? AskUser::Delete : AskUser::Trash, AskUser::ForceConfirmation, this);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    connect(job, &KJob::result, this, &GitStatusListCtrl::onDeleteJobResult);
    job->start();
}

void GitStatusListCtrl::onDeleteJobResult(const KJob *job)
{
    if (job->error() != KJob::NoError) {
        // Cancelled or failed: the index and the list are left alone, and the job's delegate has shown any error
        m_deletingPaths.clear();
        m_isDeleting = false;
        Q_EMIT checkedCountChanged();
        return;
    }
    removeDeletedFromIndex();
}

void GitStatusListCtrl::removeDeletedFromIndex()
{
    /**
     * git_index_remove_bypath for each path. --ignore-unmatch because an unversioned file won't be there, -r because
     * an ignored folder is one entry, --force because a file may have staged changes.
     */
    MassiveGitTask removeTask(
        {QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("--force"), QStringLiteral("--ignore-unmatch")});
    removeTask.setPaths(m_deletingPaths);
    m_deleteCall = removeTask.runAsync(Git(m_workingTree));
    connect(m_deleteCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onDeleteIndexFinished);
}

void GitStatusListCtrl::onDeleteIndexFinished()
{
    const GitCall *call = takeFinished(m_deleteCall);
    bool indexChanged = true; // After an error, TortoiseGit refreshes too
    try {
        // git rm prints a line for each path it removes (needWriteIndex)
        indexChanged = !call->output().isEmpty();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        showError(this, e.error().message);
    }
    const QSet<QString> deleted(m_deletingPaths.cbegin(), m_deletingPaths.cend());
    m_deletingPaths.clear();
    m_isDeleting = false;
    Q_EMIT checkedCountChanged();
    // A load started before the files went would bring their rows back, so it is restarted
    if (indexChanged || m_statusCall || m_indexDiffCall || m_numStatCall) {
        reload();
        return;
    }
    QList<GitStatusListEntry> remaining = m_model->allEntries();
    remaining.removeIf([&deleted](const GitStatusListEntry& entry) {
        return deleted.contains(entry.path);
    });
    m_model->setEntries(remaining);
}

bool GitStatusListCtrl::isPathFilterGivenToGit() const
{
    if (m_includeStaged) {
        return false;
    }
    // CGit::GetWorkingTreeChanges asks once per path; here a selection that won't fit on a command line is filtered
    // afterwards
    qsizetype length = 0;
    for (const QString& path : m_pathFilter) {
        length += path.toUtf8().size() + 1;
    }
    return length <= maxPathSpecLength;
}

QStringList GitStatusListCtrl::pathSpec() const
{
    if (m_pathFilter.isEmpty() || !isPathFilterGivenToGit()) {
        return {};
    }
    return QStringList{QStringLiteral("--")} + m_pathFilter;
}

QString GitStatusListCtrl::baseRevision() const
{
    return m_amend ? QStringLiteral("HEAD~1") : QStringLiteral("HEAD");
}

bool GitStatusListCtrl::isInPathFilter(const GitStatusEntry& entry) const
{
    if (m_pathFilter.isEmpty()) {
        return true;
    }
    if (m_pathFilterSet.contains(QStringLiteral("."))) {
        return true;
    }
    // A set and a walk up the path, as a selection may hold thousands of paths
    const auto isUnder = [this](const QString& path) {
        for (qsizetype end = path.size(); end > 0; end = path.lastIndexOf(QLatin1Char('/'), end - 1)) {
            if (m_pathFilterSet.contains(path.left(end))) {
                return true;
            }
        }
        return false;
    };
    return isUnder(entry.path) || isUnder(entry.origPath);
}

} // namespace kgt
