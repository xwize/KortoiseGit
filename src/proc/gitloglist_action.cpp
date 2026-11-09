// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp (CGitLogListBase::OnContextMenu) and
 * GitLogListAction.cpp (CGitLogListBase::ContextMenuAction): the log's right-click menu. Entries are added in
 * OnContextMenu's order, each section ending with its separators, so the entries not ported yet can slot in. Also
 * CGitLogListBase::CopySelectionToClipBoard, which waits for what TortoiseGit reads synchronously.
 */

#include "gitloglist.hpp"

#include "apputils.hpp"
#include "createbranchtagdlg.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "gitstatuslistmodel.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

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

/**
 * Ported from GitRevLoglist::GetSubjectBody. TortoiseGit's m_Body starts with the blank line after the subject.
 * @param rev A commit.
 * @returns Its message.
 */
QString subjectBody(const GitRevLoglist& rev)
{
    QString text = rev.subject + QLatin1Char('\n');
    if (!rev.body.isEmpty()) {
        text += QLatin1Char('\n') + rev.body;
    }
    return text;
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
    text += i18n("Message") + QStringLiteral(":\n") + subjectBody(rev) + QLatin1Char('\n');
    text += notesAndTagsText(rev, tagInfo);
    if (withPaths) {
        text += changedPathsText(rev);
    }
    return text + QLatin1Char('\n');
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
    if (rows.size() == 1) {
        const int row = rows.constFirst();
        const GitRevLoglist& rev = m_model->rev(row);
        const QStringList refs = sortedRefNames(rev);
        const std::optional<GitRef> label = fromKeyboard ? std::nullopt : refLabelAt(row, event->pos());
        target = LogMenuTarget{.hash = rev.hash, .refs = refs, .label = label, .isStash = isOnStash(row)};
        addSingleCommitEntries(menu, *target);
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
    // m_hasWC: CLogDlg sets it for anything but a bare repository
    if (!target.isStash && !GitAdminDir::isBareRepo(m_workingTree)) {
        const QString text = i18n("R&eset \"%1\" to this...", menuText(Git(m_workingTree).currentBranch()));
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-reset")), text, this, [this, target] {
            showingGitErrors(this, [this, &target] {
                resetToCommit(target);
            });
        });
    }

    if (!target.isStash) {
        const QIcon icon = QIcon::fromTheme(QStringLiteral("tag"));
        menu.addAction(icon, i18n("Create &Tag at this version..."), this, [this, target] {
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
            text += QStringLiteral("* ") + subjectBody(rev) + QStringLiteral("\n\n");
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
