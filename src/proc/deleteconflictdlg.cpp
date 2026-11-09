// SPDX-FileCopyrightText: 2009-2020, 2024-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/DeleteConflictDlg.cpp and its IDD_RESOLVE_CONFLICT resource, which
 * CAppUtils::ConflictEdit fills in and acts on. The Help button is left out.
 */

#include "deleteconflictdlg.hpp"

#include "apputils.hpp"
#include "gitdiff.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <tuple>

namespace
{

constexpr int refEditWidthChars = 47; // IDC_FROMHASH is 188 dialog units wide, at 4 units to a character

/**
 * The widgets of one side of the conflict, which IDD_RESOLVE_CONFLICT lays out twice.
 */
struct SideWidgets {
    QPushButton *log; // IDC_LOG, IDC_LOG2
    QPushButton *diff; // IDC_SHOWDIFF, IDC_SHOWDIFF2
};

/**
 * The text of one side of the conflict.
 */
struct SideText {
    QString ref; // The side's title
    QString status; // What the side did to the file
};

/**
 * Adds one side's two rows to the group's grid.
 * @param grid The grid.
 * @param row The first of the two rows.
 * @param text What the side shows.
 * @returns The side's buttons.
 */
SideWidgets addSide(QGridLayout *grid, int row, const SideText& text)
{
    auto *group = grid->parentWidget();
    auto *refEdit = new QLineEdit(text.ref, group);
    refEdit->setReadOnly(true);
    refEdit->setCursorPosition(0);
    refEdit->setMinimumWidth(refEdit->fontMetrics().averageCharWidth() * refEditWidthChars);
    auto *log = new QPushButton(i18n("Show log"), group);
    auto *diff = new QPushButton(i18n("Show &changes"), group);
    grid->addWidget(refEdit, row, 0);
    grid->addWidget(log, row, 1);
    grid->addWidget(new QLabel(text.status, group), row + 1, 0);
    grid->addWidget(diff, row + 1, 1);
    return {.log = log, .diff = diff};
}

} // namespace

namespace kgt
{

DeleteConflictDlg::DeleteConflictDlg(const DeleteConflictParams& params, QWidget *parent)
    : QDialog(parent)
    , m_params(params)
{
    // CAppUtils::SetWindowTitle
    const QString absolutePath = QDir(m_params.workingTree).filePath(m_params.file);
    setWindowTitle(i18nc("@title:window %1 is a file", "%1 - Conflict", QDir::toNativeSeparators(absolutePath)));

    auto *group = new QGroupBox(i18n("Delete/modify merge conflict"), this);
    auto *grid = new QGridLayout(group);
    grid->setColumnStretch(0, 1);
    grid->addWidget(new QLabel(m_params.file, group), 0, 0, 1, 2);
    const SideWidgets local = addSide(grid, 1, {.ref = m_params.localRef, .status = m_params.localStatus});
    const SideWidgets remote = addSide(grid, 3, {.ref = m_params.remoteRef, .status = m_params.remoteStatus});

    // OnInitDialog. A base version that couldn't be written can't be compared
    const bool canDiff = m_params.showModifiedButton && !m_params.fileBaseVersion.isEmpty();
    local.diff->setVisible(canDiff && m_params.diffMine);
    remote.diff->setVisible(canDiff && !m_params.diffMine);
    local.log->setVisible(!m_params.localRef.isEmpty());
    remote.log->setVisible(!m_params.remoteRef.isEmpty());

    auto *modify = new QPushButton(m_params.showModifiedButton ? i18n("Modified") : i18n("Created"), this);
    auto *remove = new QPushButton(i18n("Delete"), this);
    auto *abort = new QPushButton(i18n("Abort"), this);
    abort->setDefault(true);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(modify);
    buttons->addWidget(remove);
    buttons->addWidget(abort);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addLayout(buttons);

    connect(local.log, &QPushButton::clicked, this, [this] {
        showLog(m_params.localHash);
    });
    connect(remote.log, &QPushButton::clicked, this, [this] {
        showLog(m_params.remoteHash);
    });
    connect(local.diff, &QPushButton::clicked, this, &DeleteConflictDlg::showDiff);
    connect(remote.diff, &QPushButton::clicked, this, &DeleteConflictDlg::showDiff);
    connect(modify, &QPushButton::clicked, this, [this] {
        m_isDelete = false;
        accept();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        m_isDelete = true;
        accept();
    });
    connect(abort, &QPushButton::clicked, this, &QDialog::reject);

    abort->setFocus();
}

bool DeleteConflictDlg::isDelete() const
{
    return m_isDelete;
}

void DeleteConflictDlg::showLog(const QString& hash) const
{
    // CDeleteConflictDlg::ShowLog. Without a hash the side is HEAD, which is where the log starts anyway
    QStringList arguments{
        QStringLiteral("--command"),
        QStringLiteral("log"),
        QStringLiteral("--path"),
        QDir(m_params.workingTree).filePath(m_params.file),
    };
    if (!hash.isEmpty()) {
        arguments << QStringLiteral("--range") << hash;
    }
    std::ignore = AppUtils::runTortoiseGitProc(arguments);
}

void DeleteConflictDlg::showDiff()
{
    // CDeleteConflictDlg::OnBnClickedShowdiff
    const QString file = QDir(m_params.workingTree).filePath(m_params.file);
    const QStringList arguments = GitDiff::extDiffArguments(m_params.fileBaseVersion, file);
    std::ignore = GitDiff::start(this, {.workingDirectory = m_params.workingTree, .arguments = arguments});
}

} // namespace kgt
