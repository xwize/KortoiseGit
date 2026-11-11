// SPDX-FileCopyrightText: 2012, 2017, 2020, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/DeleteRemoteTagDlg.h (CDeleteRemoteTagDlg): lists a remote's tags and
 * deletes the selected ones there.
 */

#pragma once

#include "git/git.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <memory>

class QCheckBox;
class QPushButton;
class QStandardItemModel;
class QTreeView;

namespace kgt
{

class GitCall;

/**
 * The "Delete remote tag" dialog. The tags load in the background from git ls-remote; Delete confirms and pushes
 * the deletions, then the list is loaded again.
 */
class DeleteRemoteTagDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the remote's tags.
     * @param workingTree The working tree root or bare repository git runs in.
     * @param remote The remote, m_sRemote.
     * @param parent Parent widget.
     */
    DeleteRemoteTagDlg(const QString& workingTree, const QString& remote, QWidget *parent = nullptr);
    ~DeleteRemoteTagDlg() override;

    DeleteRemoteTagDlg(const DeleteRemoteTagDlg&) = delete;
    DeleteRemoteTagDlg& operator=(const DeleteRemoteTagDlg&) = delete;
    DeleteRemoteTagDlg(DeleteRemoteTagDlg&&) = delete;
    DeleteRemoteTagDlg& operator=(DeleteRemoteTagDlg&&) = delete;

    /**
     * Ported from CDeleteRemoteTagDlg::Refresh: loads the remote's tags again, in the background. refreshed() is
     * emitted when done.
     */
    void refresh();

    /**
     * @returns Whether the tags are being loaded.
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * @returns The tag list.
     */
    [[nodiscard]] QTreeView *tagList() const;

    /**
     * @returns The tags listed, in order.
     */
    [[nodiscard]] QStringList tags() const;

    /**
     * @returns The Delete button.
     */
    [[nodiscard]] QPushButton *deleteButton() const;

    /**
     * @returns The "Select/deselect all" box.
     */
    [[nodiscard]] QCheckBox *selectAllBox() const;

    void done(int result) override;

Q_SIGNALS:
    /**
     * The tags have been loaded (or failed to load) and listed.
     */
    void refreshed();

private:
    void onLoadFinished();
    void onSelectAllClicked();
    void onSelectionChanged();
    void onDeleteClicked();
    [[nodiscard]] QStringList selectedTags() const;

    Git m_git;
    QString m_remote;
    std::unique_ptr<GitCall> m_tagsCall;

    QTreeView *m_tags = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *m_selectAll = nullptr;
    QPushButton *m_delete = nullptr;
};

} // namespace kgt
