// SPDX-FileCopyrightText: 2011-2016, 2019-2020, 2022-2024, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/MassiveGitTaskBase.cpp

#include "git/massivegittask.hpp"

#include "git/gitcall.hpp"

#include <tuple>

namespace kgt
{

MassiveGitTask::MassiveGitTask(const QStringList& params)
    : m_params(params)
{
    Q_ASSERT(!params.isEmpty());
}

void MassiveGitTask::setPaths(const QStringList& paths)
{
    m_paths = paths;
    // CTGitPathList::RemoveDuplicates, keeping the first of each
    m_paths.removeDuplicates();
}

QStringList MassiveGitTask::arguments() const
{
    QStringList arguments{QStringLiteral("--literal-pathspecs")};
    arguments.append(m_params);
    arguments.append({QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul")});
    return arguments;
}

QByteArray MassiveGitTask::pathSpecFile() const
{
    QByteArray file;
    for (const QString& path : m_paths) {
        file.append(path.toUtf8());
        file.append('\0');
    }
    return file;
}

bool MassiveGitTask::isEmpty() const
{
    return m_paths.isEmpty();
}

void MassiveGitTask::run(const Git& git) const
{
    if (isEmpty()) {
        return;
    }
    std::ignore = git.run(arguments(), RunMode::ReadWrite, pathSpecFile());
}

std::unique_ptr<GitCall> MassiveGitTask::runAsync(const Git& git) const
{
    Q_ASSERT(!isEmpty());
    return git.runAsync(arguments(), RunMode::ReadWrite, pathSpecFile());
}

} // namespace kgt
