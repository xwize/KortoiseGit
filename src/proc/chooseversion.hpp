// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ChooseVersion.h (CChooseVersion): the "Branch / Tag / Commit" chooser
 * that Merge, Rebase, Switch and Create Branch embed.
 */

#pragma once

#include "git/git.hpp"

#include <QGroupBox>
#include <QString>

#include <memory>

class QComboBox;
class QPushButton;
class QRadioButton;

namespace kgt
{

class GitCall;

/**
 * A group box offering a branch, a tag or a commit to work from. TortoiseGit mixes CChooseVersion into each host
 * dialog; here it is a widget the host lays out. The branches and tags load in the background, as TortoiseGit's
 * LoadingThread does: while they load the Branch and Tag choices are disabled, and loaded() tells the host when
 * it may enable its OK button again.
 */
class ChooseVersion : public QGroupBox
{
    Q_OBJECT

public:
    /**
     * The radio buttons, TortoiseGit's IDC_RADIO_HEAD, IDC_RADIO_BRANCH, IDC_RADIO_TAGS and IDC_RADIO_VERSION.
     */
    enum class Choice {
        Head,
        Branch,
        Tag,
        Version,
    };

    /**
     * @param git Runs git in the working tree.
     * @param title The group box's title, such as "From".
     * @param parent Parent widget.
     */
    ChooseVersion(Git git, const QString& title, QWidget *parent = nullptr);
    ~ChooseVersion() override;

    ChooseVersion(const ChooseVersion&) = delete;
    ChooseVersion& operator=(const ChooseVersion&) = delete;
    ChooseVersion(ChooseVersion&&) = delete;
    ChooseVersion& operator=(ChooseVersion&&) = delete;

    /**
     * @param refName What to select once loaded (m_initialRefName): a branch, tag or commit, short or full.
     */
    void setInitialRefName(const QString& refName);

    /**
     * @param skip Whether the current branch is left out of the branch list (m_bSkipCurrentBranch).
     */
    void setSkipCurrentBranch(bool skip);

    /**
     * Ported from InitChooseVersion: starts loading the branches and tags.
     * @param setFocusToBranchComboBox Whether the first enabled combo box gets the focus when they are loaded.
     */
    void init(bool setFocusToBranchComboBox = false);

    /**
     * @returns Whether the branches and tags are loading.
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * Ported from SetDefaultChoose: checks a radio button and enables its combo box.
     * @param choice The radio button.
     */
    void setDefaultChoose(Choice choice);

    /**
     * @returns The checked radio button.
     */
    [[nodiscard]] Choice choice() const;

    /**
     * Ported from UpdateRevsionName: what the user chose, as git should be given it. A branch or tag whose name
     * is also the other's gets its refs/heads/ or refs/tags/ prefix.
     * @returns The version (m_VersionName), or an empty string if none is chosen.
     */
    [[nodiscard]] QString versionName() const;

    /**
     * @returns Whether the Branch radio button is checked (m_bIsBranch).
     */
    [[nodiscard]] bool isBranch() const;

    /**
     * @returns Whether Branch is checked with no branch selected, which TortoiseGit's hosts refuse.
     */
    [[nodiscard]] bool isBranchEmpty() const;

    /**
     * Ported from SelectRef: checks the radio button matching a ref and selects it in its combo box. Anything
     * that is no branch or tag goes into the commit field.
     * @param refName The ref or commit.
     * @param refNameIsPossiblyNotFullName Whether @p refName may be a short name, to resolve first.
     */
    void selectRef(QString refName, bool refNameIsPossiblyNotFullName = true);

    /**
     * @returns The current branch once loaded, as CGit::GetCurrentBranch gives it, for the host's
     *          IDC_CURRENTBRANCH field.
     */
    [[nodiscard]] const QString& currentBranch() const;

    /**
     * @returns The HEAD radio button, hidden until a host whose resource has IDC_RADIO_HEAD shows it.
     */
    [[nodiscard]] QRadioButton *headRadio() const;

    /**
     * @returns The branch combo box.
     */
    [[nodiscard]] QComboBox *branchCombo() const;

    /**
     * @returns The tag combo box.
     */
    [[nodiscard]] QComboBox *tagCombo() const;

    /**
     * @returns The commit field.
     */
    [[nodiscard]] QComboBox *versionCombo() const;

    /**
     * @returns The "..." button that picks any ref in Browse References.
     */
    [[nodiscard]] QPushButton *browseRefButton() const;

    /**
     * @returns The button that picks a commit from the log.
     */
    [[nodiscard]] QPushButton *showLogButton() const;

Q_SIGNALS:
    /**
     * The choice changed (OnVersionChanged).
     */
    void versionChanged();

    /**
     * The branches and tags have been loaded (or failed to load) and the pending ref selected.
     */
    void loaded();

private:
    void onChooseRadio();
    void onBrowseRef();
    void onShowLog();
    void onLoadFinished();
    void updateGui(const BranchList& branches, const QStringList& tags);
    [[nodiscard]] QRadioButton *radio(Choice choice) const;

    Git m_git;
    QRadioButton *m_radioHead;
    QRadioButton *m_radioBranch;
    QRadioButton *m_radioTag;
    QRadioButton *m_radioVersion;
    QComboBox *m_branch;
    QComboBox *m_tags;
    QComboBox *m_version;
    QPushButton *m_browseRef;
    QPushButton *m_showLog;
    std::unique_ptr<GitCall> m_branchCall;
    std::unique_ptr<GitCall> m_tagCall;
    QString m_initialRefName;
    QString m_pendingRefName;
    QString m_currentBranch;
    bool m_notFullName = true;
    bool m_skipCurrentBranch = false;
    bool m_isFirstTimeToSetFocus = false;
};

} // namespace kgt
