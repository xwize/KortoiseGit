// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp (CGitLogListBase::OnContextMenu) and
 * GitLogListAction.cpp (CGitLogListBase::ContextMenuAction): the log's right-click menu. Entries are added in
 * OnContextMenu's order, each section ending with its separators, so the entries not ported yet can slot in. Also
 * CGitLogListBase::CopySelectionToClipBoard, which waits for what TortoiseGit reads synchronously, and
 * CGitLogList::RevertSelectedCommits.
 */

#include "gitloglist.hpp"

#include "apputils.hpp"
#include "commands/mergecommand.hpp"
#include "createbranchtagdlg.hpp"
#include "filediffdlg.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "git/gitrev.hpp"
#include "gitdiff.hpp"
#include "gitstatuslistmodel.hpp"
#include "rebasedlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace
{

using namespace kgt;

// CGit::GetShortHASHLength
constexpr qsizetype shortHashLength = 8;

// How much of a parent's subject its menu entry shows
constexpr qsizetype parentSubjectLength = 20;

/**
 * @param rev A commit.
 * @returns Its refs as CGit::GetMapHashToFriendName lists them: full names, sorted, and no detached HEAD.
 */
QStringList sortedRefNames(const GitRevLoglist& rev)
{
    QStringList names;
    for (const GitRef& ref : rev.refs) {
        if (ref.type != GitRef::Type::Head) {
            names.append(ref.fullName());
        }
    }
    std::ranges::sort(names);
    return names;
}

/**
 * Ported from GetFirstEntryStartingWith.
 * @param refs Full ref names, sorted.
 * @param prefix A prefix such as refs/heads/.
 * @returns The first that starts with it, or an empty string.
 */
QString firstWithPrefix(const QStringList& refs, const QString& prefix)
{
    const auto found = std::ranges::find_if(refs, [&prefix](const QString& ref) {
        return ref.startsWith(prefix);
    });
    return found == refs.cend() ? QString() : *found;
}

/**
 * @param text A ref name for a menu entry.
 * @returns It with any & kept literal, not taken as a mnemonic.
 */
QString menuText(QString text)
{
    return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

/**
 * Ported from CTGitPathList::LookForGitPath and CTGitPath::GetGitOldPathString. Files that couldn't be fetched
 * count as no rename, as an empty GetFiles does.
 * @param rev A commit.
 * @param path A file's name after the commit.
 * @returns The name it had before the commit.
 */
QString oldPathIn(const GitRevLoglist& rev, const QString& path)
{
    for (const GitRevFiles& files : rev.files) {
        for (const GitStatusEntry& entry : files.status) {
            if (entry.path == path) {
                return entry.origPath.isEmpty() ? path : entry.origPath;
            }
        }
    }
    return path;
}

/**
 * Runs an action of the menu, showing a git failure from opening its dialog: nothing above a slot can catch it.
 * @param parent Parent of the message.
 * @param action The action.
 */
void showingGitErrors(QWidget *parent, const std::function<void()>& action)
{
    try {
        action();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(parent, e.error().message, QString(), KMessageBox::Options());
    }
}

/**
 * @param rev A commit.
 * @param tagInfo The annotated tags' text by full ref name.
 * @returns Its Notes and Tag Info sections, each with its "----" line; empty if it has neither.
 */
QString notesAndTagsText(const GitRevLoglist& rev, const QHash<QString, QString>& tagInfo)
{
    QString text;
    if (!rev.notes.isEmpty()) {
        // TortoiseGit's m_Notes keeps the note's final newline
        text = QStringLiteral("----\n") + i18n("Notes") + QStringLiteral(":\n") + rev.notes + QLatin1Char('\n');
    }
    // CGitLogListBase::GetTagInfo
    QString tags;
    for (const QString& ref : sortedRefNames(rev)) {
        tags += tagInfo.value(ref);
    }
    if (!tags.isEmpty()) {
        text += QStringLiteral("----\n") + i18n("Tag Info") + QStringLiteral(":\n") + tags;
    }
    return text;
}

/**
 * @param rev A commit, with its files loaded.
 * @returns The "----" line and one line per changed path, against every parent.
 */
QString changedPathsText(const GitRevLoglist& rev)
{
    const TGitPath::Actions movedFrom = TGitPath::Action::Replaced | TGitPath::Action::Copy;
    QString text = QStringLiteral("----");
    for (const GitRevFiles& diff : rev.files) {
        for (const GitStatusEntry& entry : diff.status) {
            const QString action = GitStatusListModel::actionName(entry.actions);
            text += QLatin1Char('\n') + action + QStringLiteral(": ") + entry.path;
            if ((entry.actions & movedFrom) && !entry.origPath.isEmpty()) {
                text += QLatin1Char(' ') + i18n("(from %1)", entry.origPath);
            }
        }
    }
    return text + QLatin1Char('\n');
}

/**
 * @param rev A commit.
 * @param tagInfo The annotated tags' text by full ref name.
 * @param withPaths Whether to add the changed paths.
 * @returns The commit's text for the two Full formats, ending with a blank line.
 */
QString fullCommitText(const GitRevLoglist& rev, const QHash<QString, QString>& tagInfo, bool withPaths)
{
    const QString author = rev.authorName + QStringLiteral(" <") + rev.authorEmail + QLatin1Char('>');
    QString text = i18n("Revision") + QStringLiteral(": ") + rev.hash + QLatin1Char('\n');
    text += i18n("Author") + QStringLiteral(": ") + author + QLatin1Char('\n');
    text += i18n("Date") + QStringLiteral(": ") + GitLogListModel::formatDate(rev.authorDate) + QLatin1Char('\n');
    text += i18n("Message") + QStringLiteral(":\n") + rev.subjectBody() + QLatin1Char('\n');
    text += notesAndTagsText(rev, tagInfo);
    if (withPaths) {
        text += changedPathsText(rev);
    }
    return text + QLatin1Char('\n');
}

/**
 * Ported from CGit::GitRevert: reverts a commit into the index and working tree, without committing.
 * @param git Runs git in the working tree.
 * @param parent The parent number to revert against for a merge (-m), or 0.
 * @param hash The commit.
 * @returns Nothing, or the message of git's failure.
 */
std::optional<QString> gitRevert(const Git& git, int parent, const QString& hash)
{
    QStringList arguments{QStringLiteral("revert"), QStringLiteral("--no-edit"), QStringLiteral("--no-commit")};
    if (parent != 0) {
        arguments << QStringLiteral("-m") << QString::number(parent);
    }
    arguments.append(hash);
    try {
        std::ignore = git.run(arguments);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return e.error().message;
    }
    return std::nullopt;
}

/**
 * Reports a failed revert as CGitLogList::RevertSelectedCommits does.
 * @param parent Parent of the message.
 * @param failure git's message.
 * @param single Whether it was the only commit to revert: then there is nothing to skip to.
 * @returns Whether to go on with the next commit (Skip) rather than stop (Abort).
 */
bool continueAfterFailedRevert(QWidget *parent, const QString& failure, bool single)
{
    const QString message = i18n("Failed revert") + QLatin1Char('\n') + failure;
    if (single) {
        KMessageBox::error(parent, message, QString(), KMessageBox::Options());
        return true;
    }
    const KGuiItem skip(i18n("&Skip"));
    const KGuiItem abort(i18n("A&bort"));
    return KMessageBox::warningTwoActions(parent, message, QString(), skip, abort) == KMessageBox::PrimaryAction;
}

} // namespace

namespace kgt
{

void GitLogList::showLogMenu(QContextMenuEvent *event)
{
    // CGitLogListBase::OnContextMenu
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    const bool fromKeyboard = event->reason() == QContextMenuEvent::Keyboard;
    const QModelIndex first = m_model->index(rows.constFirst(), GitLogListModel::MessageColumn);
    const QPoint anchor = fromKeyboard ? viewport()->mapToGlobal(visualRect(first).center()) : event->globalPos();

    QMenu menu(this);
    std::optional<LogMenuTarget> target;
    const QString headHash = Git(m_workingTree).hash(QStringLiteral("HEAD"));
    if (headHash.isEmpty()) {
        KMessageBox::error(this, i18n("Could not get HEAD hash."), QString(), KMessageBox::Options());
        return;
    }
    const bool hasWC = !GitAdminDir::isBareRepo(m_workingTree); // m_hasWC
    const bool mergeActive = isMergeActive();
    if (rows.size() == 1) {
        const int row = rows.constFirst();
        const GitRevLoglist& rev = m_model->rev(row);
        const QStringList refs = sortedRefNames(rev);
        const std::optional<GitRef> label = fromKeyboard ? std::nullopt : refLabelAt(row, event->pos());
        target = LogMenuTarget{
            .hash = rev.hash,
            .refs = refs,
            .label = label,
            .isStash = isOnStash(row),
            .row = row,
            .parents = parentHashes(rev),
            .isHead = rev.hash == headHash,
        };
        addSingleCommitEntries(menu, *target);
    } else if (hasWC && !mergeActive) {
        const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-undo"));
        menu.addAction(icon, i18n("&Revert changes by these commits"), this, [this] {
            revertRev(0);
        });
    }

    // The Combine, Cherry-pick and Create Patch section, whose separator also ends the Revert entry above it.
    // isHeadCommit is about the first selected commit.
    bool addSeparator = rows.size() > 1 && hasWC && !mergeActive;
    const bool isHeadCommit = m_model->rev(rows.constFirst()).hash == headHash;
    if (!isHeadCommit && hasWC && !mergeActive) {
        addSeparator = true;
        const QString text = rows.size() >= 2 ? i18n("Cherr&y Pick selected commits...") : i18n("Cherr&y Pick this commit...");
        menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-cherry-pick"), QIcon::fromTheme(QStringLiteral("edit-paste"))), text, this, [this] {
            showingGitErrors(this, [this] {
                cherryPick();
            });
        });
    }
    if (addSeparator) {
        menu.addSeparator();
    }
    addClipboardEntries(menu, target);
    menu.exec(anchor);
}

