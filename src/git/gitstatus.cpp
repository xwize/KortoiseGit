// SPDX-FileCopyrightText: 2008-2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitStatus.cpp

#include "git/gitstatus.hpp"

#include <QByteArrayView>

#include <iterator>
#include <source_location>
#include <utility>

namespace
{

using namespace kgt;

// Space-separated fields before the path in each record type, counting the type letter itself
constexpr qsizetype changedFields = 8; // 1 XY sub mH mI mW hH hI path
constexpr qsizetype renamedFields = 9; // 2 XY sub mH mI mW hH hI Xscore path
constexpr qsizetype unmergedFields = 10; // u XY sub m1 m2 m3 mW h1 h2 h3 path
constexpr qsizetype untrackedFields = 1; // ? path, or ! path

/**
 * A record split into its leading fields and the path that follows them.
 */
struct Record {
    QList<QByteArrayView> fields; // The fields before the path, the type letter first
    QString path; // The rest of the record, which may hold spaces
};

/**
 * @param record One NUL-terminated record, without the NUL.
 * @returns Why it isn't porcelain v2, as a Parse error.
 */
tl::unexpected<Error> badRecord(QByteArrayView record, std::source_location location = std::source_location::current())
{
    return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected git status record: %1").arg(QString::fromUtf8(record)), location);
}

/**
 * @param record One record.
 * @param fieldCount How many space-separated fields come before the path.
 * @returns The fields and the path, or an error if there are too few.
 */
Result<Record> splitRecord(QByteArrayView record, qsizetype fieldCount)
{
    Record split;
    qsizetype start = 0;
    for (qsizetype field = 0; field < fieldCount; ++field) {
        const qsizetype space = record.indexOf(' ', start);
        if (space < 0) {
            return badRecord(record);
        }
        split.fields.append(record.sliced(start, space - start));
        start = space + 1;
    }
    if (start >= record.size()) {
        return badRecord(record);
    }
    split.path = QString::fromUtf8(record.sliced(start));
    return split;
}

/**
 * @param letter One of git's porcelain status letters.
 * @returns The state it stands for, or a Parse error.
 */
Result<FileState> parseFileState(char letter)
{
    switch (letter) {
    case '.':
        return FileState::Unmodified;
    case 'M':
        return FileState::Modified;
    case 'T':
        return FileState::TypeChanged;
    case 'A':
        return FileState::Added;
    case 'D':
        return FileState::Deleted;
    case 'R':
        return FileState::Renamed;
    case 'C':
        return FileState::Copied;
    case 'U':
        return FileState::Unmerged;
    default:
        return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected git status letter: %1").arg(QLatin1Char(letter)));
    }
}

/**
 * Fills in the index and worktree states from a record's XY field.
 * @param letters The XY field.
 * @param[in,out] entry Gets its index, worktree and actions set.
 * @returns Nothing, or a Parse error.
 */
Result<void> parseStateLetters(QByteArrayView letters, GitStatusEntry& entry)
{
    if (letters.size() != 2) {
        return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected git status letters: %1").arg(QString::fromUtf8(letters)));
    }
    return parseFileState(letters.front()).and_then([&](FileState index) {
        return parseFileState(letters.back()).map([&](FileState worktree) {
            entry.index = index;
            entry.worktree = worktree;
            entry.actions |= TGitPath::parseStatus(letters.front()) | TGitPath::parseStatus(letters.back());
        });
    });
}

/**
 * Parses a tracked file's record ('1', '2' or 'u').
 * @param record The record.
 * @param kind Its kind, which sets how many fields come before the path.
 * @returns The entry, without the rename source, or a Parse error.
 */
Result<GitStatusEntry> parseTracked(QByteArrayView record, GitStatusKind kind)
{
    const qsizetype fieldCount = kind == GitStatusKind::Changed ? changedFields : kind == GitStatusKind::Unmerged ? unmergedFields : renamedFields;
    return splitRecord(record, fieldCount).and_then([&](const Record& split) -> Result<GitStatusEntry> {
        GitStatusEntry entry{.kind = kind, .path = split.path, .origPath = {}, .index = {}, .worktree = {}, .actions = {}};
        if (kind == GitStatusKind::Unmerged) {
            entry.actions = TGitPath::Action::Unmerged;
        }
        return parseStateLetters(split.fields.at(1), entry).map([&] {
            return entry;
        });
    });
}

/**
 * Parses an untracked ('?') or ignored ('!') record.
 * @param record The record.
 * @param kind Untracked or Ignored.
 * @returns The entry, or a Parse error.
 */
Result<GitStatusEntry> parseUntracked(QByteArrayView record, GitStatusKind kind)
{
    return splitRecord(record, untrackedFields).map([&](const Record& split) {
        return GitStatusEntry{
            .kind = kind,
            .path = split.path,
            .origPath = {},
            .index = {},
            .worktree = {},
            .actions = kind == GitStatusKind::Ignored ? TGitPath::Action::Ignore : TGitPath::Action::Unversioned,
        };
    });
}

/**
 * @param record One record.
 * @returns Its entry, or a Parse error. A rename's source path is a separate record, filled in by the caller.
 */
Result<GitStatusEntry> parseRecord(QByteArrayView record)
{
    if (record.size() < 2 || record.at(1) != ' ') {
        return badRecord(record);
    }
    switch (record.front()) {
    case '1':
        return parseTracked(record, GitStatusKind::Changed);
    case '2':
        return parseTracked(record, GitStatusKind::RenamedOrCopied);
    case 'u':
        return parseTracked(record, GitStatusKind::Unmerged);
    case '?':
        return parseUntracked(record, GitStatusKind::Untracked);
    case '!':
        return parseUntracked(record, GitStatusKind::Ignored);
    default:
        return badRecord(record);
    }
}

} // namespace

namespace kgt
{

QStringList GitStatus::arguments(IgnoredFiles ignored)
{
    QStringList arguments{QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("-z"), QStringLiteral("--untracked-files=all")};
    if (ignored == IgnoredFiles::Include) {
        arguments.append(QStringLiteral("--ignored=matching"));
    }
    return arguments;
}

Result<QList<GitStatusEntry>> GitStatus::parse(const QByteArray& output)
{
    // Every record ends in NUL, so the last split is empty
    const QList<QByteArray> records = output.split('\0');
    QList<GitStatusEntry> entries;
    for (auto record = records.cbegin(); record != records.cend(); ++record) {
        // Skip the trailing empty split and any "# branch" headers
        if (record->isEmpty() || record->startsWith('#')) {
            continue;
        }
        Result<GitStatusEntry> entry = parseRecord(*record);
        if (!entry) {
            return tl::unexpected(entry.error());
        }
        if (entry->kind == GitStatusKind::RenamedOrCopied) {
            // With -z the source path is the next record on its own
            const auto source = std::next(record);
            if (source == records.cend() || source->isEmpty()) {
                return badRecord(*record);
            }
            entry->origPath = QString::fromUtf8(*source);
            record = source;
        }
        entries.append(std::move(*entry));
    }
    return entries;
}

QList<GitStatusEntry> GitStatus::status(const Git& git, IgnoredFiles ignored)
{
    return getOrThrow(parse(git.run(arguments(ignored), RunMode::ReadOnly)));
}

} // namespace kgt
