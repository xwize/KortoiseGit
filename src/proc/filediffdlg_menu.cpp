// SPDX-FileCopyrightText: 2003-2008, 2018 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/FileDiffDlg.cpp: the file list's context menu (CFileDiffDlg::OnContextMenu)
 * and what its entries do.
 */

#include "filediffdlg.hpp"

#include "apputils.hpp"
#include "gitdiff.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>

#include <algorithm>
#include <functional>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;
using Handler = std::function<void()>; // What a menu entry does

constexpr qsizetype shortHashLength = 8; // CGit::GetShortHASHLength
constexpr qsizetype maxTitleLength = 20; // GetCommitTitle's limit
constexpr int minDiffWarning = 3; // CheckMultipleDiffs never warns for fewer
constexpr int defaultDiffWarning = 10; // NumDiffWarning's default

/**
 * Adds a menu entry whose handler runs after the menu has closed, so dialogs it opens aren't nested in it.
 * @param menu The menu.
 * @param icon The icon's theme name.
 * @param text The entry's text.
 * @param context The object the handler runs on.
 * @param handler What the entry does.
 * @returns The entry.
 */
QAction *addEntry(QMenu& menu, const QString& icon, const QString& text, QObject *context, const Handler& handler)
{
    return menu.addAction(QIcon::fromTheme(icon), text, context, [context, handler] {
        QMetaObject::invokeMethod(context, handler, Qt::QueuedConnection);
    });
}

/**
 * Ported from GetCommitTitle in FileDiffDlg.cpp.
 * @param rev A version.
 * @returns Its short hash and the start of its subject, for a menu entry.
 */
QString commitTitle(const GitRev& rev)
{
    QString title = rev.subject;
    if (title.size() > maxTitleLength) {
        title.truncate(maxTitleLength);
        title += QStringLiteral("...");
    }
    // CStringUtils::EscapeAccellerators
    title.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return QStringLiteral("%1 (%2)").arg(rev.hash.left(shortHashLength), title);
}

/**
 * Asks whether to go on after a file failed, as TortoiseGit's IDS_IGNOREBUTTON / IDS_ABORTBUTTON box does.
 * @param parent The box's parent.
 * @param message What went wrong.
 * @returns Whether to go on with the other files.
 */
bool askIgnore(QWidget *parent, const QString& message)
{
    QMessageBox box(QMessageBox::Warning, i18n("KortoiseGit"), message, QMessageBox::NoButton, parent);
    box.addButton(i18n("&Ignore"), QMessageBox::AcceptRole);
    const QAbstractButton *abort = box.addButton(i18n("&Abort"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() != abort;
}

/**
 * Removes a working tree file that a revert takes away. TortoiseGit sends it to the recycle bin by default
 * (RevertWithRecycleBin); here it goes to the trash.
 * @param path The file's absolute path.
 */
void trashFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        std::ignore = QFile::moveToTrash(path);
    }
}

} // namespace