bool GitLogList::isOnStash(int row) const
{
    // CGitLogListBase::IsOnStash: the stash commit, or the commit holding its index, listed right below it
    const auto isStash = [](const GitRevLoglist& rev) {
        return std::ranges::any_of(rev.refs, [](const GitRef& ref) {
            return ref.type == GitRef::Type::Stash;
        });
    };
    const GitRevLoglist& rev = m_model->rev(row);
    if (isStash(rev)) {
        return true;
    }
    if (row == 0) {
        return false;
    }
    const GitRevLoglist& previous = m_model->rev(row - 1);
    return isStash(previous) && previous.parents.size() == 2 && previous.parents.at(1) == rev.hash;
}

void GitLogList::addSingleCommitEntries(QMenu& menu, const LogMenuTarget& target)
{
    addCompareEntries(menu, target);

    const bool hasWC = !GitAdminDir::isBareRepo(m_workingTree); // m_hasWC: CLogDlg sets it for anything but a bare repository
    const bool mergeActive = isMergeActive();
    const QString currentBranch = menuText(Git(m_workingTree).currentBranch());
    const auto addEntry = [this, &menu, &target](const QString& iconName, const QString& text, void (GitLogList::*handler)(const LogMenuTarget&)) {
        menu.addAction(QIcon::fromTheme(iconName), text, this, [this, target, handler] {
            showingGitErrors(this, [this, &target, handler] {
                (this->*handler)(target);
            });
        });
    };

    if (!target.isHead && hasWC && !mergeActive && !target.isStash) {
        addEntry(QStringLiteral("vcs-merge"), i18n("&Merge to \"%1\"...", currentBranch), &GitLogList::mergeRev);
    }

    if (hasWC && !target.isStash) {
        addEntry(QStringLiteral("edit-reset"), i18n("R&eset \"%1\" to this...", currentBranch), &GitLogList::resetToCommit);
        addSwitchBranchEntry(menu, target);
    }

    if (!target.isHead && hasWC && !target.isStash) {
        addEntry(QStringLiteral("vcs-branch"), i18n("Sw&itch/Checkout to this..."), &GitLogList::switchToRev);
    }

    if (!target.isStash) {
        addEntry(QStringLiteral("vcs-branch"), i18n("Create Br&anch at this version..."), &GitLogList::createBranch);
        addEntry(QStringLiteral("tag"), i18n("Create &Tag at this version..."), &GitLogList::createTag);
    }

    if (!target.isHead && hasWC && !mergeActive && !target.isStash) {
        addEntry(QStringLiteral("vcs-rebase"), i18n("Rebase \"%1\" onto this...(&G)", currentBranch), &GitLogList::rebaseToVersion);
    }

    if (hasWC && !mergeActive && !target.isStash) {
        addRevertEntry(menu, target);
    }
    menu.addSeparator();

    const bool hasPush = addPushEntry(menu, target);
    const bool hasDelete = addDeleteEntries(menu, target);
    if (hasPush || hasDelete) {
        menu.addSeparator();
    }
}

