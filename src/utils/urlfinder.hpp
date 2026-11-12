// SPDX-FileCopyrightText: 2003-2008, 2013, 2018, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Utils/URLFinder.h: finds the URLs and email addresses in a commit message.
 */

#pragma once

#include <QList>
#include <QString>

namespace kgt
{

/**
 * A run of characters in a string, from start up to but not including end (a CHARRANGE).
 */
struct TextRange {
    qsizetype start = 0;
    qsizetype end = 0;

    friend bool operator==(const TextRange&, const TextRange&) = default;
};

/**
 * Ported from IsValidURLChar.
 * @returns Whether chr can be part of a URL or an email address.
 */
[[nodiscard]] bool isValidUrlChar(QChar chr);

/**
 * Ported from ::PathIsURL, which IsUrlOrEmail relies on: a scheme of at least two characters, then a colon.
 * @returns Whether text starts with a URL scheme.
 */
[[nodiscard]] bool pathIsUrl(const QString& text);

/**
 * Ported from IsUrlOrEmail.
 * @returns Whether text is an http, https, git, ftp, file or mailto URL, or looks like an email address (and not
 * like git@host:path).
 */
[[nodiscard]] bool isUrlOrEmail(const QString& text);

/**
 * Ported from FindURLMatches: trailing punctuation is left out, and a URL put within <...> runs up to the '>'.
 * @returns The URLs and email addresses in msg, in order.
 */
[[nodiscard]] QList<TextRange> findUrlMatches(const QString& msg);

} // namespace kgt