namespace kgt
{

void FileDiffDlg::showContextMenu(const QPoint& pos)
{
    // OnContextMenu
    const QList<FileDiffEntry> selected = selectedEntries();
    if (selected.isEmpty() || !m_hint.isEmpty()) {
        return;
    }
    const FileDiffEntry& first = selected.constFirst();
    QMenu menu(this);
    const QString diffIcon = QStringLiteral("vcs-diff");
    QAction *compare = addEntry(menu, diffIcon, i18n("&Compare revisions"), this, [this] {
        compareSelected();
    });
    menu.setDefaultAction(compare);
    addEntry(menu, diffIcon, i18n("Show changes as &unified diff"), this, [this] {
        unifiedDiffSelected();
    });
    menu.addSeparator();
    if (!m_isBare) {
        addRevertEntries(menu, first);
        menu.addSeparator();
    }
    addEntry(menu, QStringLiteral("view-history"), i18n("Show log"), this, [this] {
        showLogSelected(false);
    });
    if (!first.isDirectory) {
        // TODO: "&Blame revisions" (ID_BLAME, not in a bare repository) needs the blame viewer, which isn't ported
        addEntry(menu, QStringLiteral("document-export"), i18n("&Export selection to..."), this, [this] {
            exportSelected();
        });
    } else {
        addEntry(menu, QStringLiteral("view-history"), i18n("Show log of submodule"), this, [this] {
            showLogSelected(true);
        });
    }
    menu.addSeparator();
    addEntry(menu, QStringLiteral("document-save-as"), i18n("&Save list of selected files to..."), this, [this] {
        saveList();
    });
    addEntry(menu, QStringLiteral("edit-copy"), i18n("Copy paths to clipboard"), this, [this] {
        copySelectionToClipboard(false);
    });
    addEntry(menu, QStringLiteral("edit-copy"), i18n("Copy all information to clipboard"), this, [this] {
        copySelectionToClipboard(true);
    });
    menu.exec(m_fileList->viewport()->mapToGlobal(pos));
}

void FileDiffDlg::addRevertEntries(QMenu& menu, const FileDiffEntry& first)
{
    // ID_REVERT1 and ID_REVERT2: a file deleted in version 2 can't go back to it unless version 1 is the working tree
    const GitRev& rev1 = m_sides.at(0).rev;
    const GitRev& rev2 = m_sides.at(1).rev;
    const QString icon = QStringLiteral("edit-undo");
    if (!rev1.hash.isEmpty()) {
        addEntry(menu, icon, i18n("Revert to revision %1", commitTitle(rev1)), this, [this] {
            revertSelectedItemToVersion(true);
        });
    }
    if (!rev2.hash.isEmpty() && (rev1.hash.isEmpty() || !(first.actions & Action::Deleted))) {
        addEntry(menu, icon, i18n("Revert to revision %1", commitTitle(rev2)), this, [this] {
            revertSelectedItemToVersion(false);
        });
    }
}

void FileDiffDlg::doDiff(const FileDiffEntry& entry)
{
    // DoDiff
    const QString& rev1 = m_sides.at(0).rev.hash;
    const QString& rev2 = m_sides.at(1).rev.hash;
    const bool unborn = rev2.isEmpty() && m_git.isUnbornBranch();
    const QStringList arguments = diffArguments(entry, {.rev1 = rev1, .rev2 = rev2, .ignore = {}}, unborn);
    std::ignore = GitDiff::start(this, {.workingDirectory = m_workingTree, .arguments = arguments});
}

bool FileDiffDlg::checkMultipleDiffs() const
{
    // CheckMultipleDiffs
    const qsizetype selCount = m_fileList->selectionModel()->selectedRows().size();
    const int configured = AppUtils::generalSettings().readEntry("NumDiffWarning", defaultDiffWarning);
    const int warning = std::max(minDiffWarning, configured);
    if (selCount <= warning) {
        return true;
    }
    const QString message = i18n(
        "You have selected %1 items to show the diff for.\nFor every of these items a new instance of the diff viewer "
        "will be started.\nDo you really want to show the diff for so many items at once?",
        selCount);
    return QMessageBox::question(window(), i18n("KortoiseGit"), message) == QMessageBox::Yes;
}

void FileDiffDlg::compareSelected()
{
    // ID_COMPARE
    if (!checkMultipleDiffs()) {
        return;
    }
    for (const FileDiffEntry& entry : selectedEntries()) {
        doDiff(entry);
    }
}

void FileDiffDlg::unifiedDiffSelected()
{
    // ID_GNUDIFFCOMPARE. CAppUtils::StartShowUnifiedDiff only uses the new path
    if (!checkMultipleDiffs()) {
        return;
    }
    const QString& rev1 = m_sides.at(0).rev.hash;
    const QString& rev2 = m_sides.at(1).rev.hash;
    for (const FileDiffEntry& entry : selectedEntries()) {
        std::ignore = AppUtils::startShowUnifiedDiff(m_git, {.rev1 = rev1, .rev2 = rev2, .path = entry.path}, this);
    }
}

void FileDiffDlg::revertSelectedItemToVersion(bool isOldVersion)
{
    // RevertSelectedItemToVersion
    const QString& rev = (isOldVersion ? m_sides.at(0) : m_sides.at(1)).rev.hash;
    if (rev.isEmpty()) {
        return;
    }
    int count = 0;
    for (const FileDiffEntry& entry : selectedEntries()) {
        try {
            revertOne(entry, rev, isOldVersion);
            ++count;
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            if (!askIgnore(this, e.error().message)) {
                break;
            }
        }
    }
    QMessageBox::information(this, i18n("KortoiseGit"), i18n("%1 files reverted to %2.", count, rev));
}

void FileDiffDlg::revertOne(const FileDiffEntry& entry, const QString& rev, bool isOldVersion) const
{
    /**
     * One file of RevertSelectedItemToVersion. TortoiseGit also moves a file that git checkout overwrites to the
     * recycle bin first; as in RevertProgressCommand, that isn't ported.
     */
    const bool rev1IsWorkingTree = m_sides.at(0).rev.hash.isEmpty();
    const bool rev2IsWorkingTree = m_sides.at(1).rev.hash.isEmpty();
    const bool isAdded = entry.actions == Action::Added;
    const bool isDeleted = entry.actions == Action::Deleted;
    const bool isReplaced = entry.actions == Action::Replaced;
    if ((isOldVersion && isAdded) || (!isOldVersion && isDeleted)) {
        QStringList arguments{QStringLiteral("rm"), QStringLiteral("--cached")};
        if (isOldVersion && isAdded) {
            // TortoiseGit issue #3881: the file may not be in the index
            arguments.append(QStringLiteral("--ignore-unmatch"));
        }
        arguments << QStringLiteral("--") << entry.path;
        // Gone from the version reverted to, so it goes from the working tree too
        trashFile(absolutePath(entry.path));
        std::ignore = m_git.run(arguments);
    } else if (isOldVersion && isReplaced) {
        if (rev2IsWorkingTree) {
            trashFile(absolutePath(entry.path));
        }
        std::ignore = m_git.run({QStringLiteral("checkout"), rev, QStringLiteral("--"), entry.oldPath});
    } else {
        if (!isOldVersion && isReplaced && rev1IsWorkingTree) {
            trashFile(absolutePath(entry.oldPath));
        }
        std::ignore = m_git.run({QStringLiteral("checkout"), rev, QStringLiteral("--"), entry.path});
    }
}

void FileDiffDlg::showLogSelected(bool submodule)
{
    // ID_LOG and ID_LOGSUBMODULE. ID_LOG's /submodule for a folder isn't passed: there is no submodule log yet
    if (!checkMultipleDiffs()) {
        return;
    }
    const QString& rev2 = m_sides.at(1).rev.hash;
    for (const FileDiffEntry& entry : selectedEntries()) {
        QStringList arguments{QStringLiteral("--command"), QStringLiteral("log")};
        if (submodule) {
            arguments.append(submoduleRevArguments(entry));
        }
        arguments << QStringLiteral("--path") << absolutePath(entry.path);
        // TortoiseGit passes version 2 even when it is the working tree, which git can't take
        if (!submodule && !rev2.isEmpty()) {
            arguments << QStringLiteral("--endrev") << rev2 << QStringLiteral("--rev") << rev2;
        }
        std::ignore = AppUtils::runTortoiseGitProc(arguments);
    }
}

QStringList FileDiffDlg::submoduleRevArguments(const FileDiffEntry& entry) const
{
    // LogSubmoduleShowRevision, on by default: the submodule's commit in version 2 (CGit::GetSubmoduleHash)
    const QString& rev2 = m_sides.at(1).rev.hash;
    if (rev2.isEmpty() || (entry.actions & Action::Deleted)) {
        return {};
    }
    try {
        const QString object = rev2 + QLatin1Char(':') + entry.path;
        const QStringList revParse{QStringLiteral("rev-parse"), QStringLiteral("--verify"), object};
        const QString hash = QString::fromLatin1(m_git.run(revParse, RunMode::ReadOnly).trimmed());
        return {QStringLiteral("--endrev"), hash, QStringLiteral("--rev"), hash};
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        QMessageBox::critical(window(), i18n("KortoiseGit"), e.error().message);
    }
    return {};
}

void FileDiffDlg::saveList()
{
    // ID_SAVEAS
    const QList<FileDiffEntry> selected = selectedEntries();
    if (selected.isEmpty()) {
        return;
    }
    QFileDialog dialog(this, i18n("&Save list of selected files to..."));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({i18n("Text Files (*.txt)"), i18n("All Files (*)")});
    dialog.setDefaultSuffix(QStringLiteral("txt"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    QFile file(dialog.selectedFiles().constFirst());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, i18n("KortoiseGit"), file.errorString());
        return;
    }
    const QString rev1 = m_sides.at(0).rev.hash.isEmpty() ? GitRev::workingCopyRef() : m_sides.at(0).rev.hash;
    const QString rev2 = m_sides.at(1).rev.hash.isEmpty() ? GitRev::workingCopyRef() : m_sides.at(1).rev.hash;
    QString text = i18n("Changed files between %1 and %2", rev1, rev2) + QLatin1Char('\n');
    for (const FileDiffEntry& entry : selected) {
        text += entry.path + QLatin1Char('\n');
    }
    if (file.write(text.toUtf8()) < 0) {
        QMessageBox::critical(this, i18n("KortoiseGit"), file.errorString());
    }
}

void FileDiffDlg::copySelectionToClipboard(bool isFull)
{
    // CopySelectionToClipboard: the visible text of the selected rows
    QModelIndexList rows = m_fileList->selectionModel()->selectedRows();
    std::ranges::sort(rows, {}, &QModelIndex::row);
    QString text;
    for (const QModelIndex& row : rows) {
        const int columns = isFull ? FileDiffListModel::ColumnCount : 1;
        QStringList cells;
        for (int column = 0; column < columns; ++column) {
            cells.append(m_model->index(row.row(), column).data().toString());
        }
        text += cells.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    AppUtils::copyToClipboard(text);
}

void FileDiffDlg::exportSelected()
{
    // ID_EXPORT: the selected files as they are in version 2
    const QString dir = QFileDialog::getExistingDirectory(this, {}, m_exportDir);
    if (dir.isEmpty()) {
        return;
    }
    m_exportDir = dir;
    for (const FileDiffEntry& entry : selectedEntries()) {
        // We cannot export directories or folders
        if (entry.actions == Action::Deleted || entry.isDirectory) {
            continue;
        }
        if (!exportOne(entry)) {
            return;
        }
    }
}

bool FileDiffDlg::exportOne(const FileDiffEntry& entry)
{
    // One file of ID_EXPORT
    const QString& rev2 = m_sides.at(1).rev.hash;
    const QString filename = QDir(m_exportDir).filePath(entry.path);
    std::ignore = QDir().mkpath(QFileInfo(filename).absolutePath());
    if (rev2.isEmpty()) {
        // CopyFile overwrites
        QFile::remove(filename);
        QFile source(absolutePath(entry.path));
        if (!source.copy(filename)) {
            QMessageBox::critical(this, i18n("KortoiseGit"), source.errorString());
            return false;
        }
        return true;
    }
    QString error = i18n("Could not create file.");
    QFile output(filename);
    if (output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        try {
            m_git.getOneFile(rev2, entry.path, output);
            return true;
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git && e.error().category != ErrorCategory::Io) {
                throw;
            }
            error = e.error().message;
        }
    }
    const QString& path = entry.path;
    const QString failed = i18n("Failed to checkout file \"%1\" of revision %2 to \"%3\".", path, rev2, filename);
    return askIgnore(this, failed + QLatin1Char('\n') + error);
}

QList<FileDiffEntry> FileDiffDlg::selectedEntries() const
{
    QModelIndexList rows = m_fileList->selectionModel()->selectedRows();
    std::ranges::sort(rows, {}, &QModelIndex::row);
    QList<FileDiffEntry> entries;
    for (const QModelIndex& row : rows) {
        entries.append(m_model->entries().at(row.row()));
    }
    return entries;
}

QString FileDiffDlg::absolutePath(const QString& path) const
{
    return QDir(m_workingTree).filePath(path);
}

} // namespace kgt
