// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp (CGitLogListBase::OnContextMenu) and
 * GitLogListAction.cpp (CGitLogListBase::ContextMenuAction): the log's right-click menu. Entries are added in
 * OnContextMenu's order, each section ending with its separators, so the entries not ported yet can slot in.
 */

#include "gitloglist.hpp"

#include "apputils.hpp"
#include "createbranchtagdlg.hpp"
#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>

#include <algorithm>
#include <functional>

namespace
{

using namespace kgt;

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
    const QPoint anchor =
        fromKeyboard ? viewport()->mapToGlobal(visualRect(m_model->index(rows.constFirst(), GitLogListModel::MessageColumn)).center()) : event->globalPos();

    QMenu menu(this);
    if (rows.size() == 1) {
        const int row = rows.constFirst();
        const GitRevLoglist& rev = m_model->rev(row);
        addSingleCommitEntries(
            menu,
            {.hash = rev.hash, .refs = sortedRefNames(rev), .label = fromKeyboard ? std::nullopt : refLabelAt(row, event->pos()), .isStash = isOnStash(row)});
    }
    if (!menu.isEmpty()) {
        menu.exec(anchor);
    }
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
    if (!target.isStash) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("tag")), i18n("Create &Tag at this version..."), this, [this, target] {
            showingGitErrors(this, [this, &target] {
                createTag(target);
            });
        });
    }
    menu.addSeparator();

    const bool hasPush = addPushEntry(menu, target);
    const bool hasDelete = addDeleteEntries(menu, target);
    if (hasPush || hasDelete) {
        menu.addSeparator();
    }
}

bool GitLogList::addPushEntry(QMenu& menu, const LogMenuTarget& target)
{
    // Shift shows it for any commit (showExtendedMenu)
    const bool showExtendedMenu = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
    // Only if the entry has an associated local branch or tag
    const bool isLocal =
        !firstWithPrefix(target.refs, QStringLiteral("refs/heads/")).isEmpty() || !firstWithPrefix(target.refs, QStringLiteral("refs/tags/")).isEmpty();
    if (!showExtendedMenu && (target.isStash || !isLocal)) {
        return false;
    }
    const bool namesLabel = target.label && (target.label->type == GitRef::Type::LocalBranch || target.label->type == GitRef::Type::Tag);
    const QString text = namesLabel ? i18n("Pus&h \"%1\"...", menuText(target.label->name)) : i18n("Pus&h...");
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
        m_model->setRefMap(GitRevLoglist::parseRefMap(git.run(GitRevLoglist::refMapArguments(), RunMode::ReadOnly), git.currentBranch()));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, i18n("Could not get all refs.") + QStringLiteral("\n") + e.error().message, QString(), KMessageBox::Options());
    }
}

} // namespace kgt