void GitLogList::addSwitchBranchEntry(QMenu& menu, const LogMenuTarget& target)
{
    // The Switch Branch express menu of CGitLogListBase::OnContextMenu: the local branches on the commit but the
    // current one, or only the right-clicked one
    const QString headsPrefix = QStringLiteral("refs/heads/");
    const QString currentBranch = headsPrefix + Git(m_workingTree).currentBranch();
    QStringList branches;
    const QStringList candidates = target.label ? QStringList{target.label->fullName()} : target.refs;
    for (const QString& ref : candidates) {
        if (ref.startsWith(headsPrefix) && ref != currentBranch) {
            branches.append(ref.mid(headsPrefix.size()));
        }
    }

    const QIcon icon = QIcon::fromTheme(QStringLiteral("vcs-branch"));
    const QString text = i18n("&Switch/Checkout to");
    const auto switchTo = [this](const QString& branch) {
        return [this, branch] {
            showingGitErrors(this, [this, &branch] {
                switchBranch(branch);
            });
        };
    };
    if (branches.size() == 1) {
        const QString& branch = branches.constFirst();
        menu.addAction(icon, text + QStringLiteral(" \"") + menuText(branch) + QLatin1Char('"'), this, switchTo(branch));
    } else if (branches.size() > 1) {
        QMenu *submenu = menu.addMenu(icon, text);
        for (const QString& branch : std::as_const(branches)) {
            submenu->addAction(menuText(branch), this, switchTo(branch));
        }
    }
}

