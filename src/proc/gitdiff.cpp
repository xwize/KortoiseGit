// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2020, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitDiff.cpp

#include "gitdiff.hpp"

#include <KLocalizedString>

#include <QMessageBox>
#include <QProcess>

namespace
{

/**
 * @returns The start of every git difftool command line.
 */
QStringList difftool()
{
    return {QStringLiteral("--literal-pathspecs"), QStringLiteral("difftool"), QStringLiteral("--no-prompt")};
}

/**
 * @param rev A revision.
 * @param path A file, relative to the working tree root.
 * @returns git's name for that file in that revision.
 */
QString blob(const QString& rev, const QString& path)
{
    return rev + QLatin1Char(':') + path;
}

} // namespace

namespace kgt
{

QStringList GitDiff::diffArguments(const QString& path, const QString& path2, const QString& rev1, const QString& rev2)
{
    Q_ASSERT(!rev2.isEmpty());
    QStringList arguments = difftool();
    if (path == path2) {
        arguments.append(rev2);
        if (!rev1.isEmpty()) {
            arguments.append(rev1);
        }
        arguments << QStringLiteral("--") << path;
    } else if (rev1.isEmpty()) {
        arguments << blob(rev2, path2) << QStringLiteral("--") << path;
    } else {
        arguments << blob(rev2, path2) << blob(rev1, path);
    }
    return arguments;
}

QStringList GitDiff::dirArguments(const QStringList& paths, const QString& rev1, const QString& rev2)
{
    Q_ASSERT(!rev2.isEmpty());
    QStringList arguments = difftool() << QStringLiteral("--dir-diff") << rev2;
    if (!rev1.isEmpty()) {
        arguments.append(rev1);
    }
    if (!paths.isEmpty()) {
        arguments << QStringLiteral("--") << paths;
    }
    return arguments;
}

QStringList GitDiff::diffNullArguments(const QString& path)
{
    return extDiffArguments(QStringLiteral("/dev/null"), path);
}

QStringList GitDiff::extDiffArguments(const QString& file1, const QString& file2)
{
    return difftool() << QStringLiteral("--no-index") << QStringLiteral("--") << file1 << file2;
}

bool GitDiff::start(QWidget *parent, const DiffInvocation& invocation)
{
    if (QProcess::startDetached(QStringLiteral("git"), invocation.arguments, invocation.workingDirectory)) {
        return true;
    }
    QMessageBox::warning(parent, i18n("KortoiseGit"), i18n("Could not start git difftool."));
    return false;
}

} // namespace kgt
