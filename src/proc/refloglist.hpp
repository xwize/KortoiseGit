// SPDX-FileCopyrightText: 2009-2011, 2015-2017, 2020-2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/refloglist.h (CRefLogList) and the reflog entries of
 * CGitLogListBase::OnContextMenu and CGitLogList::ContextMenuAction.
 */

#pragma once

#include "git/gitcall.hpp"
#include "git/gitrevloglist.hpp"
#include "refloglistmodel.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <QTreeView>

#include <memory>

namespace kgt
{

/**
 * The reflog's list and its right-click menu. Deleting runs git one step at a time without blocking; isBusy() is
 * true meanwhile, and the entries must not change then.
 */
class RefLogList : public QTreeView
{
    Q_OBJECT

public:
    /**
     * What Copy to clipboard copies (CRefLogList::CopySelectionToClipBoard's toCopy).
     */
    enum class CopyFormat {
        Full, // ID_COPYCLIPBOARDFULL
        Hash, // ID_COPYCLIPBOARDHASH
        Messages, // ID_COPYCLIPBOARDMESSAGES
    };

    /**
     * @param workingTree The working tree's root or the bare repository.
     * @param parent Parent widget.
     */
    explicit RefLogList(const QString& workingTree, QWidget *parent = nullptr);
    ~RefLogList() override;

    RefLogList(const RefLogList&) = delete;
    RefLogList& operator=(const RefLogList&) = delete;
    RefLogList(RefLogList&&) = delete;
    RefLogList& operator=(RefLogList&&) = delete;

    /**
     * @param ref The ref whose reflog is shown (m_CurrentBranch), which a delete reads again.
     */
    void setCurrentBranch(const QString& ref);

    /**
     * @param entries The reflog, newest first.
     */
    void setEntries(const QList<GitRevLoglist>& entries);

    /**
     * @returns The reflog shown, newest first.
     */
    [[nodiscard]] const QList<GitRevLoglist>& entries() const;

    /**
     * @returns Whether a delete is going on.
     */
    [[nodiscard]] bool isBusy() const;

    /**
     * The check before deleting in CGitLogList::ContextMenuAction (ID_REFLOG_DEL): positions in a reflog shift, so
     * entries chosen from a stale view would delete the wrong ones.
     * @param shown The reflog as shown.
     * @param current The reflog as just read.
     * @returns True if they differ in length, the first entry's date or hash, or the last entry's hash.
     */
    [[nodiscard]] static bool isOutOfDate(const QList<GitRevLoglist>& shown, const QList<GitRevLoglist>& current);

    /**
     * The commands of CGitLogList::ContextMenuAction (ID_REFLOG_DEL), last entry first so the earlier positions stay
     * valid.
     * @param refs The selected entries' names, such as refs/stash@{2}, newest first.
     * @returns The arguments after "git" of each command, in the order to run them.
     */
    [[nodiscard]] static QList<QStringList> deleteCommands(const QStringList& refs);

    /**
     * Ported from CRefLogList::CopySelectionToClipBoard.
     * @param format What to copy.
     * @param entries The selected entries, newest first.
     * @returns The text.
     */
    [[nodiscard]] static QString copyText(CopyFormat format, const QList<GitRevLoglist>& entries);

    /**
     * @returns The selected entries, newest first.
     */
    [[nodiscard]] QList<GitRevLoglist> selectedEntries() const;

Q_SIGNALS:
    /**
     * Entries were deleted, MSG_REFLOG_CHANGED: the reflog must be loaded again.
     */
    void refLogChanged();

    /**
     * @param busy Whether a delete has started or ended.
     */
    void busyChanged(bool busy);

protected:
    /**
     * Ported from CGitLogListBase::OnContextMenu.
     * @param event The event.
     */
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /**
     * @returns The selected rows, in order.
     */
    [[nodiscard]] QList<int> selectedRows() const;

    /**
     * Ported from CRefLogList::OnNMDblclkLoglist and ID_LOG: opens Show Log at a commit.
     * @param hash The commit.
     */
    void showLog(const QString& hash) const;

    /**
     * ID_REFLOG_DEL: reads the reflog again, then goes on in deleteAfterCheck().
     */
    void deleteSelected();

    /**
     * ID_REFLOG_DEL once the reflog is read: refuses a stale view, confirms and deletes.
     * @param current The reflog as just read.
     */
    void deleteAfterCheck(const QList<GitRevLoglist>& current);

    /**
     * Starts the next delete command, or finishes if none is left.
     */
    void runNextDelete();

    /**
     * @param busy Whether a delete is going on.
     */
    void setBusy(bool busy);

    QString m_workingTree;
    QString m_currentBranch;
    RefLogListModel *m_model;
    bool m_busy = false;
    QStringList m_refsToDelete; // The chosen entries' names, while the reflog is read again
    QList<QStringList> m_deleteCommands; // What is left to run
    std::unique_ptr<GitCall> m_checkCall; // Reading the reflog again
    std::unique_ptr<GitCall> m_deleteCall; // The delete command running
};

} // namespace kgt
