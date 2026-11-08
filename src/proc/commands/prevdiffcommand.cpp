// SPDX-FileCopyrightText: 2008-2011, 2016, 2018-2019, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PrevDiffCommand.cpp.
 */

#include "commands/prevdiffcommand.hpp"

#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QFileInfo>
#include <QMessageBox>

namespace kgt
{

PrevDiffCommand::PrevDiffCommand()
    : Command(PathRequirement::WorkingTreeRequired)
{
}

bool PrevDiffCommand::execute()
{
    const QString& path = paths().constFirst();
    if (QFileInfo(path).isDir()) {
        throw RuntimeError({.category = ErrorCategory::CommandLine, .message = i18n("Diff with previous version does not support folders yet.")});
    }
    const std::optional<DiffInvocation> diff = invocation(path);
    if (!diff) {
        QMessageBox::critical(nullptr, i18n("KortoiseGit"), i18n("Could not determine the last committed revision!"));
        return false;
    }
    return GitDiff::start(nullptr, *diff);
}

std::optional<DiffInvocation> PrevDiffCommand::invocation(const QString& path)
{
    const QString root = Git::workingTreeRoot(path);
    const QString file = gitPath(path);
    QByteArray output;
    try {
        output = Git(root).run({QStringLiteral("log"),
                                QStringLiteral("-n"),
                                QStringLiteral("2"),
                                QStringLiteral("--topo-order"),
                                QStringLiteral("--format=%H"),
                                QStringLiteral("HEAD"),
                                QStringLiteral("--"),
                                file},
                               RunMode::ReadOnly);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // CLogDataVector::ParserFromLog finds no commits on an unborn branch
        return std::nullopt;
    }
    const QStringList revisions = QString::fromLatin1(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (revisions.size() != 2) {
        return std::nullopt;
    }
    return DiffInvocation{.workingDirectory = root, .arguments = GitDiff::diffArguments(file, file, {}, revisions.at(1))};
}

} // namespace kgt
