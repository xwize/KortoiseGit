// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2013, 2015-2020, 2023-2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/ResolveDlg.cpp

#include "resolvedlg.hpp"

#include "adddlg.hpp"
#include "apputils.hpp"
#include "gitstatuslistctrl.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KWindowConfig>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>

namespace
{

using namespace kgt;
using Show = GitStatusListModel::Show;
using ContextMenu = GitStatusListCtrl::ContextMenu;

constexpr int initialWidth = 600;
constexpr int initialHeight = 400;

/**
 * @returns Where the window size is kept (EnableSaveRestore).
 */
KConfigGroup windowSettings()
{
    return kgt::AppUtils::resizableState(QStringLiteral("ResolveDlg"));
}

} // namespace

namespace kgt
{

// TODO: dropping files onto the list adds them to the paths and reloads (CResolveDlg::OnFileDropped)
ResolveDlg::ResolveDlg(const QString& workingTree, const QStringList& paths, QWidget *parent)
    : QDialog(parent)
    , m_resolveListCtrl(new GitStatusListCtrl(this))
    , m_selectAll(new QCheckBox(i18n("Select/deselect &all"), this))
{
    Q_ASSERT(!paths.isEmpty());
    // CAppUtils::SetWindowTitle: path, dialog name, application
    const QString folder = QDir::toNativeSeparators(workingTree);
    setWindowTitle(QStringLiteral("%1 - %2 - %3").arg(folder, i18n("Resolve"), i18n("KortoiseGit")));

    // GITSLC_COLEXT: Path, Extension and Status
    m_resolveListCtrl->setColumnHidden(GitStatusListModel::LinesAddedColumn, true);
    m_resolveListCtrl->setColumnHidden(GitStatusListModel::LinesRemovedColumn, true);
    m_resolveListCtrl->setWorkingTree(workingTree);
    m_resolveListCtrl->setPathFilter(AddDlg::relativePaths(workingTree, paths));
    // GITSLC_POPALL without the entries that make no sense here; of those, we only have Add
    m_resolveListCtrl->setContextMenus(ContextMenu::All & ~GitStatusListCtrl::ContextMenus(ContextMenu::Add));
    m_resolveListCtrl->setSelectButton(m_selectAll);
    // GITSLC_SHOWINEXTERNALS has no counterpart: git status doesn't descend into submodules for us
    m_resolveListCtrl->setShowFlags(Show::Conflicted, Show::Conflicted);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &ResolveDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ResolveDlg::reject);

    auto *options = new QHBoxLayout;
    options->addWidget(m_selectAll);
    options->addStretch();
    options->addWidget(new QLabel(i18n("Reminder: Commit your change after resolve"), this));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_resolveListCtrl);
    layout->addLayout(options);
    layout->addWidget(buttons);

    // SetConfirmButton, and DialogEnableWindow(IDOK, false) in ResolveThread. A reload that one of the list's own
    // actions needs (OnSVNStatusListCtrlNeedsRefresh) is started by the list and ends in the same signals
    connect(m_resolveListCtrl, &GitStatusListCtrl::checkedCountChanged, this, &ResolveDlg::updateOkButton);
    connect(m_resolveListCtrl, &GitStatusListCtrl::loaded, this, &ResolveDlg::updateOkButton);
    connect(m_resolveListCtrl, &GitStatusListCtrl::loadFailed, this, &ResolveDlg::updateOkButton);
    // CResolveDlg::PreTranslateMessage: Ctrl+Enter is OK and F5 reloads, wherever the focus is
    const auto *okShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(okShortcut, &QShortcut::activated, this, &ResolveDlg::accept);
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &ResolveDlg::refresh);

    // EnableSaveRestore. KWindowConfig needs the native window, which show() would otherwise create
    resize(initialWidth, initialHeight);
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());

    refresh();
}

ResolveDlg::~ResolveDlg() = default;

const QList<GitStatusListEntry>& ResolveDlg::pathList() const
{
    return m_pathList;
}

GitStatusListCtrl *ResolveDlg::listCtrl() const
{
    return m_resolveListCtrl;
}

void ResolveDlg::accept()
{
    if (m_resolveListCtrl->isBusy() || !m_okButton->isEnabled()) {
        return;
    }
    // WriteCheckedNamesToPathList
    m_pathList = m_resolveListCtrl->checkedEntries();
    QDialog::accept();
}

void ResolveDlg::done(int result)
{
    if (m_resolveListCtrl->isModifying()) {
        return;
    }
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

void ResolveDlg::refresh()
{
    // CResolveDlg::ResolveThread, as an asynchronous git status
    m_resolveListCtrl->reload();
    updateOkButton();
}

void ResolveDlg::updateOkButton()
{
    // CGitStatusListCtrl::SetConfirmButton: OK needs something checked
    m_okButton->setEnabled(!m_resolveListCtrl->isBusy() && !m_resolveListCtrl->checkedEntries().isEmpty());
}

} // namespace kgt