void GitLogList::addRevertEntry(QMenu& menu, const LogMenuTarget& target)
{
    // ID_REVERTREV of CGitLogListBase::OnContextMenu: a merge reverts against the parent chosen in a submenu
    const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-undo"));
    const QString text = i18n("&Revert change by this commit");
    if (target.parents.size() == 1) {
        menu.addAction(icon, text, this, [this] {
            revertRev(0);
        });
    } else if (target.parents.size() > 1) {
        QMenu *submenu = menu.addMenu(icon, text);
        for (qsizetype i = 0; i < target.parents.size(); ++i) {
            const int parent = static_cast<int>(i) + 1;
            submenu->addAction(parentInfo(parent, target.parents.at(i)), this, [this, parent] {
                revertRev(parent);
            });
        }
    }
}

bool GitLogList::isMergeActive() const
{
    // CTGitPath::IsMergeActive
    return QFileInfo::exists(QDir(GitAdminDir::worktreeAdminDirPath(m_workingTree)).filePath(QStringLiteral("MERGE_HEAD")));
}

void GitLogList::addCompareEntries(QMenu& menu, const LogMenuTarget& target)
{
    const QIcon icon = QIcon::fromTheme(QStringLiteral("vcs-diff"));
    const int row = target.row;
    bool requiresSeparator = false;
    if (!GitAdminDir::isBareRepo(m_workingTree)) { // m_hasWC
        menu.addAction(icon, i18n("Compare with &working tree"), this, [this, row] {
            compareWithWorkingTree(row);
        });
        requiresSeparator = true;
    }

    const QString previous = i18n("&Compare with previous revision");
    if (target.parents.size() == 1) {
        menu.addAction(icon, previous, this, [this, row] {
            compareWithPrevious(row, 0);
        });
        requiresSeparator = true;
    } else if (target.parents.size() > 1) {
        QMenu *submenu = menu.addMenu(icon, previous);
        for (qsizetype i = 0; i < target.parents.size(); ++i) {
            submenu->addAction(parentInfo(i + 1, target.parents.at(i)), this, [this, row, i] {
                compareWithPrevious(row, i);
            });
        }
        requiresSeparator = true;
    }

    if (requiresSeparator) {
        menu.addSeparator();
    }
}

QStringList GitLogList::parentHashes(const GitRevLoglist& rev)
{
    // CGitLogListBase::GetParentHashes: git log's --parents leaves out the parents a path limit doesn't list
    if (!rev.parents.isEmpty()) {
        return rev.parents;
    }
    try {
        const QStringList arguments{QStringLiteral("rev-parse"), rev.hash + QStringLiteral("^@")};
        const QByteArray output = Git(m_workingTree).run(arguments, RunMode::ReadOnly);
        return QString::fromLatin1(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, e.error().message, QString(), KMessageBox::Options());
    }
    return {};
}

