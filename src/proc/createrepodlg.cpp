// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CreateRepoDlg.cpp and its IDD_CREATEREPO resource.
 */

#include "createrepodlg.hpp"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace kgt
{

CreateRepoDlg::CreateRepoDlg(const QString& folder, QWidget *parent)
    : QDialog(parent)
    , m_bare(new QCheckBox(i18n("Make it Bare (No working directories)"), this))
{
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Git Init", QDir::toNativeSeparators(folder)));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_bare);

    auto *description = new QLabel(i18n("If you plan to work inside this folder, leave this unchecked. Typically a bare repo can only have "
                                        "changes 'pushed' to it. (By convention, a bare repo folder should have a name that ends with '.git')"),
                                   this);
    description->setWordWrap(true);
    // Indented under the checkbox, as in TortoiseGit
    const int indent = style()->pixelMetric(QStyle::PM_IndicatorWidth) + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
    description->setContentsMargins(indent, 0, 0, 0);
    layout->addWidget(description);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // A folder named like "project.git" is probably meant to be a bare repository
    m_bare->setChecked(folder.endsWith(QLatin1String(".git")));
    resize(480, sizeHint().height());
}

bool CreateRepoDlg::isBare() const
{
    return m_bare->isChecked();
}

void CreateRepoDlg::setBare(bool bare)
{
    m_bare->setChecked(bare);
}

} // namespace kgt
