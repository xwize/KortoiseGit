// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/DiffCommand.cpp.
 */

#include "commands/diffcommand.hpp"

#include "git/git.hpp"
#include "git/gitrevloglist.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QFileInfo>

namespace
{

using namespace kgt;

/**
 * @returns The revision a working tree file is compared with.
 */
QString head()
{
    return QStringLiteral("HEAD");
}

/**
 * DiffCommand's git_status_file() == GIT_STATUS_INDEX_NEW: staged as new and unchanged since. libgit2's status
 * looks for no renames, so neither does this.
 * @param git Runs git at the working tree root.
 * @param file The file, relative to the root.
 * @returns Whether it is newly added.
 */
bool isNewlyAdded(const Git& git, const QString& file)
{
    return git
        .run({QStringLiteral("--literal-pathspecs"),
              QStringLiteral("status"),
              QStringLiteral("--porcelain"),
              QStringLiteral("--no-renames"),
              QStringLiteral("-z"),
              QStringLiteral("--"),
              file},
             RunMode::ReadOnly)
        .startsWith("A  ");
}

/**
 * DiffCommand's search for the original name of a newly added file.
 * @param git Runs git at the working tree root.
 * @param file The file, relative to the root.
 * @returns The path it was renamed or copied from, or an empty string if there is none.
 */
QString renameSource(const Git& git, const QString& file)
{
    QByteArray output;
    try {
        // 50 is the default of TortoiseGit's DiffSimilarityIndexThreshold
        output = git.run({QStringLiteral("diff-index"),
                          QStringLiteral("--raw"),
                          head(),
                          QStringLiteral("-M50%"),
                          QStringLiteral("-C50%"),
                          QStringLiteral("-z"),
                          QStringLiteral("--")},
                         RunMode::ReadOnly);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // CGit::IsInitRepos: without a commit there is nothing to have been renamed
        return {};
    }
    const Result<GitRevFiles> files = GitRevLoglist::parseFiles(output);
    if (!files) {
        return {};
    }
    for (const GitStatusEntry& entry : files->status) {
        if (entry.path == file) {
            return entry.origPath;
        }
    }
    return {};
}

/**
 * The half of DiffCommand::Execute for one file in the working tree.
 * @param git Runs git at the working tree root.
 * @param file The file, relative to the root.
 * @returns The arguments after "git".
 */
QStringList workingTreeArguments(const Git& git, const QString& file)
{
    if (!isNewlyAdded(git, file)) {
        return GitDiff::diffArguments(file, file, {}, head());
    }
    const QString source = renameSource(git, file);
    return source.isEmpty() ? GitDiff::diffNullArguments(file) : GitDiff::diffArguments(file, source, {}, head());
}

} // namespace

namespace kgt
{

DiffCommand::DiffCommand()
    : Command(PathRequirement::PathRequired)
{
}

bool DiffCommand::execute()
{
    const QString& path = paths().constFirst();
    const QString path2 = value(QStringLiteral("path2"));
    if (path2.isEmpty() && QFileInfo(path).isDir()) {
        throw RuntimeError({.category = ErrorCategory::CommandLine, .message = i18n("Diff needs a file, not a folder.")});
    }
    return GitDiff::start(nullptr, invocation(path, path2, value(QStringLiteral("startrev")), value(QStringLiteral("endrev"))));
}

DiffInvocation DiffCommand::invocation(const QString& path, const QString& path2, const QString& startRev, const QString& endRev)
{
    if (path2.isEmpty()) {
        const QString root = Git::workingTreeRoot(path);
        const QString file = gitPath(path);
        return {.workingDirectory = root,
                .arguments =
                    !startRev.isEmpty() && !endRev.isEmpty() ? GitDiff::diffArguments(file, file, endRev, startRev) : workingTreeArguments(Git(root), file)};
    }
    if (!startRev.isEmpty() && !endRev.isEmpty()) {
        // Two files of one repository, each at its revision
        const QString root = repositoryRoot(path);
        if (path2.startsWith(root + QLatin1Char('/'))) {
            return {.workingDirectory = root, .arguments = GitDiff::diffArguments(gitPath(path2), gitPath(path), endRev, startRev)};
        }
    }
    // As TortoiseGit calls CAppUtils::StartExtDiff: the second file is on the left
    return {.workingDirectory = QFileInfo(path).absolutePath(), .arguments = GitDiff::extDiffArguments(path2, path)};
}

} // namespace kgt