QString GitLogList::parentInfo(qsizetype number, const QString& hash) const
{
    // parentInfo of CGitLogListBase::OnContextMenu: the subject is left out if the commit can't be read
    const QString label = i18n("Parent %1", number);
    const QString shortHash = hash.left(shortHashLength);
    try {
        const QStringList arguments{
            QStringLiteral("log"),
            QStringLiteral("-1"),
            QStringLiteral("--no-show-signature"),
            QStringLiteral("--encoding=UTF-8"),
            QStringLiteral("--format=%s"),
            hash,
        };
        QString subject = QString::fromUtf8(Git(m_workingTree).run(arguments, RunMode::ReadOnly)).trimmed();
        if (subject.size() > parentSubjectLength) {
            subject.truncate(parentSubjectLength);
            subject += QStringLiteral("...");
        }
        const QString quoted = QLatin1Char('"') + menuText(subject) + QLatin1Char('"');
        return label + QStringLiteral(": ") + quoted + QStringLiteral(" (") + shortHash + QLatin1Char(')');
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    return label + QStringLiteral(" (") + shortHash + QLatin1Char(')');
}

void GitLogList::compareWithWorkingTree(int row)
{
    // ID_COMPARE
    startCompare(row, QString(), m_model->rev(row).hash);
}

void GitLogList::compareWithPrevious(int row, qsizetype parentNo)
{
    // ID_COMPAREWITHPREVIOUS
    const QStringList parents = parentHashes(m_model->rev(row));
    if (parents.isEmpty()) {
        KMessageBox::information(this, i18n("No previous version."));
        return;
    }
    Q_ASSERT(parentNo >= 0 && parentNo < parents.size());
    startCompare(row, m_model->rev(row).hash, parents.at(parentNo));
}

void GitLogList::startCompare(int row, const QString& rev1, const QString& rev2)
{
    // CGitDiff::DiffCommit. A newer Compare entry replaces one still waiting
    m_pendingCompare.reset();
    const auto isDirectory = [this](const QString& path) {
        return QFileInfo(QDir(m_workingTree).filePath(path)).isDir();
    };
    const bool isFile = m_pathFilter.size() == 1 && !isDirectory(m_pathFilter.constFirst());
    if (!isFile) {
        // CGitDiff::DiffCommit shows the changed files with rev2 as the base (SetDiff), a folder in the filter box
        const QString folder = m_pathFilter.size() == 1 ? m_pathFilter.constFirst() : QString();
        FileDiffDlg dlg(m_workingTree, this);
        dlg.setDiff(folder, rev2, rev1.isEmpty() ? GitRev::workingCopyRef() : rev1);
        dlg.exec();
        return;
    }
    if (!m_walkOptions.follow) {
        const QString& path = m_pathFilter.constFirst();
        const QStringList arguments = GitDiff::diffArguments(path, path, rev1, rev2);
        std::ignore = GitDiff::start(this, {.workingDirectory = m_workingTree, .arguments = arguments});
        return;
    }

    // ID_COMPARE walks the commits above the selected one; ID_COMPAREWITHPREVIOUS also needs the selected one
    const int lastRow = rev1.isEmpty() ? row - 1 : row;
    QSet<int> waitingFor;
    for (int i = 0; i <= lastRow; ++i) {
        if (m_model->rev(i).filesState == GitRevLoglist::FilesState::NotLoaded) {
            waitingFor.insert(i);
        }
    }
    const PendingCompare compare{.row = row, .rev1 = rev1, .rev2 = rev2, .waitingFor = waitingFor};
    if (waitingFor.isEmpty()) {
        finishCompare(compare);
        return;
    }
    m_pendingCompare = compare;
    // TortoiseGit reads them on the spot (GetFiles); the commits' files load in the background here
    for (const int waiting : std::as_const(waitingFor)) {
        requestFiles(waiting);
    }
}

void GitLogList::onCompareFilesLoaded(int row)
{
    if (!m_pendingCompare || !m_pendingCompare->waitingFor.remove(row) || !m_pendingCompare->waitingFor.isEmpty()) {
        return;
    }
    const PendingCompare compare = *m_pendingCompare;
    m_pendingCompare.reset();
    finishCompare(compare);
}

void GitLogList::finishCompare(const PendingCompare& compare)
{
    const QString& logPath = m_pathFilter.constFirst();
    QString path = logPath;
    for (int i = 0; i < compare.row; ++i) {
        path = oldPathIn(m_model->rev(i), path);
    }
    const bool withWorkingTree = compare.rev1.isEmpty();
    const QString right = withWorkingTree ? logPath : path;
    const QString left = withWorkingTree ? path : oldPathIn(m_model->rev(compare.row), path);
    const QStringList arguments = GitDiff::diffArguments(right, left, compare.rev1, compare.rev2);
    std::ignore = GitDiff::start(this, {.workingDirectory = m_workingTree, .arguments = arguments});
}

bool GitLogList::addPushEntry(QMenu& menu, const LogMenuTarget& target)
{
    // Shift shows it for any commit (showExtendedMenu)
    const bool showExtendedMenu = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
    // Only if the entry has an associated local branch or tag
    const bool hasBranch = !firstWithPrefix(target.refs, QStringLiteral("refs/heads/")).isEmpty();
    const bool hasTag = !firstWithPrefix(target.refs, QStringLiteral("refs/tags/")).isEmpty();
    const bool isLocal = hasBranch || hasTag;
    if (!showExtendedMenu && (target.isStash || !isLocal)) {
        return false;
    }
    const GitRef::Type labelType = target.label ? target.label->type : GitRef::Type::Other;
    const bool namesLabel = labelType == GitRef::Type::LocalBranch || labelType == GitRef::Type::Tag;
    const QString labelName = target.label ? menuText(target.label->name) : QString();
    const QString text = namesLabel ? i18n("Pus&h \"%1\"...", labelName) : i18n("Pus&h...");
    menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-push")), text, this, [this, target] {
        showingGitErrors(this, [this, &target] {
            push(target);
        });
    });
    return true;
}

bool GitLogList::addDeleteEntries(QMenu& menu, const LogMenuTarget& target)
{
    // A right-clicked label limits it to that ref. The current branch can't be deleted
    QStringList refs = target.label ? QStringList{target.label->fullName()} : target.refs;
    refs.removeAll(QStringLiteral("refs/heads/") + Git(m_workingTree).currentBranch());
    if (refs.isEmpty()) {
        return false;
    }
    const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-delete"));
    if (refs.size() == 1) {
        menu.addAction(icon, i18n("&Delete") + QLatin1Char(' ') + menuText(refs.constFirst()), this, [this, refs] {
            deleteRefs(refs);
        });
        return true;
    }
    QMenu *submenu = menu.addMenu(icon, i18n("&Delete branch/tag"));
    for (const QString& ref : std::as_const(refs)) {
        submenu->addAction(menuText(ref), this, [this, ref] {
            deleteRefs({ref});
        });
    }
    submenu->addAction(i18n("All"), this, [this, refs] {
        deleteRefs(refs);
    });
    return true;
}

