// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logdlg.hpp"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QLabel>
#include <QListView>
#include <QStringListModel>
#include <QVBoxLayout>

namespace kgt
{

LogDlg::LogDlg(const QStringList& paths, QWidget *parent)
    : QDialog(parent)
    , m_pathList(new QListView(this))
{
    setWindowTitle(i18n("Log Messages"));

    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(i18n("The log dialog is not ported yet. It was opened for these paths:"), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_pathList->setModel(new QStringListModel(paths, m_pathList));
    m_pathList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_pathList);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(600, 300);
}

QStringList LogDlg::paths() const
{
    const auto *model = qobject_cast<const QStringListModel *>(m_pathList->model());
    Q_ASSERT(model);
    return model->stringList();
}

} // namespace kgt
