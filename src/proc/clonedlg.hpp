// SPDX-FileCopyrightText: 2008-2017, 2019-2021, 2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CloneDlg.h (CCloneDlg): asks what to clone where.
 */

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace kgt
{

/**
 * What CloneDlg asks for, the members of CCloneDlg that CloneCommand reads.
 */
struct CloneOptions {
    QString url; // m_URL: the repository to clone
    QString directory; // m_Directory: the folder to clone into
    bool depth = false; // m_bDepth: make a shallow clone
    int depthValue = 1; // m_nDepth: how many commits a shallow clone keeps
    bool recursive = false; // m_bRecursive: --recursive
    bool bare = false; // m_bBare: --bare
    bool noCheckout = false; // m_bNoCheckout: --no-checkout
    bool branch = false; // m_bBranch: check out another branch than the remote's HEAD
    QString branchName; // m_strBranch
    bool origin = false; // m_bOrigin: name the remote other than "origin"
    QString originName; // m_strOrigin
};

/**
 * The "Git clone" dialog: URL (prefilled from the clipboard) and Directory, which follows the URL's last component,
 * then Depth, Recursive, Clone into Bare Repo, No Checkout, Branch and Origin Name. The git-svn and PuTTY key parts
 * of TortoiseGit's dialog are left out.
 */
class CloneDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param directory The folder to clone into, to begin with; the repository's name is added to it as the URL
     *                  is typed.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit CloneDlg(const QString& directory, QWidget *parent = nullptr);

    /**
     * @returns What the dialog shows now.
     */
    [[nodiscard]] CloneOptions options() const;

    /**
     * Sets the URL, as typing it would, so the directory follows it.
     * @param url The repository to clone.
     */
    void setUrl(const QString& url);

    /**
     * Ported from CCloneDlg::OnCbnEditchangeUrlcombo: the folder name a clone of a URL gets, its last path
     * component without ".git".
     * @param url A URL, an scp-like user\@host:path or a local path.
     * @returns The name.
     */
    [[nodiscard]] static QString moduleName(const QString& url);

    /**
     * Checks the choices as TortoiseGit's OnOK does, and closes the dialog if they are fine.
     */
    void accept() override;

private:
    /**
     * Ported from OnCbnEditchangeUrlcombo: replaces the directory's last component, if it came from the previous
     * URL, with the name taken from the new one.
     * @param text The URL as typed.
     */
    void onUrlChanged(const QString& text);

    /**
     * Enables the fields that go with each checkbox, and keeps Bare apart from the options it excludes
     * (OnBnClickedCheckBare, OnBnClickedCheckRecursive, OnBnClickedCheckOrigin and the like).
     */
    void updateStates();

    /**
     * Lets the user pick a local repository as the URL.
     */
    void browseUrl();

    /**
     * Lets the user pick the directory.
     */
    void browseDirectory();

    QComboBox *m_url;
    QLineEdit *m_directory;
    QCheckBox *m_depth;
    QSpinBox *m_depthValue;
    QCheckBox *m_recursive;
    QCheckBox *m_bare;
    QCheckBox *m_noCheckout;
    QCheckBox *m_branch;
    QLineEdit *m_branchName;
    QCheckBox *m_origin;
    QLineEdit *m_originName;
    QString m_oldUrl; // The URL the directory was last updated for
    QString m_moduleName; // The name the directory got from m_oldUrl
};

} // namespace kgt
