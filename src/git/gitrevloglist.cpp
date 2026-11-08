// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitRevLoglist.cpp, src/Git/GitRev.cpp and CGit::GetLogCmd in src/Git/Git.cpp

#include "git/gitrevloglist.hpp"

#include "git/git.hpp"

#include <QByteArrayView>
#include <QLatin1StringView>
#include <QLocale>

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
    CommitterName,
    CommitterEmail,
    CommitDate,
    Decoration,
    Subject,
    Body,
    Notes,
    Count,
};

constexpr qsizetype fieldCount = static_cast<qsizetype>(Field::Count);

constexpr char simpleListMarker = '\x01'; // Starts each commit's header in simpleListArguments()' output
constexpr QLatin1StringView tagPrefix("refs/tags/");

/**
 * Every field ends with a NUL, and -z ends the record with one more: a message can't hold a NUL, so the fields
 * are found without escaping.
 */
constexpr QLatin1StringView logFormat("--format=%H%x00%P%x00%an%x00%ae%x00%at%x00%cn%x00%ce%x00%ct%x00%D%x00%s%x00%b%x00%N%x00");

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
 * @param text A field of git's output.
 * @returns It as text, without its trailing newlines.
 */
QString withoutTrailingNewlines(QByteArrayView text)
{
    while (text.endsWith('\n') || text.endsWith('\r')) {
        text.chop(1);
    }
    return QString::fromUtf8(text);
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
    if (!Git::isHash(field(Field::Hash))) {
        return std::nullopt;
    }
    bool isNumber = false;
    const qint64 authorTime = field(Field::AuthorDate).toLongLong(&isNumber);
    if (!isNumber) {
        return std::nullopt;
    }
    const qint64 commitTime = field(Field::CommitDate).toLongLong(&isNumber);
    if (!isNumber) {
        return std::nullopt;
    }
    GitRevLoglist rev;
    rev.hash = QString::fromLatin1(field(Field::Hash));
    rev.parents = QString::fromLatin1(field(Field::Parents)).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    rev.authorName = QString::fromUtf8(field(Field::AuthorName));
    rev.authorEmail = QString::fromUtf8(field(Field::AuthorEmail));
    rev.authorDate = QDateTime::fromSecsSinceEpoch(authorTime);
    rev.committerName = QString::fromUtf8(field(Field::CommitterName));
    rev.committerEmail = QString::fromUtf8(field(Field::CommitterEmail));
    rev.commitDate = QDateTime::fromSecsSinceEpoch(commitTime);
    rev.refs = GitRevLoglist::parseRefs(QString::fromUtf8(field(Field::Decoration)));
    rev.subject = QString::fromUtf8(field(Field::Subject));
    rev.body = withoutTrailingNewlines(field(Field::Body));
    rev.notes = withoutTrailingNewlines(field(Field::Notes));
    return rev;
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
    std::optional<QString> path = Git::takeField(output, pos, '\0');
    if (!path) {
        return std::nullopt;
    }
    if (status == 'R' || status == 'C') {
        entry.kind = GitStatusKind::RenamedOrCopied;
        entry.origPath = std::move(*path);
        path = Git::takeField(output, pos, '\0');
        if (!path) {
            return std::nullopt;
        }
    }
    entry.path = std::move(*path);
    return entry;
}

/**
 * Records an annotated tag's text as CGit::GetTagInfo writes it.
 * @param[in,out] infos The tags so far, by full ref name.
 * @param fields The tag's for-each-ref fields: ref name, object type, tagger name, email and date, message.
 */
void addTagInfo(QHash<QString, QString>& infos, const std::array<QByteArrayView, 6>& fields)
{
    const QString refName = QString::fromUtf8(fields.at(0));
    QString info = QStringLiteral("tag ") + refName.mid(tagPrefix.size()) + QLatin1Char('\n');
    const QString taggerName = QString::fromUtf8(fields.at(2));
    const QString taggerEmail = QString::fromUtf8(fields.at(3));
    if (!taggerName.isEmpty() || !taggerEmail.isEmpty()) {
        const QDateTime when = QDateTime::fromSecsSinceEpoch(fields.at(4).toLongLong());
        info += QStringLiteral("tagger %1 %2 %3\n").arg(taggerName, taggerEmail, QLocale().toString(when, QLocale::ShortFormat));
    }
    info += QLatin1Char('\n') + withoutTrailingNewlines(fields.at(5)) + QLatin1Char('\n');
    infos.insert(refName, info);
}

} // namespace

