// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/Git.cpp (CGit::Run)

#include "git/git.hpp"

#include <QFileInfo>

#include <utility>

namespace kgt
{

Git::Git(QString workingDirectory)
    : m_workingDirectory(std::move(workingDirectory))
{
}

const QString& Git::workingDirectory() const
{
    return m_workingDirectory;
}

QByteArray Git::run(const QStringList& arguments, RunMode mode) const
{
    GitCall call(m_workingDirectory, arguments, mode);
    call.waitForFinished();
    return call.output();
}

std::unique_ptr<GitCall> Git::runAsync(const QStringList& arguments, RunMode mode) const
{
    return std::make_unique<GitCall>(m_workingDirectory, arguments, mode);
}

QString Git::workingTreeRoot(const QString& path)
{
    const QFileInfo info(path);
    const Git git(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    QByteArray root = git.run({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, RunMode::ReadOnly);
    // Only the newline git ends with: the directory name itself may end in a space
    if (root.endsWith('\n')) {
        root.chop(1);
    }
    return QString::fromUtf8(root);
}

} // namespace kgt
