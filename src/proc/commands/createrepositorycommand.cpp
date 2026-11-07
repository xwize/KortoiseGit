// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/CreateRepositoryCommand.cpp. git init stands in for libgit2's
 * git_repository_init_ext; the bare repository's Desktop.ini icon has no counterpart here.
 */

#include "commands/createrepositorycommand.hpp"

#include "createrepodlg.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <tuple>

namespace
{

using namespace kgt;

// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

/**
 * Asks whether to go on despite a warning, with TortoiseGit's Abort and Proceed buttons.
 * @param message The warning.
 * @returns True if the user chose Proceed.
 */
bool confirmProceed(const QString& message)
{
    return KMessageBox::warningTwoActions(nullptr, message, QString(), KGuiItem(i18n("A&bort")), KGuiItem(i18n("&Proceed")), QString(), noNotify)
        == KMessageBox::SecondaryAction;
}

/**
 * @param folder A folder that may not exist yet.
 * @returns Its nearest existing ancestor, or the folder itself if it exists.
 */
QString existingAncestor(const QString& folder)
{
    // QDir::cdUp() won't move into a folder that doesn't exist, so go up by path
    QString path = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    while (!QFileInfo(path).isDir() && path != QDir::rootPath()) {
        path = QFileInfo(path).absolutePath();
    }
    return path;
}

/**
 * @param folder A folder.
 * @returns Whether it exists and holds anything, hidden files included.
 */
bool isNonEmptyDirectory(const QString& folder)
{
    const QDir dir(folder);
    return dir.exists() && !dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

} // namespace

namespace kgt
{

CreateRepositoryCommand::CreateRepositoryCommand()
    : Command(PathRequirement::PathRequired)
{
}

bool CreateRepositoryCommand::execute()
{
    const QString folder = QDir::cleanPath(paths().constFirst());
    const QString nativeFolder = QDir::toNativeSeparators(folder);
    if (isSpecialFolder(folder)
        && !confirmProceed(i18n("The target folder \n%1\nis a special folder and is not supposed to be a repository root!\nAre you sure you want to "
                                "initialize a git repository inside that folder?",
                                nativeFolder))) {
        return false;
    }
    // TortoiseGit doesn't offer the command inside a repository; started by hand, it asks first
    if (const QString repository = existingRepository(folder); !repository.isEmpty()
        && !confirmProceed(i18n("The target folder \n%1\nis already inside the git repository\n%2\nAre you sure you want to initialize a git "
                                "repository inside that folder?",
                                nativeFolder,
                                QDir::toNativeSeparators(repository)))) {
        return false;
    }

    CreateRepoDlg dlg(folder);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    if (dlg.isBare() && isNonEmptyDirectory(folder)
        && !confirmProceed(
            i18n("The target folder \n%1\nis not empty!\nAre you sure you want to initialize a git repository inside that folder?", nativeFolder))) {
        return false;
    }

    try {
        initRepository(folder, dlg.isBare());
    } catch (const RuntimeError& e) {
        KMessageBox::error(nullptr, i18n("Could not initialize a new repository.") + QLatin1Char('\n') + e.error().message, QString(), noNotify);
        return false;
    }
    KMessageBox::information(nullptr, i18n("Initialized empty Git repository in %1.", nativeFolder), QString(), QString(), noNotify);
    return true;
}

bool CreateRepositoryCommand::isSpecialFolder(const QString& folder)
{
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    if (canonical.isEmpty()) {
        // It doesn't exist yet, so it can't be any of them
        return false;
    }
    // The root, then Linux's counterparts of FOLDERID_Profile, Desktop, Documents, Windows, System and ProgramFiles
    QStringList special{QDir::rootPath()};
    for (const auto location : {QStandardPaths::HomeLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation}) {
        special.append(QStandardPaths::writableLocation(location));
    }
    special.append({QStringLiteral("/etc"), QStringLiteral("/usr"), QStringLiteral("/usr/bin"), QStringLiteral("/opt")});
    return std::ranges::any_of(special, [&canonical](const QString& path) {
        return !path.isEmpty() && QFileInfo(path).canonicalFilePath() == canonical;
    });
}

QString CreateRepositoryCommand::existingRepository(const QString& folder)
{
    if (GitAdminDir::isBareRepo(folder)) {
        return folder;
    }
    return GitAdminDir::worktreeAdminDirPath(existingAncestor(folder));
}

void CreateRepositoryCommand::initRepository(const QString& folder, bool bare)
{
    QStringList arguments{QStringLiteral("init")};
    if (bare) {
        arguments.append(QStringLiteral("--bare"));
    }
    arguments << QStringLiteral("--") << folder;
    std::ignore = Git(existingAncestor(folder)).run(arguments);
}

} // namespace kgt