void GitLogList::addClipboardEntries(QMenu& menu, const std::optional<LogMenuTarget>& target)
{
    const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    QMenu *submenu = menu.addMenu(i18n("Copy to clipboard"));
    const std::array entries{
        std::pair{i18n("Full data"), CopyFormat::Full},
        std::pair{i18n("Full commit data without changed paths"), CopyFormat::FullNoPaths},
        std::pair{i18n("SHA-1"), CopyFormat::Hash},
        std::pair{i18n("Authors (full)"), CopyFormat::AuthorsFull},
        std::pair{i18n("Author names"), CopyFormat::AuthorsName},
        std::pair{i18n("Author email addresses"), CopyFormat::AuthorsEmail},
        std::pair{i18n("Subjects"), CopyFormat::Subjects},
        std::pair{i18n("Messages"), CopyFormat::Messages},
    };
    for (const auto& entry : entries) {
        submenu->addAction(icon, entry.first, this, [this, format = entry.second] {
            copySelectionToClipboard(format);
        });
    }
    if (target && !target->refs.isEmpty()) {
        submenu->addAction(icon, i18n("Tag/branch names"), this, [refs = target->refs, label = target->label] {
            AppUtils::copyToClipboard(refNamesText(refs, label));
        });
    }
}

void GitLogList::copySelectionToClipboard(CopyFormat format)
{
    // CGitLogListBase::CopySelectionToClipBoard. GetFiles and GetTagInfo don't wait here: the text is built when
    // the files and tag info have arrived
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    cancelCopy();
    m_copy = PendingCopy{.format = format, .rows = rows, .tagInfo = {}};
    if (format == CopyFormat::Full) {
        for (const int row : rows) {
            requestFiles(row);
        }
    }
    const auto hasTag = [this](int row) {
        return std::ranges::any_of(m_model->rev(row).refs, [](const GitRef& ref) {
            return ref.type == GitRef::Type::Tag;
        });
    };
    if ((format == CopyFormat::Full || format == CopyFormat::FullNoPaths) && std::ranges::any_of(rows, hasTag)) {
        // Read afresh, as GetTagInfo does: m_tagInfo is only there for a filter and misses tags made since
        m_copyTagInfoCall = Git(m_workingTree).runAsync(GitRevLoglist::tagInfoArguments(), RunMode::ReadOnly);
        connect(m_copyTagInfoCall.get(), &GitCall::finished, this, &GitLogList::onCopyTagInfoFinished);
    }
    finishCopyIfReady();
}

void GitLogList::onCopyTagInfoFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_copyTagInfoCall.release();
    Q_ASSERT(call && m_copy);
    call->deleteLater();
    try {
        m_copy->tagInfo = GitRevLoglist::parseTagInfo(call->output());
    } catch (const RuntimeError& e) {
        // As CGit::GetTagInfo's callers do, a tag that can't be read adds nothing
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    finishCopyIfReady();
}

void GitLogList::finishCopyIfReady()
{
    if (!m_copy || m_copyTagInfoCall) {
        return;
    }
    const auto isResolved = [this](int row) {
        return m_model->rev(row).filesState != GitRevLoglist::FilesState::NotLoaded;
    };
    if (m_copy->format == CopyFormat::Full && !std::ranges::all_of(m_copy->rows, isResolved)) {
        return;
    }
    QList<GitRevLoglist> revs;
    revs.reserve(m_copy->rows.size());
    for (const int row : std::as_const(m_copy->rows)) {
        revs.append(m_model->rev(row));
    }
    AppUtils::copyToClipboard(copyText(m_copy->format, revs, m_copy->tagInfo));
    m_copy.reset();
}

void GitLogList::cancelCopy()
{
    m_copy.reset();
    m_copyTagInfoCall.reset();
}

QString GitLogList::copyText(CopyFormat format, const QList<GitRevLoglist>& revs, const TagInfo& tags)
{
    // CGitLogListBase::CopySelectionToClipBoard
    QString text;
    QStringList lines;
    for (const GitRevLoglist& rev : revs) {
        switch (format) {
        case CopyFormat::Full:
        case CopyFormat::FullNoPaths:
            text += fullCommitText(rev, tags, format == CopyFormat::Full);
            break;
        case CopyFormat::Hash:
            lines.append(rev.hash);
            break;
        case CopyFormat::AuthorsFull:
            lines.append(rev.authorName + QStringLiteral(" <") + rev.authorEmail + QLatin1Char('>'));
            break;
        case CopyFormat::AuthorsName:
            lines.append(rev.authorName);
            break;
        case CopyFormat::AuthorsEmail:
            lines.append(rev.authorEmail);
            break;
        case CopyFormat::Subjects:
            text += QStringLiteral("* ") + rev.subject.trimmed() + QStringLiteral("\n\n");
            break;
        case CopyFormat::Messages:
            text += QStringLiteral("* ") + rev.subjectBody() + QStringLiteral("\n\n");
            break;
        }
    }
    return text + lines.join(QLatin1Char('\n'));
}

