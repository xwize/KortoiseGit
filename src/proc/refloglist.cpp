// SPDX-FileCopyrightText: 2009-2011, 2013, 2015-2020, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/refloglist.cpp (CRefLogList) and the reflog entries of
 * CGitLogListBase::OnContextMenu and CGitLogList::ContextMenuAction.
 */

#include "refloglist.hpp"

#include "apputils.hpp"
#include "git/git.hpp"
#include "gitloglistmodel.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QMenu>

#include <algorithm>
#include <array>
#include <utility>

namespace
{

using namespace kgt;

constexpr KMessageBox::Options noNotify = KMessageBox::Options();
// InsertRefLogColumn's widths, widened for the longer columns of a list that doesn't resize its text
constexpr int hashColumnWidth = 120;
constexpr int refColumnWidth = 150;
constexpr int actionColumnWidth = 110;
constexpr int messageColumnWidth = 300;

/**
 * @param entry A reflog entry.
 * @returns Its action and subject, as CopySelectionToClipBoard joins them.
 */
QString actionAndMessage(const GitRevLoglist& entry)
{
    const QString action = entry.refAction.isEmpty() ? QString() : entry.refAction + QStringLiteral(": ");
    return action + entry.subjectBody();
}

/**
 * @param parent Parent of the message.
 * @param text What to say.
 */
void showError(QWidget *parent, const QString& text)
{
    KMessageBox::error(parent, text, QString(), noNotify);
}

/**
 * @param parent Parent of the question.
 * @param question IDS_PROC_DELETEREF or IDS_PROC_DELETENREFS.
 * @returns Whether the user chose Delete.
 */
bool confirmDelete(QWidget *parent, const QString& question)
{
    const KGuiItem deleteItem(i18n("&Delete"));
    const KGuiItem abortItem(i18n("A&bort"));
    const auto answer = KMessageBox::questionTwoActions(parent, question, {}, deleteItem, abortItem, {}, noNotify);
    return answer == KMessageBox::PrimaryAction;
}

} // namespace

namespace kgt
{

RefLogList::RefLogList(const QString& workingTree, QWidget *parent)
    : QTreeView(parent)
    , m_workingTree(workingTree)
    , m_model(new RefLogListModel(this))
{
    setModel(m_model);
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    header()->resizeSection(RefLogListModel::HashColumn, hashColumnWidth);
    header()->resizeSection(RefLogListModel::RefColumn, refColumnWidth);
    header()->resizeSection(RefLogListModel::ActionColumn, actionColumnWidth);
    header()->resizeSection(RefLogListModel::MessageColumn, messageColumnWidth);

    connect(this, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        showLog(m_model->entries().at(index.row()).hash);
    });
}

RefLogList::~RefLogList() = default;

void RefLogList::setCurrentBranch(const QString& ref)
{
    m_currentBranch = ref;
}

void RefLogList::setEntries(const QList<GitRevLoglist>& entries)
{
    m_model->setEntries(entries);
}

const QList<GitRevLoglist>& RefLogList::entries() const
{
    return m_model->entries();
}

bool RefLogList::isBusy() const
{
    return m_busy;
}

bool RefLogList::isOutOfDate(const QList<GitRevLoglist>& shown, const QList<GitRevLoglist>& current)
{
    if (current.size() != shown.size()) {
        return true;
    }
    if (current.isEmpty()) {
        return false;
    }
    const GitRevLoglist& first = current.constFirst();
    const GitRevLoglist& newest = shown.constFirst();
    const bool firstDiffers = first.commitDate != newest.commitDate || first.hash != newest.hash;
    return firstDiffers || current.constLast().hash != shown.constLast().hash;
}

QList<QStringList> RefLogList::deleteCommands(const QStringList& refs)
{
    QList<QStringList> commands;
    for (auto entry = refs.crbegin(); entry != refs.crend(); ++entry) {
        QString ref = *entry;
        if (ref.startsWith(QLatin1String("refs/"))) {
            ref = ref.mid(5);
        }
        const qsizetype position = ref.lastIndexOf(QLatin1Char('{'));
        if (position > 0 && ref.mid(position - 1, 2) != QLatin1String("@{")) {
            ref = ref.left(position) + QLatin1Char('@') + ref.mid(position);
        }
        const bool isStash = ref.startsWith(QLatin1String("stash"));
        const QString command = isStash ? QStringLiteral("stash") : QStringLiteral("reflog");
        const QString action = isStash ? QStringLiteral("drop") : QStringLiteral("delete");
        commands.append({command, action, QStringLiteral("--"), ref});
    }
    return commands;
}

QString RefLogList::copyText(CopyFormat format, const QList<GitRevLoglist>& entries)
{
    QString text;
    bool first = true;
    for (const GitRevLoglist& entry : entries) {
        switch (format) {
        case CopyFormat::Full: {
            const QString date = GitLogListModel::formatDate(entry.commitDate);
            text += i18n("Revision") + QStringLiteral(": ") + entry.hash + QLatin1Char('\n');
            text += i18n("Date") + QStringLiteral(": ") + date + QLatin1Char('\n');
            text += i18n("Message") + QStringLiteral(": ") + actionAndMessage(entry) + QLatin1Char('\n');
            break;
        }
        case CopyFormat::Messages:
            text += QStringLiteral("* ") + actionAndMessage(entry) + QStringLiteral("\n\n");
            break;
        case CopyFormat::Hash:
            if (!first) {
                text += QLatin1Char('\n');
            }
            text += entry.hash;
            break;
        }
        first = false;
    }
    return text;
}

