// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AppUtils.cpp

#include "apputils.hpp"

#include "git/gitadmindir.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>

#include <tuple>

namespace kgt
{

QString AppUtils::getClipboardLink(const QString& skipGitPrefix, int paramsCount)
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return {};
    }
    return clipboardLink(clipboard->text(), skipGitPrefix, paramsCount);
}

QString AppUtils::clipboardLink(QString clipboardText, const QString& skipGitPrefix, int paramsCount)
{
    if (clipboardText.isEmpty()) {
        return {};
    }
    if (clipboardText.size() >= 2 && clipboardText.startsWith(QLatin1Char('"')) && clipboardText.endsWith(QLatin1Char('"'))) {
        clipboardText = clipboardText.mid(1, clipboardText.size() - 2);
    }
    if (const qsizetype newline = clipboardText.indexOf(QLatin1Char('\n')); newline >= 0) {
        clipboardText.truncate(newline);
        while (!clipboardText.isEmpty() && clipboardText.back().isSpace()) {
            clipboardText.chop(1);
        }
    }
    if (clipboardText.isEmpty()) {
        return {};
    }

    for (const QLatin1String prefix :
         {QLatin1String("http://"), QLatin1String("https://"), QLatin1String("git://"), QLatin1String("ssh://"), QLatin1String("git@")}) {
        if (clipboardText.startsWith(prefix) && clipboardText.size() != prefix.size()) {
            return clipboardText;
        }
    }
    // TortoiseGit takes a local path starting with a drive letter; here, an absolute path
    if (clipboardText.size() >= 2 && clipboardText.startsWith(QLatin1Char('/'))) {
        return clipboardText;
    }

    // Skip prefixes like "git clone "
    if (!skipGitPrefix.isEmpty() && clipboardText.startsWith(skipGitPrefix)) {
        clipboardText = clipboardText.mid(skipGitPrefix.size()).trimmed();
        qsizetype spacePos = -1;
        while (paramsCount >= 0) {
            --paramsCount;
            spacePos = clipboardText.indexOf(QLatin1Char(' '), spacePos + 1);
            if (spacePos == -1) {
                break;
            }
        }
        if (spacePos > 0 && paramsCount < 0) {
            clipboardText.truncate(spacePos);
        }
        return clipboardText;
    }
    return {};
}

bool AppUtils::runTortoiseGitProc(const QStringList& arguments)
{
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

bool AppUtils::isTGitRebaseActive(const QString& workingTree, QWidget *parent)
{
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(workingTree);
    if (adminDir.isEmpty()) {
        return false;
    }
    const QDir lock(QDir(adminDir).filePath(QStringLiteral("tgitrebase.active")));
    if (!lock.exists()) {
        return false;
    }
    const auto answer = KMessageBox::warningTwoActions(parent,
                                                       i18n("Found a lock file indicating that a TortoiseGit rebase process is running. This blocks "
                                                            "this operation.\n\nIf you are not running a TortoiseGit rebase process right now, you "
                                                            "can remove the stale lock file in order to continue."),
                                                       QString(),
                                                       KGuiItem(i18n("Remove stale lock file")),
                                                       KGuiItem(i18n("A&bort")),
                                                       QString(),
                                                       KMessageBox::Options());
    if (answer != KMessageBox::PrimaryAction) {
        return true;
    }
    // RemoveDirectory: only an empty directory goes
    std::ignore = lock.rmdir(lock.absolutePath());
    return false;
}

} // namespace kgt