QString GitLogList::refNamesText(const QStringList& refs, const std::optional<GitRef>& label)
{
    // ID_COPYCLIPBOARDBRANCHTAG. Our refs never carry show-ref's ^{}: they come from git log's decorations
    if (label) {
        return label->type == GitRef::Type::Tag ? label->name : Git::stripRefName(label->fullName());
    }
    QString text;
    for (const QString& ref : refs) {
        text += ref + QLatin1Char('\n');
    }
    return text;
}

void GitLogList::resetToCommit(const LogMenuTarget& target)
{
    // ID_RESET. ResetWcRev(true) only refreshes the working tree row, which this list doesn't have yet
    if (AppUtils::gitReset(m_workingTree, target.hash, this)) {
        reloadHashMap();
    }
}

void GitLogList::createTag(const LogMenuTarget& target)
{
    // ID_CREATE_TAG: guesses a remote branch, otherwise the commit
    const QString remoteBranch = firstWithPrefix(target.refs, QStringLiteral("refs/remotes/"));
    CreateBranchTagParams params;
    params.initialRefName = remoteBranch.isEmpty() ? target.hash : remoteBranch;
    params.isTag = true;
    AppUtils::createBranchTag(m_workingTree, params, this);
    reloadHashMap();
}

void GitLogList::createBranch(const LogMenuTarget& target)
{
    // ID_CREATE_BRANCH: a right-clicked remote branch, else a remote branch on the commit to enable tracking, else
    // the commit
    QString ref = target.hash;
    if (target.label && target.label->type == GitRef::Type::RemoteBranch) {
        ref = target.label->fullName();
    } else if (const QString remoteBranch = firstWithPrefix(target.refs, QStringLiteral("refs/remotes/")); !remoteBranch.isEmpty()) {
        ref = remoteBranch;
    }
    CreateBranchTagParams params;
    params.initialRefName = ref;
    AppUtils::createBranchTag(m_workingTree, params, this);
    reloadHashMap();
}

void GitLogList::switchToRev(const LogMenuTarget& target)
{
    // ID_SWITCHTOREV: the right-clicked ref, else a remote branch on the commit to recommend a good branch name and
    // tracking, else the commit
    QString ref = target.hash;
    if (target.label) {
        ref = target.label->fullName();
    } else if (const QString remoteBranch = firstWithPrefix(target.refs, QStringLiteral("refs/remotes/")); !remoteBranch.isEmpty()) {
        ref = remoteBranch;
    }
    AppUtils::switchTo(m_workingTree, ref, this);
    reloadHashMap();
}

void GitLogList::switchBranch(const QString& branch)
{
    // ID_SWITCHBRANCH: CAppUtils::PerformSwitch without asking
    SwitchOptions options;
    options.ref = branch;
    AppUtils::performSwitch(m_workingTree, options, this);
    reloadHashMap();
}

void GitLogList::mergeRev(const LogMenuTarget& target)
{
    // ID_MERGEREV: the right-clicked ref, else the commit's first ref, else the commit
    QString ref = target.hash;
    if (target.label) {
        ref = target.label->fullName();
    } else if (!target.refs.isEmpty()) {
        ref = target.refs.constFirst();
    }
    if (MergeCommand::merge(m_workingTree, ref, this)) {
        reload();
    }
}

void GitLogList::rebaseToVersion(const LogMenuTarget& target)
{
    // ID_REBASE_TO_VERSION
    if (isBusy()) {
        KMessageBox::information(this, i18n("This operation cannot be started while the log dialog is still loading commits."));
        return;
    }
    // Try to guess a branch, optimally a local branch
    QString upstream = target.refs.isEmpty() ? target.hash : target.refs.constFirst();
    if (const QString branch = firstWithPrefix(target.refs, QStringLiteral("refs/heads/")); !branch.isEmpty()) {
        upstream = Git::stripRefName(branch);
    }
    RebaseOptions options;
    options.upstream = upstream;
    RebaseDlg dlg(m_workingTree, options, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
    }
}

void GitLogList::cherryPick()
{
    // ID_CHERRY_PICK
    if (isBusy()) {
        KMessageBox::information(this, i18n("This operation cannot be started while the log dialog is still loading commits."));
        return;
    }
    RebaseOptions options;
    options.cherryPick = true;
    options.upstream = Git(m_workingTree).currentBranch(); // m_CurrentBranch, which cherry-pick mode doesn't use
    for (const int row : selectedRows()) {
        GitRevLoglist rev = m_model->rev(row);
        rev.rebaseAction = GitRevLoglist::RebasePick;
        options.commits.append(rev);
    }
    RebaseDlg dlg(m_workingTree, options, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
    }
}

