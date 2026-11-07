// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2021, 2023-2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AddDlg.cpp

#include "adddlg.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "gitstatuslistctrl.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace
{

using namespace kgt;
using Show = GitStatusListModel::Show;

/**
 * @param path An absolute path.
 * @returns It with symbolic links resolved, as git reports the working tree; unchanged if it doesn't exist.
 */
QString resolved(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace

namespace kgt
{

AddDlg::AddDlg(const QString& workingTree, const QStringList& paths, QWidget *parent)
    : QDialog(parent)
    , m_addListCtrl(new GitStatusListCtrl(this))
    , m_selectAll(new QCheckBox(i18n("Select/deselect &all"), this))
    , m_includeIgnored(new QCheckBox(i18n("Include &ignored files"), this))
{
    Q_ASSERT(!paths.isEmpty());
    // CAppUtils::FormatWindowTitle: path, dialog name, application
    setWindowTitle(QStringLiteral("%1 - %2 - %3").arg(QDir::toNativeSeparators(commonRoot(paths)), i18n("Add"), i18n("KortoiseGit")));

    // GITSLC_COLEXT: Path, Extension and Status; there are no line counts for files git doesn't track
    m_addListCtrl->setColumnHidden(GitStatusListModel::LinesAddedColumn, true);
    m_addListCtrl->setColumnHidden(GitStatusListModel::LinesRemovedColumn, true);
    m_addListCtrl->setWorkingTree(workingTree);
    m_addListCtrl->setPathFilter(relativePaths(workingTree, paths));
    m_addListCtrl->setSelectButton(m_selectAll);
    m_addListCtrl->setEmptyString(
        i18n("There is nothing to add.\nAll the files and folders are either under version control,\nhave been ignored or the global ignore configuration "
             "setting."));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AddDlg::reject);

    auto *options = new QHBoxLayout;
    options->addWidget(m_selectAll);
    options->addWidget(m_includeIgnored);
    options->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_addListCtrl);
    layout->addLayout(options);
    layout->addWidget(buttons);

    connect(m_includeIgnored, &QCheckBox::toggled, this, &AddDlg::refresh);
    connect(m_addListCtrl, &GitStatusListCtrl::checkedCountChanged, this, &AddDlg::updateOkButton);
    connect(m_addListCtrl, &GitStatusListCtrl::loaded, this, &AddDlg::updateOkButton);
    connect(m_addListCtrl, &GitStatusListCtrl::loadFailed, this, &AddDlg::updateOkButton);
    // CAddDlg::PreTranslateMessage: Ctrl+Enter is OK and F5 reloads, wherever the focus is
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this), &QShortcut::activated, this, &AddDlg::accept);
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, m_addListCtrl, &GitStatusListCtrl::reload);

    resize(600, 400);
    refresh();
}

AddDlg::~AddDlg() = default;

QStringList AddDlg::relativePaths(const QString& workingTree, const QStringList& paths)
{
    const QDir root(workingTree);
    QStringList relative;
    for (const QString& path : paths) {
        const QString inTree = root.relativeFilePath(resolved(path));
        relative.append(inTree.isEmpty() ? QStringLiteral(".") : inTree);
    }
    return relative;
}

QString AddDlg::commonRoot(const QStringList& paths)
{
    Q_ASSERT(!paths.isEmpty());
    if (paths.size() == 1) {
        return QDir::cleanPath(paths.constFirst());
    }
    QStringList common = QDir::cleanPath(paths.constFirst()).split(QLatin1Char('/'));
    for (const QString& path : paths) {
        const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'));
        const auto mismatch = std::ranges::mismatch(common, parts);
        common.erase(mismatch.in1, common.end());
    }
    const QString root = common.join(QLatin1Char('/'));
    return root.isEmpty() ? QStringLiteral("/") : root;
}

GitStatusListCtrl *AddDlg::listCtrl() const
{
    return m_addListCtrl;
}

bool AddDlg::isAdding() const
{
    return m_addCall != nullptr;
}

void AddDlg::accept()
{
    if (m_addListCtrl->isBusy() || isAdding() || !m_okButton->isEnabled()) {
        return;
    }
    QStringList paths;
    for (const GitStatusListEntry& entry : m_addListCtrl->checkedEntries()) {
        paths.append(entry.path);
    }
    // AddProgressCommand without libgit2: force, so the ignored files the user checked are added too
    MassiveGitTask task({QStringLiteral("add"), QStringLiteral("-f")});
    task.setPaths(paths);
    m_addCall = task.runAsync(Git(m_addListCtrl->workingTree()));
    connect(m_addCall.get(), &GitCall::finished, this, &AddDlg::onAddFinished);
    setCursor(Qt::BusyCursor);
    updateOkButton();
}

void AddDlg::reject()
{
    if (isAdding()) {
        return;
    }
    QDialog::reject();
}

void AddDlg::refresh()
{
    GitStatusListModel::ShowFlags show = Show::Unversioned;
    if (m_includeIgnored->isChecked()) {
        show |= Show::Ignored;
    }
    // Every unversioned file starts checked; ignored ones don't
    m_addListCtrl->setShowFlags(show, Show::Unversioned);
    m_addListCtrl->reload();
    updateOkButton();
}

void AddDlg::updateOkButton()
{
    // CGitStatusListCtrl::SetConfirmButton: OK needs something checked
    m_okButton->setEnabled(!m_addListCtrl->isBusy() && !isAdding() && !m_addListCtrl->checkedEntries().isEmpty());
}

void AddDlg::onAddFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_addCall.release();
    call->deleteLater();
    unsetCursor();
    try {
        std::ignore = call->output();
    } catch (const RuntimeError& e) {
        updateOkButton();
        // Window-modal without a nested event loop, so the list keeps working behind it
        auto *box = new QMessageBox(QMessageBox::Critical, i18n("KortoiseGit"), e.error().message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        m_addListCtrl->reload();
        Q_EMIT addFailed(e.error().message);
        return;
    }
    QDialog::accept();
}

} // namespace kgt
