// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/AppUtils.h (CAppUtils): helpers shared by several dialogs. Only what
 * has been needed so far is here.
 */

#pragma once

#include <QString>

namespace kgt
{

/**
 * Helpers shared by several dialogs and commands, TortoiseGit's CAppUtils.
 */
class AppUtils
{
public:
    AppUtils() = delete;

    /**
     * Ported from CAppUtils::GetClipboardLink: a repository URL on the clipboard, for prefilling a dialog.
     * @param skipGitPrefix A command such as "git clone " that may precede the URL, to be skipped.
     * @param paramsCount How many words after the prefix to keep.
     * @returns The URL, or an empty string if the clipboard holds none.
     */
    [[nodiscard]] static QString getClipboardLink(const QString& skipGitPrefix = {}, int paramsCount = 0);

    /**
     * getClipboardLink()'s rules applied to a given text.
     * @param clipboardText The clipboard's text.
     * @param skipGitPrefix A command such as "git clone " that may precede the URL, to be skipped.
     * @param paramsCount How many words after the prefix to keep.
     * @returns The URL, or an empty string if the text holds none.
     */
    [[nodiscard]] static QString clipboardLink(QString clipboardText, const QString& skipGitPrefix = {}, int paramsCount = 0);
};

} // namespace kgt
