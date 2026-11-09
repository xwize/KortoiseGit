// SPDX-FileCopyrightText: 2011-2017, 2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/StashSave.cpp and its IDD_STASH resource, without the Help button.
 */

#include "stashsave.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto noIncludeUntrackedWarning = "NoStashIncludeUntrackedWarning"_L1;

} // namespace

namespace kgt
{

StashSaveDlg::StashSaveDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_message(new QLineEdit(this))
    , m_includeUntracked(new QCheckBox(i18n("include &untracked"), this))
    , m_all(new QCheckBox(i18n("--&all"), this))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Stash", QDir::toNativeSeparators(workingTree)));

    auto *layout = new QVBoxLayout(this);

    auto *messageGroup = new QGroupBox(i18n("Stash &Message"), this);
    auto *messageLayout = new QVBoxLayout(messageGroup);
    messageLayout->addWidget(m_message);
    layout->addWidget(messageGroup);

    auto *optionsGroup = new QGroupBox(i18n("Options"), this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->addWidget(m_includeUntracked);
    optionsLayout->addWidget(m_all);
    layout->addWidget(optionsGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &StashSaveDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // OnBnClickedCheckUntracked and OnBnClickedCheckAll: the two options exclude each other
    connect(m_includeUntracked, &QCheckBox::toggled, m_all, &QWidget::setDisabled);
    connect(m_all, &QCheckBox::toggled, m_includeUntracked, &QWidget::setDisabled);

    // CHorizontalResizableStandAloneDialog: only the width can change
    resize(460, sizeHint().height());
    setFixedHeight(sizeHint().height());
}

void StashSaveDlg::setMessage(const QString& message)
{
    m_message->setText(message);
}

StashSaveOptions StashSaveDlg::options() const
{
    return StashSaveOptions{
        .message = m_message->text(),
        .includeUntracked = m_includeUntracked->isChecked(),
        .all = m_all->isChecked(),
    };
}

void StashSaveDlg::accept()
{
    if (m_includeUntracked->isChecked() && !confirmIncludeUntracked()) {
        return;
    }
    QDialog::accept();
}

bool StashSaveDlg::confirmIncludeUntracked()
{
    if (!KMessageBox::shouldBeShownContinue(noIncludeUntrackedWarning)) {
        return true;
    }

    // Not KMessageBox::warningContinueCancel, whose check box can only say "Do not ask again". createKMessageBox
    // deletes the dialog
    auto *box = new QDialog(this);
    box->setWindowTitle(i18n("KortoiseGit"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, box);
    KGuiItem::assign(buttons->button(QDialogButtonBox::Yes), KGuiItem(i18n("Continue")));
    KGuiItem::assign(buttons->button(QDialogButtonBox::No), KGuiItem(i18n("A&bort")));
    // ShowCheck's default button is the second one
    buttons->button(QDialogButtonBox::No)->setDefault(true);

    const QString dontShowLabel = i18n("Do &not show me this warning again (if Continue is selected)");
    bool dontShowAgain = false;
    const auto answer = KMessageBox::createKMessageBox(box,
                                                       buttons,
                                                       QMessageBox::Warning,
                                                       i18n("You have checked \"include untracked\".\n"
                                                            "This includes a \"git clean -fd\" call, so ignored "
                                                            "files/directories are removed w/o using recycle bin."),
                                                       {},
                                                       dontShowLabel,
                                                       &dontShowAgain,
                                                       KMessageBox::Options());
    if (answer != QDialogButtonBox::Yes) {
        return false;
    }
    // Only the answer "Continue" is stored
    if (dontShowAgain) {
        KMessageBox::saveDontShowAgainContinue(noIncludeUntrackedWarning);
    }
    return true;
}

} // namespace kgt
