// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitRev.cpp

#include "gitrev.hpp"

#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <QList>
#include <QStringList>

namespace
{

constexpr qsizetype hashLength = 40; // GIT_REV_ZERO's length
constexpr qsizetype minWorkingCopyRefLength = 8; // GetCommit's shortest form of GIT_REV_ZERO
constexpr qsizetype commitFieldCount = 5;

} // namespace

namespace kgt
{

QString GitRev::workingCopyRef()
{
    return QString(hashLength, QLatin1Char('0'));
}

bool GitRev::isWorkingCopyRef(const QString& refName)
{
    return refName.size() >= minWorkingCopyRefLength && workingCopyRef().startsWith(refName);
}

Result<GitRev> GitRev::getCommit(const Git& git, const QString& refName)
{
    if (isWorkingCopyRef(refName)) {
        // TortoiseGit doesn't translate it either
        return GitRev{
            .hash = {},
            .subject = QStringLiteral("Working Tree"),
            .authorName = {},
            .authorDate = {},
            .committerDate = {},
            .isCommit = true,
        };
    }
    const QString name = git.fixBranchName(refName);
    try {
        // ^{commit} dereferences signed tags, as GetCommit's ^{} does
        const QStringList arguments{
            QStringLiteral("log"),
            QStringLiteral("-1"),
            QStringLiteral("--no-walk"),
            QStringLiteral("--no-show-signature"),
            QStringLiteral("--encoding=UTF-8"),
            QStringLiteral("--format=%H%x00%s%x00%aN%x00%at%x00%ct"),
            QStringLiteral("--end-of-options"),
            name + QStringLiteral("^{commit}"),
            QStringLiteral("--"),
        };
        return parseCommit(git.run(arguments, RunMode::ReadOnly));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    // allowTree
    try {
        const QStringList arguments{
            QStringLiteral("rev-parse"),
            QStringLiteral("--verify"),
            QStringLiteral("--end-of-options"),
            name + QStringLiteral("^{tree}"),
        };
        return GitRev{
            .hash = QString::fromLatin1(git.run(arguments, RunMode::ReadOnly).trimmed()),
            .subject = {},
            .authorName = {},
            .authorDate = {},
            .committerDate = {},
            .isCommit = false,
        };
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return makeError(ErrorCategory::Git, QStringLiteral("Ref \"%1\" is neither a commit nor a tree.").arg(name));
    }
}

Result<GitRev> GitRev::parseCommit(const QByteArray& output)
{
    const QList<QByteArray> fields = output.trimmed().split('\0');
    if (fields.size() != commitFieldCount) {
        const QString message = QStringLiteral("Unexpected output from git log: %1").arg(QString::fromUtf8(output));
        return makeError(ErrorCategory::Parse, message);
    }
    return GitRev{
        .hash = QString::fromLatin1(fields.at(0)),
        .subject = QString::fromUtf8(fields.at(1)),
        .authorName = QString::fromUtf8(fields.at(2)),
        .authorDate = QDateTime::fromSecsSinceEpoch(fields.at(3).toLongLong()),
        .committerDate = QDateTime::fromSecsSinceEpoch(fields.at(4).toLongLong()),
        .isCommit = true,
    };
}

} // namespace kgt
