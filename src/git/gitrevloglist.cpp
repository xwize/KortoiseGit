// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitRevLoglist.cpp, src/Git/GitRev.cpp and CGit::GetLogCmd in src/Git/Git.cpp

#include "git/gitrevloglist.hpp"

#include <QByteArrayView>
#include <QLatin1StringView>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace
{

using namespace kgt;

/**
 * The fields of a log record, in the order of logFormat.
 */
enum class Field {
    Hash,
    Parents,
    AuthorName,
    AuthorEmail,
    AuthorDate,
    Decoration,
    Subject,
    Body,
    Count,
};

constexpr qsizetype fieldCount = static_cast<qsizetype>(Field::Count);

/**
 * Every field ends with a NUL, and -z ends the record with one more: a message can't hold a NUL, so the fields
 * are found without escaping.
 */
constexpr QLatin1StringView logFormat("--format=%H%x00%P%x00%an%x00%ae%x00%at%x00%D%x00%s%x00%b%x00");

/**
 * @param command The git command that wrote the output, for the message.
 * @param buffer The output.
 * @param pos Where the bad record starts.
 * @returns A Parse error quoting the start of the record.
 */
tl::unexpected<Error> badRecord(QLatin1StringView command, const QByteArray& buffer, qsizetype pos)
{
    constexpr qsizetype quoteLength = 80;
    QByteArray quote = buffer.mid(pos, quoteLength);
    quote.replace('\0', "\\0");
    return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected %1 record: %2").arg(command, QString::fromUtf8(quote)));
}

/**
 * @param text A field.
 * @returns Whether it is a full commit hash (SHA-1 or SHA-256).
 */
bool isHash(QByteArrayView text)
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

/**
 * Builds a commit from the fields of one record.
 * @param fields The record's fields, in Field order.
 * @returns The commit, or nothing if a field is malformed.
 */
std::optional<GitRevLoglist> makeRev(const std::array<QByteArrayView, fieldCount>& fields)
{
    const auto field = [&fields](Field which) {
        return fields.at(static_cast<std::size_t>(which));
    };
    if (!isHash(field(Field::Hash))) {
        return std::nullopt;
    }
    bool isNumber = false;
    const qint64 authorTime = field(Field::AuthorDate).toLongLong(&isNumber);
    if (!isNumber) {
        return std::nullopt;
    }
    GitRevLoglist rev;
    rev.hash = QString::fromLatin1(field(Field::Hash));
    rev.parents = QString::fromLatin1(field(Field::Parents)).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    rev.authorName = QString::fromUtf8(field(Field::AuthorName));
    rev.authorEmail = QString::fromUtf8(field(Field::AuthorEmail));
    rev.authorDate = QDateTime::fromSecsSinceEpoch(authorTime);
    rev.refs = GitRevLoglist::parseRefs(QString::fromUtf8(field(Field::Decoration)));
    rev.subject = QString::fromUtf8(field(Field::Subject));
    QByteArrayView body = field(Field::Body);
    while (body.endsWith('\n') || body.endsWith('\r')) {
        body.chop(1);
    }
    rev.body = QString::fromUtf8(body);
    return rev;
}

/**
 * Reads up to the next NUL.
 * @param output The whole output.
 * @param[in,out] pos Where the field starts; moved past the NUL.
 * @returns The field, or nothing if the NUL is missing or the field is empty.
 */
std::optional<QString> takeNulField(const QByteArray& output, qsizetype& pos)
{
    const qsizetype end = output.indexOf('\0', pos);
    if (end <= pos) {
        return std::nullopt;
    }
    QString field = QString::fromUtf8(QByteArrayView(output).sliced(pos, end - pos));
    pos = end + 1;
    return field;
}

/**
 * Parses one raw diff record: ":srcmode dstmode srcsha dstsha status\0path\0", with the source path first for a
 * rename or copy (CTGitPathList::ParserFromLog's raw branch).
 * @param output The whole output.
 * @param[in,out] pos Where the record starts, at its ':'; moved past it.
 * @returns The entry, or nothing if the record is malformed.
 */
std::optional<GitStatusEntry> takeRawRecord(const QByteArray& output, qsizetype& pos)
{
    const qsizetype headerEnd = output.indexOf('\0', pos);
    if (headerEnd < 0) {
        return std::nullopt;
    }
    const QByteArrayView header = QByteArrayView(output).sliced(pos, headerEnd - pos);
    const qsizetype statusPos = header.lastIndexOf(' ') + 1;
    if (statusPos <= 0 || statusPos >= header.size()) {
        return std::nullopt;
    }
    const char status = header.at(statusPos);
    pos = headerEnd + 1;

    GitStatusEntry entry;
    entry.actions = TGitPath::parseStatus(status);
    if (!entry.actions) {
        return std::nullopt;
    }
    std::optional<QString> path = takeNulField(output, pos);
    if (!path) {
        return std::nullopt;
    }
    if (status == 'R' || status == 'C') {
        entry.kind = GitStatusKind::RenamedOrCopied;
        entry.origPath = std::move(*path);
        path = takeNulField(output, pos);
        if (!path) {
            return std::nullopt;
        }
    }
    entry.path = std::move(*path);
    return entry;
}

} // namespace

