// SPDX-FileCopyrightText: 2008-2021, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingGitRemote.h (CSettingGitRemote).
 */

#pragma once

#include "git/git.hpp"
#include "settings/settingsproppage.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace kgt
{

/**
 * The Settings dialog's "Remote" page, under "Git": the repository's remotes, with each one's URL, push URL, tag
 * option and prune setting and whether it is the default to push to. A remote is added or changed with
 * "Add New/Save", renamed and removed.
 *
 * Left out: the PuTTY key, as we use OpenSSH.
 */
class SettingGitRemote : public SettingsPropPage
{
    Q_OBJECT

public:
    /**
     * Reads the remotes; this runs git, quickly, on the calling thread.
     * @param repository The working tree's root or the bare repository.
     * @param parent Parent widget.
     */
    explicit SettingGitRemote(const QString& repository, QWidget *parent = nullptr);

    /**
     * m_bNoFetch: set when the page is the one the dialog was opened for, as from Push's "Manage".
     * @param noFetch Whether to leave out the offer to fetch from a remote just added.
     */
    void setNoFetch(bool noFetch);

    /**
     * Ported from CSettingGitRemote::OnApply: adds the remote if its name is new, then saves what was changed.
     * @returns False if something is missing or git failed, which is shown to the user.
     */
    [[nodiscard]] bool apply() override;

    /**
     * Ported from CSettingGitRemote::IsRemoteCollideWithRefspec's CheckRemoteCollideWithRefspec: whether another
     * remote, or a git-svn remote, already fetches into refs/remotes/<remote>.
     * @param remote A remote's name.
     * @param configEntries The fetch refspecs of the repository's configuration, as git config -z --get-regexp
     *                      prints them: each a key, a newline, the value and a NUL.
     * @returns Whether the name collides with one of them.
     */
    [[nodiscard]] static bool remoteCollidesWithRefspec(const QString& remote, const QByteArray& configEntries);

private:
    /**
     * What was changed since the remote was loaded, CSettingGitRemote's REMOTE_* bits.
     */
    enum ChangedFlag : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
        RemoteName = 0x1,
        RemoteUrl = 0x2,
        RemoteTagOpt = 0x8,
        RemotePrune = 0x10,
        RemotePushDefault = 0x40,
        RemotePushUrl = 0x80,
    };

    /**
     * Ported from CSettingGitRemote::OnBnClickedButtonAdd.
     */
    void onButtonAdd();

    /**
     * Ported from CSettingGitRemote::OnBnClickedButtonRemove.
     */
    void onButtonRemove();

    /**
     * Ported from CSettingGitRemote::OnBnClickedButtonRenameRemote.
     */
    void onButtonRenameRemote();

    /**
     * Ported from CSettingGitRemote::OnLbnSelchangeListRemote: offers to save the changes, then loads the remote
     * selected.
     */
    void onSelChangeListRemote();

    /**
     * Ported from CSettingGitRemote::OnEnChangeEditRemote.
     */
    void onChangeEditRemote();

    /**
     * Ported from CSettingGitRemote::OnEnChangeEditUrl: the first remote is named "origin" for the user.
     */
    void onChangeEditUrl();

    /**
     * The handlers of the controls that only note the change (OnEnChangeEditPushUrl, OnCbnSelchangeComboTagOpt,
     * OnBnClickedCheckprune, OnBnClickedCheckpushdefault).
     * @param flag The control's REMOTE_* bit.
     */
    void onChange(ChangedFlag flag);

    /**
     * The "//Add Remote" part of OnApply: git remote add, after asking whether a second remote should fetch tags,
     * and the offer to fetch from it.
     * @param remote The new remote's name.
     * @param url Its URL.
     * @returns False if something is missing or git failed.
     */
    [[nodiscard]] bool addRemote(const QString& remote, const QString& url);

    /**
     * The end of OnApply: saves the URL, tag option, prune setting and push URL, whichever were changed.
     * @param remote The remote's name.
     * @returns False if saving failed.
     */
    [[nodiscard]] bool saveRemoteSettings(const QString& remote);

    /**
     * The remote.pushdefault part of OnApply.
     * @param remote The remote's name.
     * @returns False if saving failed.
     */
    [[nodiscard]] bool applyPushDefault(const QString& remote);

    /**
     * Ported from CSettingGitRemote::IsRemoteExist.
     * @param remote A remote's name.
     * @returns Whether the list has it.
     */
    [[nodiscard]] bool isRemoteExist(const QString& remote) const;

    /**
     * Ported from CSettingGitRemote::IsRemoteCollideWithRefspec.
     * @param remote A remote's name.
     * @returns Whether another remote's fetch refspec writes to its refs/remotes/ folder.
     */
    [[nodiscard]] bool isRemoteCollideWithRefspec(const QString& remote) const;

    /**
     * Ported from CSettingGitRemote::SaveGeneral: sets a key of the repository's configuration, or removes it if
     * the value is empty.
     * @param key The whole key.
     * @param value Its new value.
     * @returns False if it couldn't be saved, which is shown to the user.
     */
    [[nodiscard]] bool saveGeneral(const QString& key, const QString& value);

    /**
     * Adds a remote to the sorted list and selects it, without loading it: the fields already show it.
     * @param remote The remote's name.
     */
    void addAndSelect(const QString& remote);

    /**
     * Shows a hint at an edit field, TortoiseGit's ShowEditBalloon.
     * @param edit The field.
     * @param text The hint.
     */
    static void showEditBalloon(QLineEdit *edit, const QString& text);

    Git m_git;
    bool m_noFetch = false; // m_bNoFetch
    unsigned int m_changedMask = 0; // m_ChangedMask
    QListWidget *m_remoteList;
    QLineEdit *m_remote;
    QLineEdit *m_url;
    QLineEdit *m_pushUrl;
    QComboBox *m_tagOpt;
    QCheckBox *m_pushDefault;
    QCheckBox *m_prune;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_renameButton;
};

} // namespace kgt
