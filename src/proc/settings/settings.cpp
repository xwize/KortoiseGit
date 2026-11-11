// SPDX-FileCopyrightText: 2003-2008, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2018, 2020-2021, 2024, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/Settings.cpp. CTreePropSheet's tree of pages is KPageDialog's
 * tree face.
 */

#include "settings/settings.hpp"

#include "settings/setsaveddatapage.hpp"
#include "settings/settinggitconfig.hpp"
#include "settings/settinggitremote.hpp"
#include "settings/settingsprogsdiff.hpp"
#include "settings/settingsprogsmerge.hpp"
#include "settings/settingsproppage.hpp"

#include <KLocalizedString>
#include <KPageWidgetModel>

#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QPushButton>

#include <algorithm>
#include <tuple>
#include <utility>

namespace kgt
{

Settings::Settings(const QString& repository, QWidget *parent)
    : KPageDialog(parent)
{
    // CSettings::OnInitDialog: IDS_PROC_SETTINGS_TITLE, through CAppUtils::SetWindowTitle in a repository
    setWindowTitle(repository.isEmpty() ? i18nc("@title:window", "Settings - KortoiseGit")
                                        : i18nc("@title:window %1 is a folder", "%1 - Settings", QDir::toNativeSeparators(repository)));
    setFaceType(KPageDialog::Tree); // SetTreeViewMode
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);

    addPropPages(repository);

    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        std::ignore = applyAll();
    });
    updateApplyButton();
}

void Settings::setDefaultPage(const QString& pageName)
{
    const auto found = std::ranges::find(m_pages, pageName, &PropPage::pageName);
    if (found == m_pages.cend()) {
        return;
    }
    if (found->page == m_gitRemote) {
        m_gitRemote->setNoFetch(true);
    }
    setCurrentPage(found->item);
}

QStringList Settings::pageNames() const
{
    QStringList names;
    for (const PropPage& propPage : m_pages) {
        names.append(propPage.pageName);
    }
    return names;
}

QString Settings::currentPageName() const
{
    const auto found = std::ranges::find(m_pages, currentPage(), &PropPage::item);
    return found == m_pages.cend() ? QString() : found->pageName;
}

SettingsPropPage *Settings::page(const QString& pageName) const
{
    const auto found = std::ranges::find(m_pages, pageName, &PropPage::pageName);
    return found == m_pages.cend() ? nullptr : found->page;
}

void Settings::accept()
{
    if (applyAll()) {
        KPageDialog::accept();
    }
}

void Settings::addPropPages(const QString& repository)
{
    // Only the pages of what is ported so far, each where CSettings::AddPropPages has it. The pages before "gitconfig"
    // (General and what is under it) and the others in between configure things we don't have yet
    KPageWidgetItem *gitConfig = addPropPage(new SettingGitConfig(repository, this),
                                             {.pageName = QStringLiteral("gitconfig"), .title = i18n("Git"), .iconName = QStringLiteral("vcs-normal")});
    // hasLocalRepo
    if (!repository.isEmpty()) {
        m_gitRemote = new SettingGitRemote(repository, this);
        addPropPage(m_gitRemote,
                    {.pageName = QStringLiteral("gitremote"), .title = i18n("Remote"), .iconName = QStringLiteral("network-server"), .parentItem = gitConfig});
        // CGitSettings::InitGitSettings adds the repository to the Git page's caption
        gitConfig->setHeader(i18nc("%1 is a page's title, %2 a folder", "%1 - %2", i18n("Git"), QDir::toNativeSeparators(repository)));
    }

    KPageWidgetItem *progsDiff =
        addPropPage(new SettingsProgsDiff(this), {.pageName = QStringLiteral("diff"), .title = i18n("Diff Viewer"), .iconName = QStringLiteral("vcs-diff")});
    addPropPage(new SettingsProgsMerge(this),
                {.pageName = QStringLiteral("merge"), .title = i18n("Merge Tool"), .iconName = QStringLiteral("vcs-merge"), .parentItem = progsDiff});

    addPropPage(new SetSavedDataPage(this), {.pageName = QStringLiteral("save"), .title = i18n("Saved Data"), .iconName = QStringLiteral("document-save")});
}

KPageWidgetItem *Settings::addPropPage(SettingsPropPage *propPage, const PageInfo& info)
{
    Q_ASSERT(std::ranges::find(m_pages, info.pageName, &PropPage::pageName) == m_pages.cend()); // pageName must be unique
    KPageWidgetItem *item = info.parentItem == nullptr ? addPage(propPage, info.title) : addSubPage(info.parentItem, propPage, info.title);
    item->setIcon(QIcon::fromTheme(info.iconName));
    connect(propPage, &SettingsPropPage::modifiedChanged, this, &Settings::updateApplyButton);
    m_pages.append({.pageName = info.pageName, .page = propPage, .item = item});
    return item;
}

bool Settings::applyAll()
{
    for (const PropPage& propPage : std::as_const(m_pages)) {
        if (!propPage.page->apply()) {
            setCurrentPage(propPage.item);
            return false;
        }
    }
    return true;
}

void Settings::updateApplyButton()
{
    button(QDialogButtonBox::Apply)->setEnabled(std::ranges::any_of(m_pages, &SettingsPropPage::isModified, &PropPage::page));
}

} // namespace kgt