void GitLogList::revertRev(int parent)
{
    // ID_REVERTREV
    if (!revertSelectedCommits(parent)) {
        return;
    }
    QMessageBox box(QMessageBox::Question,
                    i18n("KortoiseGit"),
                    i18n("Revision(s) reverted. All changes are integrated into your working tree now."),
                    QMessageBox::NoButton,
                    this);
    QPushButton *okButton = box.addButton(QMessageBox::Ok);
    okButton->setText(i18n("&OK"));
    const QPushButton *commit = box.addButton(i18n("Commit"), QMessageBox::ActionRole);
    box.setDefaultButton(okButton);
    box.setEscapeButton(okButton);
    box.exec();
    if (box.clickedButton() == commit) {
        openCommitDlg();
    }
    reload();
}

bool GitLogList::revertSelectedCommits(int parent)
{
    // CGitLogList::RevertSelectedCommits. Each git revert runs while the progress dialog waits, as TortoiseGit's do
    const QString question = i18n("Revert the selected commit(s)?");
    if (QMessageBox::question(this, i18n("KortoiseGit"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    const QList<int> rows = selectedRows();
    std::unique_ptr<QProgressDialog> progress;
    if (rows.size() > 1) {
        progress = std::make_unique<QProgressDialog>(QString(), i18n("Cancel"), 0, static_cast<int>(rows.size()), this);
        progress->setWindowTitle(i18n("Revert commit"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
    }

    const Git git(m_workingTree);
    const QString mergeMsgPath = QDir(GitAdminDir::worktreeAdminDirPath(m_workingTree)).filePath(QStringLiteral("MERGE_MSG"));
    QString mergeMsg;
    bool reverted = false;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const GitRevLoglist& rev = m_model->rev(rows.at(i));
        if (progress) {
            progress->setLabelText(i18n("Revert commit %1", rev.hash) + QLatin1Char('\n') + rev.subject);
            progress->setValue(static_cast<int>(i));
        }

        if (const std::optional<QString> failure = gitRevert(git, parent, rev.hash)) {
            if (!continueAfterFailedRevert(this, *failure, rows.size() == 1)) {
                // As TortoiseGit does, an abort leaves MERGE_MSG as git last wrote it
                return reverted;
            }
        } else {
            if (!mergeMsg.isEmpty()) {
                mergeMsg += QLatin1Char('\n');
            }
            // CGit::LoadTextFile: git revert wrote this commit's message, replacing the last one's
            QFile file(mergeMsgPath);
            if (file.open(QIODevice::ReadOnly)) {
                mergeMsg += QString::fromUtf8(file.readAll());
            }
            reverted = true;
        }

        if (progress && progress->wasCanceled()) {
            break;
        }
    }
    if (progress) {
        progress->setValue(progress->maximum());
    }
    if (!mergeMsg.isEmpty()) {
        // CStringUtils::WriteStringToTextFile: the messages of every commit reverted
        QFile file(mergeMsgPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(mergeMsg.toUtf8()) < 0) {
            KMessageBox::error(this, file.errorString(), QString(), KMessageBox::Options());
        }
    }
    return reverted;
}

void GitLogList::openCommitDlg()
{
    // CAppUtils::Commit, for the log's path (m_Path)
    QStringList paths;
    for (const QString& path : std::as_const(m_pathFilter)) {
        paths.append(QDir(m_workingTree).filePath(path));
    }
    if (paths.isEmpty()) {
        paths.append(m_workingTree);
    }
    std::ignore = AppUtils::commit(paths, QString(), this);
}

void GitLogList::push(const LogMenuTarget& target)
{
    // ID_PUSH. PushDlg has no "Push all branches" yet, which a ref chosen from its label would turn off
    QString guessedBranch = target.hash;
    if (target.label && target.label->type != GitRef::Type::RemoteBranch) {
        guessedBranch = target.label->fullName();
    } else if (const QString branch = firstWithPrefix(target.refs, QStringLiteral("refs/heads/")); !branch.isEmpty()) {
        guessedBranch = branch;
    } else if (const QString tag = firstWithPrefix(target.refs, QStringLiteral("refs/tags/")); !tag.isEmpty()) {
        guessedBranch = tag;
    }
    if (AppUtils::push(m_workingTree, guessedBranch, this)) {
        reload();
    }
}

void GitLogList::deleteRefs(const QStringList& refs)
{
    // ID_DELETE: stops at the first refusal or failure, and "All" reloads if any went
    const Git git(m_workingTree);
    bool deletedAny = false;
    for (const QString& ref : refs) {
        if (!AppUtils::deleteRef(git, ref, this)) {
            break;
        }
        deletedAny = true;
    }
    if (deletedAny) {
        reloadHashMap();
    }
}

void GitLogList::reloadHashMap()
{
    // CGitLogListBase::ReloadHashMap
    const Git git(m_workingTree);
    try {
        const QByteArray output = git.run(GitRevLoglist::refMapArguments(), RunMode::ReadOnly);
        m_model->setRefMap(GitRevLoglist::parseRefMap(output, git.currentBranch()));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        const QString message = i18n("Could not get all refs.") + QStringLiteral("\n") + e.error().message;
        KMessageBox::error(this, message, QString(), KMessageBox::Options());
    }
}

} // namespace kgt
