// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2013, 2016-2020, 2023-2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/RevertDlg.cpp

#include "revertdlg.hpp"

#include "adddlg.hpp"
#include "gitstatuslistctrl.hpp"
#include "progresscommands/revertprogresscommand.hpp"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace kgt
{

using Show = GitStatusListModel::Show;

RevertDlg::RevertDlg(const QString& workingTree, const QStringList& paths, QWidget *parent)
    : QDialog(parent)
    , m_revertList(new GitStatusListCtrl(this))
    , m_selectAll(new QCheckBox(i18n("Select/deselect &all"), this))
{
    Q_ASSERT(!paths.isEmpty());
    // CAppUtils::SetWindowTitle: path, dialog name, application
    setWindowTitle(QStringLiteral("%1 - %2 - %3").arg(QDir::toNativeSeparators(AddDlg::commonRoot(paths)), i18n("Revert"), i18n("KortoiseGit")));

    // GITSLC_COLEXT | GITSLC_COLSTATUS | GITSLC_COLADD | GITSLC_COLDEL: every column; GITSLC_POPALL less entries
    // that aren't ported
    m_revertList->setWorkingTree(workingTree);
    m_revertList->setPathFilter(AddDlg::relativePaths(workingTree, paths));
    m_revertList->setSelectButton(m_selectAll);
    // Check only the files the user selected directly, and added ones (CRevertDlg::RevertThread)
    m_revertList->setShowFlags(Show::VersionedButNormal | Show::DirectFiles, Show::DirectFiles | Show::Added);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &RevertDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &RevertDlg::reject);

    auto *options = new QHBoxLayout;
    options->addWidget(m_selectAll);
    options->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_revertList);
    layout->addLayout(options);
    layout->addWidget(buttons);

    connect(m_revertList, &GitStatusListCtrl::checkedCountChanged, this, &RevertDlg::updateOkButton);
    connect(m_revertList, &GitStatusListCtrl::loaded, this, &RevertDlg::updateOkButton);
    connect(m_revertList, &GitStatusListCtrl::loadFailed, this, &RevertDlg::updateOkButton);
    // CRevertDlg::PreTranslateMessage: Ctrl+Enter is OK and F5 reloads, wherever the focus is
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this), &QShortcut::activated, this, &RevertDlg::accept);
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, m_revertList, &GitStatusListCtrl::reload);

    resize(560, 440);
    m_revertList->reload();
    updateOkButton();
}

RevertDlg::~RevertDlg() = default;

GitStatusListCtrl *RevertDlg::listCtrl() const
{
    return m_revertList;
}

bool RevertDlg::isReverting() const
{
    return m_revertCommand != nullptr;
}

void RevertDlg::accept()
{
    if (m_revertList->isBusy() || isReverting() || !m_okButton->isEnabled()) {
        return;
    }
    m_revertCommand = std::make_unique<RevertProgressCommand>();
    m_revertCommand->setPathList(m_revertList->checkedEntries());
    connect(m_revertCommand.get(), &RevertProgressCommand::finished, this, &RevertDlg::onRevertFinished);
    setCursor(Qt::BusyCursor);
    updateOkButton();
    m_revertCommand->run(m_revertList->workingTree());
}

void RevertDlg::reject()
{
    if (isReverting()) {
        return;
    }
    QDialog::reject();
}

void RevertDlg::updateOkButton()
{
    // CGitStatusListCtrl::SetConfirmButton: OK needs something checked
    m_okButton->setEnabled(!m_revertList->isBusy() && !isReverting() && !m_revertList->checkedEntries().isEmpty());
}

void RevertDlg::onRevertFinished()
{
    // Deleted from the event loop: it is still emitting, maybe from inside run()
    RevertProgressCommand *command = m_revertCommand.release();
    command->deleteLater();
    unsetCursor();
    if (const std::optional<Error>& error = command->error()) {
        updateOkButton();
        // Window-modal without a nested event loop, so the list keeps working behind it
        auto *box = new QMessageBox(QMessageBox::Critical, i18n("KortoiseGit"), error->message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        m_revertList->reload();
        Q_EMIT revertFailed(error->message);
        return;
    }
    QDialog::accept();
}

} // namespace kgt
