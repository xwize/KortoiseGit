// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/AppUtils.cpp

#include "apputils.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>

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

} // namespace kgt