QList<int> RefLogList::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::ranges::sort(rows);
    return rows;
}

QList<GitRevLoglist> RefLogList::selectedEntries() const
{
    QList<GitRevLoglist> selected;
    for (const int row : selectedRows()) {
        selected.append(m_model->entries().at(row));
    }
    return selected;
}

void RefLogList::contextMenuEvent(QContextMenuEvent *event)
{
    const QList<GitRevLoglist> selected = selectedEntries();
    if (selected.isEmpty() || m_busy) {
        return;
    }

    // TODO: CRefLogList also has the other entries of Show Log's menu: compare, reset, create tag and so on
    QMenu menu(this);
    if (selected.size() == 1) {
        const QIcon logIcon = QIcon::fromTheme(QStringLiteral("view-history"));
        QAction *showLogAction = menu.addAction(logIcon, i18n("Show log"), this, [this, selected] {
            showLog(selected.constFirst().hash);
        });
        menu.setDefaultAction(showLogAction);
        menu.addSeparator();
    }
    const QIcon deleteIcon = QIcon::fromTheme(QStringLiteral("edit-delete"));
    menu.addAction(deleteIcon, i18n("Delete Ref..."), this, &RefLogList::deleteSelected);
    if (selected.size() == 1 && selected.constFirst().ref.startsWith(QLatin1String("refs/stash"))) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-stash-pop")), i18n("Stash Apply"), this, [this, selected] {
            std::ignore = AppUtils::stashApply(Git(m_workingTree), selected.constFirst().ref, window());
        });
    }
    if (selected.size() <= 2) {
        menu.addSeparator();
    }

    QMenu *clipboard = menu.addMenu(i18n("Copy to clipboard"));
    const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    const std::array entries{
        std::pair{i18n("Full data"), CopyFormat::Full},
        std::pair{i18n("SHA-1"), CopyFormat::Hash},
        std::pair{i18n("Messages"), CopyFormat::Messages},
    };
    for (const auto& entry : entries) {
        clipboard->addAction(icon, entry.first, this, [format = entry.second, selected] {
            AppUtils::copyToClipboard(copyText(format, selected));
        });
    }
    menu.exec(event->globalPos());
}

void RefLogList::showLog(const QString& hash) const
{
    // TortoiseGit passes the commit as /endrev and /rev. Our log has neither yet: as a range the commit comes first
    QStringList arguments{QStringLiteral("--command"), QStringLiteral("log")};
    arguments << QStringLiteral("--path") << m_workingTree << QStringLiteral("--range") << hash;
    std::ignore = AppUtils::runTortoiseGitProc(arguments);
}

void RefLogList::deleteSelected()
{
    m_refsToDelete.clear();
    for (const GitRevLoglist& entry : selectedEntries()) {
        m_refsToDelete.append(entry.ref);
    }
    if (m_refsToDelete.isEmpty()) {
        return;
    }
    setBusy(true);

    const Git git(m_workingTree);
    if (!git.hasRefLog(m_currentBranch)) {
        deleteAfterCheck({});
        return;
    }
    m_checkCall = git.runAsync(GitRevLoglist::refLogArguments(m_currentBranch), RunMode::ReadOnly);
    connect(m_checkCall.get(), &GitCall::finished, this, [this] {
        GitCall *call = m_checkCall.release();
        call->disconnect(this);
        // Not deleted here: this runs inside its finished() signal
        call->deleteLater();
        try {
            deleteAfterCheck(GitRevLoglist::parseRefLog(call->output(), m_currentBranch));
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            showError(this, i18n("Error while loading reflog.") + QLatin1Char('\n') + e.error().message);
            setBusy(false);
        }
    });
}

void RefLogList::deleteAfterCheck(const QList<GitRevLoglist>& current)
{
    if (isOutOfDate(m_model->entries(), current)) {
        showError(this,
                  i18n("The current view seems to be out of date. "
                       "Please refresh, e.g. by pressing F5 and recheck the selection."));
        setBusy(false);
        return;
    }

    const qsizetype count = m_refsToDelete.size();
    const QString single = i18n(
        "\"%1\" will be permanently deleted. It can NOT be recovered!\n\n"
        "Do you really want to continue?",
        m_refsToDelete.constFirst());
    const QString several = i18n(
        "Do you really want to permanently delete the %1 selected refs? "
        "It can NOT be recovered!",
        count);
    if (!confirmDelete(this, count > 1 ? several : single)) {
        setBusy(false);
        return;
    }

    m_deleteCommands = deleteCommands(m_refsToDelete);
    runNextDelete();
}

void RefLogList::runNextDelete()
{
    if (m_deleteCommands.isEmpty()) {
        setBusy(false);
        Q_EMIT refLogChanged();
        return;
    }
    m_deleteCall = Git(m_workingTree).runAsync(m_deleteCommands.takeFirst());
    connect(m_deleteCall.get(), &GitCall::finished, this, [this] {
        GitCall *call = m_deleteCall.release();
        call->disconnect(this);
        // Not deleted here: this runs inside its finished() signal
        call->deleteLater();
        // As in TortoiseGit a failure is reported and the rest are still tried
        if (call->exitCode() != 0) {
            showError(this, call->combinedOutput());
        }
        runNextDelete();
    });
}

void RefLogList::setBusy(bool busy)
{
    m_busy = busy;
    Q_EMIT busyChanged(busy);
}

} // namespace kgt
