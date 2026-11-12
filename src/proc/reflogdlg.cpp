// SPDX-FileCopyrightText: 2009-2021, 2023-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RefLogDlg.cpp and its IDD_REFLOG resource, without the Help button.
 */

#include "reflogdlg.hpp"

#include "apputils.hpp"
#include "refloglist.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace
{

constexpr KMessageBox::Options noNotify = KMessageBox::Options();
// IDD_REFLOG's size in pixels
constexpr int initialWidth = 760;
constexpr int initialHeight = 420;

/**
 * @returns Where the window size is kept, as EnableSaveRestore(L"RefLogDlg") names it.
 */
KConfigGroup windowSettings()
{
    return kgt::AppUtils::resizableState(QStringLiteral("RefLogDlg"));
}

/**
 * @param parent Parent of the message.
 * @param text What to say.
 */
void showError(QWidget *parent, const QString& text)
{
    KMessageBox::error(parent, text, QString(), noNotify);
}

} // namespace

namespace kgt
{

RefLogDlg::RefLogDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
    , m_chooseRef(new QComboBox(this))
    , m_refList(new RefLogList(workingTree, this))
    , m_clearStash(new QPushButton(i18n("&Clear stash"), this))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - RefLog", QDir::toNativeSeparators(workingTree)));
    setWindowFlag(Qt::WindowMaximizeButtonHint);

    auto *layout = new QVBoxLayout(this);

    auto *refRow = new QHBoxLayout;
    auto *refLabel = new QLabel(i18n("&Ref:"), this);
    refLabel->setBuddy(m_chooseRef);
    refRow->addWidget(refLabel);
    refRow->addWidget(m_chooseRef, 1);
    layout->addLayout(refRow);

    layout->addWidget(m_refList, 1);

    auto *buttonRow = new QHBoxLayout;
    auto *search = new QPushButton(i18n("&Search..."), this);
    search->setAutoDefault(false);
    buttonRow->addWidget(search);
    m_clearStash->setAutoDefault(false);
    m_clearStash->setVisible(false);
    m_clearStash->setEnabled(false);
    buttonRow->addWidget(m_clearStash);
    buttonRow->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        // CRefLogDlg::OnBnClickedOk: picks up the revision of a single selected entry
        const QList<GitRevLoglist> selected = m_refList->selectedEntries();
        m_selectedHash = selected.size() == 1 ? selected.constFirst().hash : QString();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(search, &QPushButton::clicked, this, &RefLogDlg::onFind);
    connect(m_clearStash, &QPushButton::clicked, this, &RefLogDlg::onBnClickedClearStash);
    connect(m_chooseRef, &QComboBox::activated, this, &RefLogDlg::onCbnSelchangeRef);
    connect(m_refList, &RefLogList::refLogChanged, this, &RefLogDlg::onCbnSelchangeRef);
    connect(m_refList, &RefLogList::busyChanged, this, [this](bool busy) {
        setEnabled(!busy);
    });
    // OnLvnItemchangedRefLoglist: the next search starts at the entry chosen
    const QItemSelectionModel *selection = m_refList->selectionModel();
    connect(selection, &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (current.isValid()) {
            m_searchLine = current.row();
        }
    });

    // PreTranslateMessage, wherever the focus is
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &RefLogDlg::refresh);
    connect(new QShortcut(QKeySequence(Qt::Key_F3), this), &QShortcut::activated, this, &RefLogDlg::onFind);
    connect(new QShortcut(QKeySequence::Find, this), &QShortcut::activated, this, &RefLogDlg::onFind);

    // EnableSaveRestore. KWindowConfig needs the native window, which show() would otherwise create
    resize(initialWidth, initialHeight);
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());
}

RefLogDlg::~RefLogDlg() = default;

void RefLogDlg::setCurrentBranch(const QString& ref)
{
    m_currentBranch = ref;
}

void RefLogDlg::refresh()
{
    m_refsCall = m_git.runAsync(Git::refListArguments(), RunMode::ReadOnly);
    connect(m_refsCall.get(), &GitCall::finished, this, [this] {
        GitCall *call = m_refsCall.release();
        call->disconnect(this);
        // Not deleted here: this runs inside its finished() signal
        call->deleteLater();
        // Refresh with GetRefList. show-ref exits with 1, saying nothing, in a repository without refs
        QStringList list{QStringLiteral("HEAD")};
        if (call->exitCode() == 0) {
            list += Git::parseRefList(call->output());
        } else if (call->exitCode() != 1 || !call->combinedOutput().isEmpty()) {
            showError(this, i18n("Could not get all refs.") + QLatin1Char('\n') + call->combinedOutput());
        }

        m_chooseRef->clear();
        m_chooseRef->addItems(list);
        if (m_currentBranch.isEmpty()) {
            m_currentBranch = QStringLiteral("HEAD");
        }
        // Index 0, HEAD, if the ref is gone
        m_chooseRef->setCurrentIndex(std::max<int>(0, static_cast<int>(list.indexOf(m_currentBranch))));
        onCbnSelchangeRef();
    });
}

