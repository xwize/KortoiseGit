// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2021, 2023-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/HistoryDlg.cpp

#include "historydlg.hpp"

#include "apputils.hpp"
#include "reghistory.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KWindowConfig>

#include <QDialogButtonBox>
#include <QListWidget>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace
{

KConfigGroup windowSettings()
{
    return kgt::AppUtils::resizableState(QStringLiteral("HistoryDlg"));
}

} // namespace

namespace kgt
{

HistoryDlg::HistoryDlg(RegHistory& history, QWidget *parent)
    : QDialog(parent)
    , m_history(&history)
    , m_list(new QListWidget(this))
{
    setWindowTitle(i18n("Log History"));
    auto *layout = new QVBoxLayout(this);

    // OnInitDialog: each entry on one line; the list scrolls sideways for long ones
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setWordWrap(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (qsizetype i = 0; i < m_history->count(); ++i) {
        QString entry = m_history->entry(i);
        entry.remove(QLatin1Char('\r'));
        entry.replace(QLatin1Char('\n'), QLatin1Char(' '));
        m_list->addItem(entry);
    }
    layout->addWidget(m_list);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &HistoryDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &HistoryDlg::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HistoryDlg::onDoubleClicked);
    connect(new QShortcut(QKeySequence::Delete, m_list, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, &HistoryDlg::onDelete);
    m_list->setFocus();

    // EnableSaveRestore(L"HistoryDlg"). KWindowConfig needs the native window, which show() would otherwise create
    resize(400, 250);
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());
}

const QString& HistoryDlg::selectedText() const
{
    return m_selectedText;
}

QListWidget *HistoryDlg::list() const
{
    return m_list;
}

void HistoryDlg::accept()
{
    // OnBnClickedOk: the selected entries in the list's order, separated by blank lines
    QList<int> rows;
    for (const QListWidgetItem *item : m_list->selectedItems()) {
        rows.append(m_list->row(item));
    }
    std::ranges::sort(rows);
    m_selectedText.clear();
    for (const int row : std::as_const(rows)) {
        if (!m_selectedText.isEmpty()) {
            m_selectedText += QLatin1String("\n\n");
        }
        m_selectedText += m_history->entry(row);
    }
    QDialog::accept();
}

void HistoryDlg::done(int result)
{
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

void HistoryDlg::onDoubleClicked()
{
    const int pos = m_list->currentRow();
    if (pos < 0) {
        m_selectedText.clear();
        return;
    }
    m_selectedText = m_history->entry(pos);
    QDialog::accept();
}

void HistoryDlg::onDelete()
{
    const int pos = m_list->currentRow();
    if (pos < 0) {
        return;
    }
    delete m_list->takeItem(pos);
    m_list->setCurrentRow(std::min(pos, m_list->count() - 1));
    m_history->removeEntry(pos);
    m_history->save();
}

} // namespace kgt
