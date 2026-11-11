// SPDX-FileCopyrightText: 2003-2008, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2018, 2020-2021, 2024, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/Settings.h (CSettings): the Settings dialog.
 */

#pragma once

#include <KPageDialog>

#include <QList>
#include <QString>
#include <QStringList>

namespace kgt
{

class SettingGitRemote;
class SettingsPropPage;

/**
 * The "Settings" dialog: a tree of pages on the left and the chosen page on the right, as CSettings in tree view
 * mode. It has the pages for what is ported so far, under TortoiseGit's page names and in its tree positions:
 * "gitconfig" (Git) with "gitremote" (Remote) below it, "diff" (Diff Viewer) with "merge" (Merge Tool) below it,
 * and "save" (Saved Data). The Remote page is there only for a repository.
 */
class Settings : public KPageDialog
{
    Q_OBJECT

public:
    /**
     * @param repository The working tree's root or bare repository to show the settings of; empty for none.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit Settings(const QString& repository, QWidget *parent = nullptr);

    /**
     * m_DefaultPage, as CSettings::OnInitDialog applies it: starts on the page of that name, if there is one. Opened
     * on the Remote page, the dialog doesn't offer to fetch from a remote just added: the Push or Pull dialog that
     * opened it is about to use the remote anyway.
     * @param pageName A page's name, such as "gitremote".
     */
    void setDefaultPage(const QString& pageName);

    /**
     * @returns The names of the pages, in the tree's order.
     */
    [[nodiscard]] QStringList pageNames() const;

    /**
     * @returns The name of the page shown.
     */
    [[nodiscard]] QString currentPageName() const;

    /**
     * @param pageName A page's name.
     * @returns The page, or nullptr if the dialog has none of that name.
     */
    [[nodiscard]] SettingsPropPage *page(const QString& pageName) const;

    /**
     * OK: applies every page and closes the dialog unless one of them refuses.
     */
    void accept() override;

private:
    /**
     * A page and what CSettings::AddPropPage records for it.
     */
    struct PropPage {
        QString pageName; // TortoiseGit's name of the page, as on the command line
        SettingsPropPage *page = nullptr; // Owned by the page widget
        KPageWidgetItem *item = nullptr; // Its entry in the tree
    };

    /**
     * What CSettings::AddPropPage is told about a page.
     */
    struct PageInfo {
        QString pageName; // TortoiseGit's name of the page
        QString title; // The page's caption
        QString iconName; // A theme icon for the tree
        KPageWidgetItem *parentItem = nullptr; // The page it is listed under; nullptr at the top level
    };

    /**
     * Ported from CSettings::AddPropPages.
     * @param repository The working tree's root or bare repository; empty for none.
     */
    void addPropPages(const QString& repository);

    /**
     * Ported from CSettings::AddPropPage.
     * @param propPage The page; the dialog takes it over.
     * @param info Its name, caption, icon and parent.
     * @returns Its entry in the tree.
     */
    KPageWidgetItem *addPropPage(SettingsPropPage *propPage, const PageInfo& info);

    /**
     * Apply, and the first half of OK: MFC's property sheet calls OnApply on each page and stops at one that
     * returns FALSE, which it then shows.
     * @returns Whether every page saved its changes.
     */
    [[nodiscard]] bool applyAll();

    /**
     * Enables Apply while a page has changes to save.
     */
    void updateApplyButton();

    QList<PropPage> m_pages; // m_pPages
    SettingGitRemote *m_gitRemote = nullptr; // m_pGitRemote: null outside a repository
};

} // namespace kgt