namespace kgt
{

QStringList GitRevLoglist::logArguments(const QStringList& paths, const QString& range, bool noMerges)
{
    QStringList arguments{
        QStringLiteral("log"),
        QStringLiteral("--topo-order"),
        QStringLiteral("-z"),
        QStringLiteral("--decorate=full"),
        QStringLiteral("--no-show-signature"),
        QStringLiteral("--no-color"),
        QStringLiteral("--encoding=UTF-8"),
        // CGit::GetLogCmd without LOG_INFO_FULL_HISTORY: rewrites %P to the parents that are listed when a path
        // limits the log, so the graph's lanes join up
        QStringLiteral("--parents"),
        logFormat,
    };
    if (noMerges) {
        arguments.append(QStringLiteral("--no-merges"));
    }
    if (!range.isEmpty()) {
        arguments.append({QStringLiteral("--end-of-options"), range});
    }
    arguments.append(QStringLiteral("--"));
    arguments.append(paths);
    return arguments;
}

Result<QList<GitRevLoglist>> GitRevLoglist::takeRecords(QByteArray& buffer)
{
    QList<GitRevLoglist> revs;
    qsizetype pos = 0;
    while (pos < buffer.size()) {
        std::array<QByteArrayView, fieldCount> fields;
        qsizetype fieldStart = pos;
        bool isComplete = true;
        for (QByteArrayView& field : fields) {
            const qsizetype end = buffer.indexOf('\0', fieldStart);
            if (end < 0) {
                isComplete = false;
                break;
            }
            field = QByteArrayView(buffer).sliced(fieldStart, end - fieldStart);
            fieldStart = end + 1;
        }
        // The record's own terminator follows the last field
        if (!isComplete || fieldStart >= buffer.size()) {
            break;
        }
        if (buffer.at(fieldStart) != '\0') {
            return badRecord(QLatin1StringView("git log"), buffer, pos);
        }
        std::optional<GitRevLoglist> rev = makeRev(fields);
        if (!rev) {
            return badRecord(QLatin1StringView("git log"), buffer, pos);
        }
        revs.append(std::move(*rev));
        pos = fieldStart + 1;
    }
    buffer.remove(0, pos);
    return revs;
}

QList<GitRef> GitRevLoglist::parseRefs(const QString& decoration)
{
    static const QString headArrow = QStringLiteral("HEAD -> ");
    static const QString tagPrefix = QStringLiteral("tag: ");
    static const QString headsPrefix = QStringLiteral("refs/heads/");
    static const QString remotesPrefix = QStringLiteral("refs/remotes/");
    static const QString tagsPrefix = QStringLiteral("refs/tags/");

    QList<GitRef> refs;
    // Ref names can't hold a space, so ", " only ever separates them
    const QStringList items = decoration.split(QStringLiteral(", "), Qt::SkipEmptyParts);
    for (QString item : items) {
        GitRef ref;
        if (item == QLatin1String("HEAD")) {
            refs.append(GitRef{.type = GitRef::Type::Head, .name = item, .isCurrent = true});
            continue;
        }
        if (item.startsWith(headArrow)) {
            ref.isCurrent = true;
            item.remove(0, headArrow.size());
        }
        if (item.startsWith(tagPrefix)) {
            item.remove(0, tagPrefix.size());
        }
        if (item.startsWith(headsPrefix)) {
            ref.type = GitRef::Type::LocalBranch;
            ref.name = item.mid(headsPrefix.size());
        } else if (item.startsWith(remotesPrefix)) {
            ref.type = GitRef::Type::RemoteBranch;
            ref.name = item.mid(remotesPrefix.size());
        } else if (item.startsWith(tagsPrefix)) {
            ref.type = GitRef::Type::Tag;
            ref.name = item.mid(tagsPrefix.size());
        } else if (item == QLatin1String("refs/stash")) {
            ref.type = GitRef::Type::Stash;
            ref.name = QStringLiteral("stash");
        } else {
            ref.name = item;
        }
        refs.append(std::move(ref));
    }
    return refs;
}

qsizetype GitRevLoglist::diffCount() const
{
    return std::max<qsizetype>(parents.size(), 1);
}

QStringList GitRevLoglist::filesArguments(const GitRevLoglist& rev, qsizetype parentNo)
{
    Q_ASSERT(parentNo >= 0 && parentNo < rev.diffCount());
    // CGit::GetGitDiff's command line, with TortoiseGit's default similarity index (50%) for renames and copies
    QStringList arguments{
        QStringLiteral("diff-tree"),
        QStringLiteral("-r"),
        QStringLiteral("--raw"),
        QStringLiteral("--numstat"),
        QStringLiteral("-z"),
        QStringLiteral("-M50%"),
        QStringLiteral("-C50%"),
        QStringLiteral("--no-commit-id"),
        QStringLiteral("--end-of-options"),
    };
    if (rev.parents.isEmpty()) {
        arguments.insert(1, QStringLiteral("--root"));
    } else {
        arguments.append(rev.parents.at(parentNo));
    }
    arguments.append({rev.hash, QStringLiteral("--")});
    return arguments;
}

Result<GitRevFiles> GitRevLoglist::parseFiles(const QByteArray& output)
{
    // The raw records all come first, each starting with ':', then the --numstat records
    GitRevFiles files;
    qsizetype pos = 0;
    while (pos < output.size() && output.at(pos) == ':') {
        const qsizetype start = pos;
        std::optional<GitStatusEntry> entry = takeRawRecord(output, pos);
        if (!entry) {
            return badRecord(QLatin1StringView("git diff-tree"), output, start);
        }
        files.status.append(std::move(*entry));
    }
    Result<QList<NumStatEntry>> numStat = GitNumStat::parse(output.mid(pos));
    if (!numStat) {
        return tl::unexpected(std::move(numStat).error());
    }
    files.numStat = std::move(*numStat);
    return files;
}

} // namespace kgt
