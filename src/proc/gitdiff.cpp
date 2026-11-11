// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2020, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitDiff.cpp

#include "gitdiff.hpp"

#include "git/git.hpp"

#include <KLocalizedString>

#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

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

/**
 * @param invocation What to run.
 * @returns Its arguments, with the default diff tool added if the user's git settings don't name one.
 */
QStringList argumentsWithTool(const kgt::DiffInvocation& invocation)
{
    const auto isInstalled = [](const QString& tool) {
        return !QStandardPaths::findExecutable(tool).isEmpty();
    };
    const kgt::Git git(invocation.workingDirectory);
    const QString diffTool = git.configValue(QStringLiteral("diff.tool"));
    const QString mergeTool = git.configValue(QStringLiteral("merge.tool"));
    return kgt::GitDiff::withDefaultTool(invocation.arguments, diffTool, mergeTool, isInstalled);
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

QStringList GitDiff::defaultTools()
{
    // Not from TortoiseGit, which has TortoiseGitMerge for both jobs: this is our default until we have an equivalent.
    // Left alone, git would try KDiff3 first (list_merge_tool_candidates), but Meld is the better fit for diffs.
    return {QStringLiteral("meld"), QStringLiteral("kdiff3")};
}

QStringList GitDiff::withDefaultTool(const QStringList& arguments, const QString& diffTool, const QString& mergeTool, const AppUtils::IsInstalled& isInstalled)
{
    // git difftool falls back to merge.tool when diff.tool isn't set (get_configured_merge_tool in git-mergetool--lib)
    if (!diffTool.isEmpty() || !mergeTool.isEmpty()) {
        return arguments;
    }
    const QStringList candidates = defaultTools();
    const auto found = std::ranges::find_if(candidates, isInstalled);
    if (found == candidates.cend()) {
        return arguments;
    }
    QStringList result = arguments;
    result.insert(result.indexOf(QStringLiteral("--no-prompt")) + 1, QStringLiteral("--tool=") + *found);
    return result;
}

bool GitDiff::start(QWidget *parent, const DiffInvocation& invocation)
{
    if (QProcess::startDetached(QStringLiteral("git"), argumentsWithTool(invocation), invocation.workingDirectory)) {
        return true;
    }
    QMessageBox::warning(parent, i18n("KortoiseGit"), i18n("Could not start git difftool."));
    return false;
}

} // namespace kgt
