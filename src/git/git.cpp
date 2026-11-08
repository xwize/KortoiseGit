// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/Git.cpp (CGit::Run, CGit::StripRefName, CGit::HasWorkingTreeConflicts, the
// branch and tag lists and the ref name helpers)

#include "git/git.hpp"

#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <tuple>
#include <utility>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto remotesPrefix = "remotes/"_L1;

/**
 * @returns The order of StrCmpLogicalW, TortoiseGit's LogicalComparePredicate: numbers by value, case ignored.
 */
QCollator logicalCollator()
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

/**
 * @param output Text git printed.
 * @returns Its lines, without the empty ones.
 */
QList<QByteArray> nonEmptyLines(const QByteArray& output)
{
    QList<QByteArray> lines = output.split('\n');
    lines.removeIf([](const QByteArray& line) {
        return line.trimmed().isEmpty();
    });
    return lines;
}

} // namespace

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

QByteArray Git::run(const QStringList& arguments, RunMode mode, const QByteArray& input) const
{
    GitCall call(m_workingDirectory, arguments, mode, input);
    call.waitForFinished();
    return call.output();
}

std::unique_ptr<GitCall> Git::runAsync(const QStringList& arguments, RunMode mode, const QByteArray& input) const
{
    return std::make_unique<GitCall>(m_workingDirectory, arguments, mode, input);
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

QString Git::stripRefName(const QString& ref)
{
    for (const QLatin1StringView prefix : {QLatin1StringView("refs/heads/"), QLatin1StringView("refs/")}) {
        if (ref.startsWith(prefix)) {
            return ref.mid(prefix.size());
        }
    }
    return ref;
}

bool Git::isHash(QByteArrayView text)
{
    constexpr qsizetype sha1Length = 40;
    constexpr qsizetype sha256Length = 64;
    if (text.size() != sha1Length && text.size() != sha256Length) {
        return false;
    }
    return std::ranges::all_of(text, [](char digit) {
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    });
}

bool Git::hasWorkingTreeConflicts() const
{
    return !run({QStringLiteral("ls-files"), QStringLiteral("-u")}, RunMode::ReadOnly).isEmpty();
}

QString Git::configValue(const QString& name) const
{
    try {
        QByteArray value = run({QStringLiteral("config"), QStringLiteral("--get"), name}, RunMode::ReadOnly);
        if (value.endsWith('\n')) {
            value.chop(1);
        }
        return QString::fromUtf8(value);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: not set
        return {};
    }
}

void Git::setConfigValue(const QString& name, const QString& value) const
{
    // After --: a value may start with a dash
    std::ignore = run({QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--"), name, value});
}

void Git::unsetConfigValue(const QString& name) const
{
    std::ignore = run({QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--unset"), QStringLiteral("--"), name});
}

QString Git::currentBranch() const
{
    const QString branch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(m_workingDirectory));
    if (branch.isEmpty() || branch == QLatin1String("HEAD")) {
        return QStringLiteral("(no branch)");
    }
    return branch;
}

QString Git::fullRefName(const QString& shortRefName) const
{
    try {
        return QString::fromUtf8(run({QStringLiteral("rev-parse"),
                                      QStringLiteral("--symbolic-full-name"),
                                      QStringLiteral("--verify"),
                                      QStringLiteral("--end-of-options"),
                                      shortRefName},
                                     RunMode::ReadOnly))
            .trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

bool Git::isBranchTagNameUnique(const QString& name) const
{
    try {
        const QByteArray output = run({QStringLiteral("show-ref"),
                                       QStringLiteral("--tags"),
                                       QStringLiteral("--heads"),
                                       QStringLiteral("--"),
                                       QStringLiteral("refs/heads/") + name,
                                       QStringLiteral("refs/tags/") + name},
                                      RunMode::ReadOnly);
        return nonEmptyLines(output).size() <= 1;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: neither exists
        return true;
    }
}

bool Git::branchTagExists(const QString& name, bool isBranch) const
{
    try {
        const QByteArray output = run({QStringLiteral("show-ref"),
                                       isBranch ? QStringLiteral("--heads") : QStringLiteral("--tags"),
                                       QStringLiteral("--"),
                                       QStringLiteral("refs/heads/") + name,
                                       QStringLiteral("refs/tags/") + name},
                                      RunMode::ReadOnly);
        return !output.isEmpty();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: none
        return false;
    }
}

bool Git::isBranchNameValid(const QString& name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('-')) || name == QLatin1String("HEAD")) {
        return false;
    }
    try {
        std::ignore = Git(QDir::tempPath()).run({QStringLiteral("check-ref-format"), QStringLiteral("refs/heads/") + name}, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

QString Git::derefFetchHead() const
{
    QFile fetchHead(QDir(GitAdminDir::worktreeAdminDirPath(m_workingDirectory)).absoluteFilePath(QStringLiteral("FETCH_HEAD")));
    if (!fetchHead.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Each line is <hash> TAB [not-for-merge] TAB <description>; the lines to merge have nothing between the tabs
    int forMergeLineCount = 0;
    QByteArray hashToReturn;
    for (const QByteArray& line : fetchHead.readAll().split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 3) {
            continue;
        }
        if (fields.at(1).isEmpty()) {
            hashToReturn = fields.at(0);
            ++forMergeLineCount;
            if (forMergeLineCount > 1) {
                // More than one branch to merge: an octopus merge, no single ref to pick
                return {};
            }
        }
    }
    return QString::fromLatin1(hashToReturn);
}

QString Git::fixBranchName(const QString& branchName) const
{
    if (branchName == QLatin1String("FETCH_HEAD")) {
        return derefFetchHead();
    }
    return branchName;
}

QStringList Git::branchListArguments(unsigned int type)
{
    QStringList arguments{QStringLiteral("branch"), QStringLiteral("--no-color")};
    if ((type & BranchAll) == BranchAll) {
        arguments.append(QStringLiteral("-a"));
    } else if (type & BranchRemote) {
        arguments.append(QStringLiteral("-r"));
    }
    return arguments;
}

BranchList Git::parseBranchList(const QByteArray& output, unsigned int type, bool skipCurrent, bool hasFetchHead)
{
    BranchList result;
    QString current;
    bool headIsDetached = false;
    for (const QByteArray& rawLine : nonEmptyLines(output)) {
        const QByteArray line = rawLine.trimmed();
        if (line.contains(" -> ")) {
            // Something like remotes/origin/HEAD -> origin/main
            continue;
        }
        QString branch = QString::fromUtf8(line);
        if (line.startsWith('*')) {
            if (skipCurrent) {
                continue;
            }
            branch = branch.mid(2);
            // A detached HEAD shows as "(HEAD detached at ...)"
            if (branch.startsWith(QLatin1Char('('))) {
                headIsDetached = true;
                continue;
            }
            current = branch;
        } else if (line.startsWith('+')) {
            // Checked out in another worktree of the same repository (git 2.23 and later)
            branch = branch.mid(2);
        }
        if ((type & BranchRemote) != 0 && (type & BranchLocal) == 0) {
            branch.prepend(remotesPrefix);
        }
        result.branches.append(branch);
    }

    if ((type & BranchFetchHead) != 0 && hasFetchHead) {
        result.branches.append(QStringLiteral("FETCH_HEAD"));
    }

    // LogicalCompareBranchesPredicate, with TortoiseGit's default of local branches first
    const QCollator collator = logicalCollator();
    std::ranges::sort(result.branches, [&collator](const QString& left, const QString& right) {
        const bool leftIsRemote = left.startsWith(remotesPrefix);
        const bool rightIsRemote = right.startsWith(remotesPrefix);
        if (leftIsRemote != rightIsRemote) {
            return rightIsRemote;
        }
        return collator.compare(left, right) < 0;
    });

    if (!headIsDetached && !skipCurrent && !current.isEmpty()) {
        result.current = static_cast<int>(result.branches.indexOf(current));
    }
    return result;
}

QStringList Git::parseTagList(const QByteArray& output)
{
    QStringList tags;
    for (const QByteArray& line : nonEmptyLines(output)) {
        tags.append(QString::fromUtf8(line));
    }
    // TortoiseGit's default order; SortTagsReversed isn't a setting here yet
    const QCollator collator = logicalCollator();
    std::ranges::sort(tags, [&collator](const QString& left, const QString& right) {
        return collator.compare(left, right) < 0;
    });
    return tags;
}

} // namespace kgt
