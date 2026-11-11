// SPDX-FileCopyrightText: 2012-2017, 2019-2020, 2024-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/DeleteRemoteTagDlg.cpp and its IDD_DELETEREMOTETAG resource

#include "deleteremotetagdlg.hpp"

#include "apputils.hpp"
#include "progressdlg.hpp"

#include "git/gitcall.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowConfig>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace
{

using namespace Qt::StringLiterals;

constexpr QSize initialSize(380, 300);
// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

KConfigGroup windowSettings()
{
    return kgt::AppUtils::resizableState(QStringLiteral("DeleteRemoteTagDlg"));
}

} // namespace

namespace kgt
{

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): A repository and a remote, as everywhere
DeleteRemoteTagDlg::DeleteRemoteTagDlg(const QString& workingTree, const QString& remote, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
    , m_remote(remote)
    , m_tags(new QTreeView(this))
    , m_model(new QStandardItemModel(this))
    , m_selectAll(new QCheckBox(i18n("Select/deselect &all"), this))
{
    setWindowTitle(i18nc("@title:window", "Delete remote tag"));

    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout;
    auto *remoteEdit = new QLineEdit(m_remote, this);
    remoteEdit->setReadOnly(true);
    grid->addWidget(new QLabel(i18n("Remote:"), this), 0, 0);
    grid->addWidget(remoteEdit, 0, 1);

    // A one-column list with no header (LVS_NOCOLUMNHEADER), several tags selectable
    m_model->setHorizontalHeaderLabels({i18n("Tag")});
    m_tags->setModel(m_model);
    m_tags->setHeaderHidden(true);
    m_tags->setRootIsDecorated(false);
    m_tags->setUniformRowHeights(true);
    m_tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *tagsLabel = new QLabel(i18n("&Tags:"), this);
    tagsLabel->setBuddy(m_tags);
    grid->addWidget(tagsLabel, 1, 0, Qt::AlignTop);
    grid->addWidget(m_tags, 1, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(1, 1);
    layout->addLayout(grid, 1);

    auto *bottomRow = new QHBoxLayout;
    m_selectAll->setTristate(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_delete = buttons->addButton(i18n("&Delete"), QDialogButtonBox::AcceptRole);
    m_delete->setDefault(true);
    m_delete->setEnabled(false);
    bottomRow->addWidget(m_selectAll);
    bottomRow->addStretch();
    bottomRow->addWidget(buttons);
    layout->addLayout(bottomRow);

    connect(m_selectAll, &QCheckBox::clicked, this, &DeleteRemoteTagDlg::onSelectAllClicked);
    connect(m_tags->selectionModel(), &QItemSelectionModel::selectionChanged, this, &DeleteRemoteTagDlg::onSelectionChanged);
    connect(m_delete, &QPushButton::clicked, this, &DeleteRemoteTagDlg::onDeleteClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // PreTranslateMessage
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &DeleteRemoteTagDlg::refresh);

    // EnableSaveRestore. KWindowConfig needs the native window, which show() would otherwise create
    resize(initialSize);
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());

    refresh();
}

DeleteRemoteTagDlg::~DeleteRemoteTagDlg() = default;

void DeleteRemoteTagDlg::refresh()
{
    // CDeleteRemoteTagDlg::Refresh. TortoiseGit waits behind a modal "Loading..." window; here the list says so
    // and the dialog stays responsive while git ls-remote talks to the remote
    m_model->removeRows(0, m_model->rowCount());
    m_selectAll->setCheckState(Qt::Unchecked);
    m_selectAll->setEnabled(false);
    m_delete->setEnabled(false);
    auto *loading = new QStandardItem(i18n("Loading..."));
    loading->setFlags(Qt::NoItemFlags);
    m_model->appendRow(loading);

    m_tagsCall = m_git.runAsync(Git::remoteTagsArguments(m_remote), RunMode::ReadOnly);
    connect(m_tagsCall.get(), &GitCall::finished, this, &DeleteRemoteTagDlg::onLoadFinished);
}

bool DeleteRemoteTagDlg::isLoading() const
{
    return m_tagsCall != nullptr;
}

void DeleteRemoteTagDlg::onLoadFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_tagsCall.release();
    call->deleteLater();
    m_model->removeRows(0, m_model->rowCount());

    QStringList tags;
    try {
        tags = Git::parseRemoteTags(call->output());
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, i18n("Could not retrieve remote tags.") + u'\n' + e.error().message, QString(), noNotify);
    }
    for (const QString& tag : std::as_const(tags)) {
        // A peeled annotated tag names the same tag again
        if (!tag.endsWith("^{}"_L1)) {
            m_model->appendRow(new QStandardItem(tag));
        }
    }
    m_selectAll->setEnabled(m_model->rowCount() > 0);
    onSelectionChanged();
    Q_EMIT refreshed();
}

