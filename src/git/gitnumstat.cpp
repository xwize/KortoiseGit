// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/TGitPathList.cpp (CTGitPathList::ParserFromLog, its --numstat part)

#include "git/gitnumstat.hpp"

#include "git/git.hpp"

#include <QByteArrayView>

#include <optional>
#include <utility>

namespace
{

using namespace kgt;

/**
 * @param output The whole output, for the message.
 * @param pos Where the bad record starts.
 * @returns A Parse error quoting the record.
 */
tl::unexpected<Error> badRecord(const QByteArray& output, qsizetype pos)
{
    const QByteArrayView rest = QByteArrayView(output).sliced(pos);
    const qsizetype end = rest.indexOf('\0');
    return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected git diff --numstat record: %1").arg(QString::fromUtf8(end < 0 ? rest : rest.first(end))));
}

/**
 * Parses one record: "added\tremoved\tpath\0", or "added\tremoved\t\0source\0path\0" for a rename or copy.
 * @param output The whole output.
 * @param[in,out] pos Where the record starts; moved past it.
 * @returns The entry, or nothing if the record is malformed.
 */
std::optional<NumStatEntry> takeRecord(const QByteArray& output, qsizetype& pos)
{
    NumStatEntry entry;
    std::optional<QString> added = Git::takeField(output, pos, '\t');
    std::optional<QString> removed = added ? Git::takeField(output, pos, '\t') : std::nullopt;
    if (!removed || pos >= output.size()) {
        return std::nullopt;
    }
    entry.added = std::move(*added);
    entry.removed = std::move(*removed);
    if (output.at(pos) == '\0') {
        ++pos;
        std::optional<QString> source = Git::takeField(output, pos, '\0');
        if (!source) {
            return std::nullopt;
        }
        entry.origPath = std::move(*source);
    }
    std::optional<QString> path = Git::takeField(output, pos, '\0');
    if (!path) {
        return std::nullopt;
    }
    entry.path = std::move(*path);
    return entry;
}

} // namespace

namespace kgt
{

QStringList GitNumStat::arguments(const QString& base, const QStringList& paths)
{
    QStringList arguments{QStringLiteral("--literal-pathspecs"),
                          QStringLiteral("diff"),
                          QStringLiteral("--numstat"),
                          QStringLiteral("-z"),
                          QStringLiteral("-M"),
                          base,
                          QStringLiteral("--")};
    arguments.append(paths);
    return arguments;
}

Result<QList<NumStatEntry>> GitNumStat::parse(const QByteArray& output)
{
    QList<NumStatEntry> entries;
    qsizetype pos = 0;
    while (pos < output.size()) {
        const qsizetype start = pos;
        std::optional<NumStatEntry> entry = takeRecord(output, pos);
        if (!entry) {
            return badRecord(output, start);
        }
        entries.append(std::move(*entry));
    }
    return entries;
}

} // namespace kgt