void RefLogDlg::onCbnSelchangeRef()
{
    m_currentBranch = m_chooseRef->currentText();
    m_refList->setCurrentBranch(m_currentBranch);
    m_refList->setEntries({});
    m_logCall.reset();

    // TortoiseGit's check for the case where git would fail with "branch not known", such as after Clear stash. It
    // looks for the ref's file, which a packed ref doesn't have; the reflog's file is the one that matters
    if (!m_git.hasRefLog(m_currentBranch)) {
        showEntries({});
        return;
    }
    m_logCall = m_git.runAsync(GitRevLoglist::refLogArguments(m_currentBranch), RunMode::ReadOnly);
    connect(m_logCall.get(), &GitCall::finished, this, [this] {
        GitCall *call = m_logCall.release();
        call->disconnect(this);
        // Not deleted here: this runs inside its finished() signal
        call->deleteLater();
        QList<GitRevLoglist> entries;
        try {
            entries = GitRevLoglist::parseRefLog(call->output(), m_currentBranch);
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            showError(this, i18n("Error while loading reflog.") + QLatin1Char('\n') + e.error().message);
        }
        showEntries(entries);
    });
}

void RefLogDlg::showEntries(const QList<GitRevLoglist>& entries)
{
    m_refList->setEntries(entries);
    // Reset search start positions
    m_searchLine = 0;

    const bool isStash = m_currentBranch == QLatin1String("refs/stash");
    m_clearStash->setVisible(isStash);
    m_clearStash->setEnabled(isStash && !entries.isEmpty());
    if (isStash && entries.isEmpty()) {
        m_okButton->setFocus();
    }
}

void RefLogDlg::onBnClickedClearStash()
{
    // TortoiseGit's default button is the second one, which KMessageBox::Dangerous makes it here
    const QString question = i18n("Do you really want to delete ALL %1 stash?", m_refList->entries().size());
    const KGuiItem deleteItem(i18n("&Delete"));
    const KGuiItem abortItem(i18n("A&bort"));
    const auto options = KMessageBox::Options(KMessageBox::Dangerous);
    const auto answer = KMessageBox::questionTwoActions(this, question, {}, deleteItem, abortItem, {}, options);
    if (answer != KMessageBox::PrimaryAction) {
        return;
    }
    m_clearing = true;
    setEnabled(false);
    m_clearCall = m_git.runAsync({QStringLiteral("stash"), QStringLiteral("clear")});
    connect(m_clearCall.get(), &GitCall::finished, this, [this] {
        GitCall *call = m_clearCall.release();
        call->disconnect(this);
        // Not deleted here: this runs inside its finished() signal
        call->deleteLater();
        m_clearing = false;
        setEnabled(true);
        if (call->exitCode() != 0) {
            showError(this, call->combinedOutput());
            return;
        }
        onCbnSelchangeRef();
    });
}

void RefLogDlg::onFind()
{
    if (m_findDialog) {
        m_findDialog->raise();
        m_findDialog->activateWindow();
        return;
    }
    // The strings of CFindReplaceDialog's FR_HIDEWHOLEWORD | FR_HIDEUPDOWN form
    m_findDialog = new QDialog(this);
    m_findDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_findDialog->setWindowTitle(i18n("Find"));
    auto *layout = new QVBoxLayout(m_findDialog);
    auto *textRow = new QHBoxLayout;
    auto *text = new QLineEdit(m_findDialog);
    auto *label = new QLabel(i18n("Fi&nd what:"), m_findDialog);
    label->setBuddy(text);
    textRow->addWidget(label);
    textRow->addWidget(text, 1);
    layout->addLayout(textRow);
    auto *matchCase = new QCheckBox(i18n("Match &case"), m_findDialog);
    layout->addWidget(matchCase);
    auto *buttons = new QDialogButtonBox(m_findDialog);
    QPushButton *findButton = buttons->addButton(i18n("&Find Next"), QDialogButtonBox::AcceptRole);
    findButton->setEnabled(false);
    findButton->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(text, &QLineEdit::textChanged, findButton, [findButton](const QString& value) {
        findButton->setEnabled(!value.isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this, text, matchCase] {
        findNext(text->text(), matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    });
    connect(buttons, &QDialogButtonBox::rejected, m_findDialog, &QWidget::close);
    m_findDialog->show();
}

qsizetype RefLogDlg::findEntry(const QList<GitRevLoglist>& entries, const RefLogFind& find)
{
    const qsizetype count = entries.size();
    const qsizetype first = find.start >= count ? 0 : find.start;
    for (qsizetype offset = 0; offset < count; ++offset) {
        const qsizetype row = (first + offset) % count;
        const GitRevLoglist& entry = entries.at(row);
        const QStringList fields{entry.ref, entry.refAction, entry.hash, entry.subject, entry.body};
        if (fields.join(QLatin1Char('\n')).contains(find.text, find.sensitivity)) {
            return row;
        }
    }
    return -1;
}

void RefLogDlg::findNext(const QString& text, Qt::CaseSensitivity sensitivity)
{
    const QList<GitRevLoglist>& entries = m_refList->entries();
    if (entries.isEmpty()) {
        return;
    }
    const RefLogFind find{.text = text, .sensitivity = sensitivity, .start = m_searchLine};
    const qsizetype row = findEntry(entries, find);
    if (row < 0) {
        const QString message = QLatin1Char('"') + text + QStringLiteral("\" ") + i18n("not found");
        KMessageBox::information(m_findDialog, message, QString(), QString(), noNotify);
        return;
    }
    const QModelIndex index = m_refList->model()->index(static_cast<int>(row), 0);
    const auto flags = QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
    m_refList->selectionModel()->setCurrentIndex(index, flags);
    m_refList->scrollTo(index);
    // After the selection, which moves it to the row found
    m_searchLine = row + 1;
}

void RefLogDlg::done(int result)
{
    if (m_clearing || m_refList->isBusy()) {
        return;
    }
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

const QString& RefLogDlg::selectedHash() const
{
    return m_selectedHash;
}

} // namespace kgt