namespace kgt
{

QStringList GitRevLoglist::logArguments(const QStringList& paths, const QString& range, const GitLogOptions& options)
{
    QStringList arguments{
        QStringLiteral("log"),
        QStringLiteral("--topo-order"),
        QStringLiteral("-z"),
        QStringLiteral("--decorate=full"),
        QStringLiteral("--no-show-signature"),
        QStringLiteral("--no-color"),
        QStringLiteral("--encoding=UTF-8"),
        logFormat,
    };
    // CGit::GetLogCmd's mask
    if (options.firstParent) {
        arguments.append(QStringLiteral("--first-parent"));
    }
    if (options.noMerges) {
        arguments.append(QStringLiteral("--no-merges"));
    }
    // Without full history, --parents rewrites %P to the parents that are listed when a path limits the log, so the
    // graph's lanes join up
    arguments.append(options.fullHistory ? QStringLiteral("--full-history") : QStringLiteral("--parents"));
    if (options.follow) {
        arguments.append(QStringLiteral("--follow"));
    }
    if (options.from > 0) {
        arguments.append(QStringLiteral("--max-age=%1").arg(options.from));
    }
    if (options.to != -1) {
        arguments.append(QStringLiteral("--min-age=%1").arg(options.to));
    }
    if (!range.isEmpty()) {
        arguments.append({QStringLiteral("--end-of-options"), range});
    }
    arguments.append(QStringLiteral("--"));
    if (!paths.isEmpty()) {
        // A file may be named like a pathspec, such as ":x" or "*"
        arguments.prepend(QStringLiteral("--literal-pathspecs"));
        arguments.append(paths);
    }
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

QString GitRef::fullName() const
{
    switch (type) {
    case Type::LocalBranch:
        return QStringLiteral("refs/heads/") + name;
    case Type::RemoteBranch:
        return QStringLiteral("refs/remotes/") + name;
    case Type::Tag:
        return tagPrefix + name;
    case Type::Stash:
        return QStringLiteral("refs/stash");
    case Type::Head:
    case Type::Other:
        break;
    }
    return name;
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

QStringList GitRevLoglist::simpleListArguments()
{
    // One diff per parent and the root commit against nothing, without rename detection (SafeGetSimpleList)
    return {
        QStringLiteral("diff-tree"),
        QStringLiteral("--stdin"),
        QStringLiteral("-r"),
        QStringLiteral("-m"),
        QStringLiteral("--root"),
        QStringLiteral("--no-renames"),
        QStringLiteral("--name-only"),
        QStringLiteral("-z"),
        QStringLiteral("--format=%x01%H"),
    };
}

QHash<QString, QStringList> GitRevLoglist::parseSimpleList(const QByteArray& output)
{
    // <marker><hash> NUL newline <path> NUL <path> NUL ..., repeated for each parent of a merge
    QHash<QString, QStringList> lists;
    QStringList *current = nullptr;
    bool isFirstPath = false;
    qsizetype pos = 0;
    while (pos < output.size()) {
        qsizetype end = output.indexOf('\0', pos);
        if (end < 0) {
            end = output.size();
        }
        QByteArrayView token = QByteArrayView(output).sliced(pos, end - pos);
        pos = end + 1;
        if (token.startsWith(simpleListMarker)) {
            const QString hash = QString::fromLatin1(token.sliced(1));
            auto entry = lists.find(hash); // A merge has a header per parent
            if (entry == lists.end()) {
                entry = lists.insert(hash, {});
            }
            current = &entry.value();
            isFirstPath = true;
            continue;
        }
        if (isFirstPath && token.startsWith('\n')) {
            token = token.sliced(1);
        }
        isFirstPath = false;
        if (current && !token.isEmpty()) {
            current->append(QString::fromUtf8(token));
        }
    }
    return lists;
}

QStringList GitRevLoglist::tagInfoArguments()
{
    return {
        QStringLiteral("for-each-ref"),
        QStringLiteral("--format=%(refname)%00%(objecttype)%00%(taggername)%00%(taggeremail)%00%(taggerdate:unix)%00%(contents)%00%00"),
        tagPrefix,
    };
}

QHash<QString, QString> GitRevLoglist::parseTagInfo(const QByteArray& output)
{
    constexpr std::size_t tagFieldCount = 6;
    QHash<QString, QString> infos;
    qsizetype pos = 0;
    while (pos < output.size()) {
        std::array<QByteArrayView, tagFieldCount> fields;
        for (QByteArrayView& field : fields) {
            const qsizetype end = output.indexOf('\0', pos);
            if (end < 0) {
                return infos;
            }
            field = QByteArrayView(output).sliced(pos, end - pos);
            pos = end + 1;
        }
        pos += 2; // The record's own NUL and the newline for-each-ref ends it with
        if (fields.at(1) != "tag") {
            continue; // A lightweight tag has no tag object to read
        }
        addTagInfo(infos, fields);
    }
    return infos;
}

} // namespace kgt