QTreeView *DeleteRemoteTagDlg::tagList() const
{
    return m_tags;
}

QStringList DeleteRemoteTagDlg::tags() const
{
    QStringList tags;
    if (isLoading()) {
        return tags;
    }
    for (int row = 0; row < m_model->rowCount(); ++row) {
        tags.append(m_model->item(row)->text());
    }
    return tags;
}

QPushButton *DeleteRemoteTagDlg::deleteButton() const
{
    return m_delete;
}

QCheckBox *DeleteRemoteTagDlg::selectAllBox() const
{
    return m_selectAll;
}

QStringList DeleteRemoteTagDlg::selectedTags() const
{
    QModelIndexList rows = m_tags->selectionModel()->selectedRows();
    std::ranges::sort(rows, {}, &QModelIndex::row);
    QStringList tags;
    for (const QModelIndex& index : std::as_const(rows)) {
        tags.append(index.data().toString());
    }
    return tags;
}

void DeleteRemoteTagDlg::onSelectAllClicked()
{
    // OnBnClickedSelectall: it is not at all useful to manually place the checkbox into the indeterminate state;
    // force it to unchecked
    if (m_selectAll->checkState() == Qt::PartiallyChecked) {
        m_selectAll->setCheckState(Qt::Unchecked);
    }
    if (m_selectAll->checkState() == Qt::Unchecked) {
        m_tags->clearSelection();
    } else {
        m_tags->selectAll();
    }
}

void DeleteRemoteTagDlg::onSelectionChanged()
{
    // OnSelchangeTags
    const qsizetype selectedCount = isLoading() ? 0 : m_tags->selectionModel()->selectedRows().size();
    m_delete->setEnabled(selectedCount > 0);
    if (selectedCount == 0) {
        m_selectAll->setCheckState(Qt::Unchecked);
    } else if (selectedCount < m_model->rowCount()) {
        m_selectAll->setCheckState(Qt::PartiallyChecked);
    } else {
        m_selectAll->setCheckState(Qt::Checked);
    }
}

void DeleteRemoteTagDlg::onDeleteClicked()
{
    // OnBnClickedOk
    const QStringList tags = selectedTags();
    if (tags.isEmpty() || isLoading()) {
        return;
    }
    const QString message = tags.size() > 1 ? i18n("Do you really want to permanently delete the %1 selected refs? It can NOT be recovered!", tags.size())
                                            : i18n("Do you really want to delete \"%1\"?", tags.constFirst());
    if (KMessageBox::questionTwoActions(this, message, QString(), KGuiItem(i18n("&Delete")), KGuiItem(i18n("A&bort")), QString(), noNotify)
        != KMessageBox::PrimaryAction) {
        return;
    }

    QStringList refs;
    for (const QString& tag : tags) {
        refs.append("refs/tags/"_L1 + tag);
    }
    // TortoiseGit waits behind a "Deleting remote refs..." window and reports a failure in a message box;
    // ProgressDlg shows git's output, and any error, as it runs
    ProgressDlg progress(m_git.workingDirectory(), this);
    progress.addGitCmd(Git::deleteRemoteRefsArguments(m_remote, refs));
    progress.exec();
    refresh();
}

void DeleteRemoteTagDlg::done(int result)
{
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

} // namespace kgt
