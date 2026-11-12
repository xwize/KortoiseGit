// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/MergeAbortDlg.cpp and its IDD_MERGEABORT resource.
 */

#include "mergeabortdlg.hpp"

#include "filediffdlg.hpp"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace kgt
{

MergeAbortDlg::MergeAbortDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_merge(new QRadioButton(i18n("M&erge: Resets the index and try to reconstruct the pre-merge state"), this))
    , m_mixed(new QRadioButton(i18n("&Mixed: Leave working tree untouched, reset index"), this))
    , m_hard(new QRadioButton(i18n("&Hard: Reset working tree and index (discard all local changes)"), this))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Abort Merge", QDir::toNativeSeparators(workingTree)));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(i18n("In order to abort a merge progress a reset (to HEAD) is needed."), this));

    auto *group = new QGroupBox(i18n("Reset Type"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(m_merge);
    groupLayout->addWidget(m_mixed);
    groupLayout->addWidget(m_hard);
    layout->addWidget(group);

    auto *showModifiedFiles = new QPushButton(i18n("Show modified files in &working tree"), this);
    showModifiedFiles->setAutoDefault(false);
    connect(showModifiedFiles, &QPushButton::clicked, this, [this, workingTree] {
        // OnBnClickedShowModifiedFiles
        FileDiffDlg dlg(workingTree, this);
        dlg.setDiff({}, QStringLiteral("HEAD"), FileDiffDlg::workingCopyRef());
        dlg.exec();
    });
    layout->addWidget(showModifiedFiles);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // m_ResetType starts at 0, the merge reset
    m_merge->setChecked(true);
}

ResetType MergeAbortDlg::resetType() const
{
    if (m_mixed->isChecked()) {
        return ResetType::Mixed;
    }
    if (m_hard->isChecked()) {
        return ResetType::Hard;
    }
    return ResetType::Merge;
}

void MergeAbortDlg::setResetType(ResetType type)
{
    switch (type) {
    case ResetType::Mixed:
        m_mixed->setChecked(true);
        break;
    case ResetType::Hard:
        m_hard->setChecked(true);
        break;
    case ResetType::Soft:
        Q_ASSERT(false); // Not one of the dialog's choices
        [[fallthrough]];
    case ResetType::Merge:
        m_merge->setChecked(true);
        break;
    }
}

} // namespace kgt
