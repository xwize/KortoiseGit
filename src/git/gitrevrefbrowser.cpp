// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitRevRefBrowser.cpp (GetGitRevRefMap), with CGit::GetBranchDescriptions and
// CGit::GetRemoteList

#include "git/gitrevrefbrowser.hpp"

#include <QList>
#include <QTimeZone>

#include <array>

namespace
{

using namespace kgt;
using namespace Qt::StringLiterals;

/**
 * The for-each-ref fields, in order. TortoiseGit separates them with %04 and ends a record with %03; NUL separates
 * them here, and each record ends with one more NUL (git then adds a newline).
 */
constexpr qsizetype refNameField = 0;
constexpr qsizetype objectNameField = 1;
constexpr qsizetype objectTypeField = 2;
constexpr qsizetype upstreamField = 3;
constexpr qsizetype upstreamTrackField = 4; // "[gone]" when the upstream ref is gone; TortoiseGit lists the remote branches for that
constexpr qsizetype subjectField = 5;
constexpr qsizetype authorNameField = 6;
constexpr qsizetype authorDateField = 7;
constexpr qsizetype committerNameField = 8;
constexpr qsizetype committerDateField = 9;
constexpr qsizetype creatorField = 10; // An annotated tag's tagger, "Name <email> time zone"
constexpr qsizetype creatorDateField = 11;
constexpr qsizetype fieldCount = 12;

constexpr std::array<QLatin1StringView, fieldCount> fieldFormats{
    "%(refname)"_L1,
    "%(objectname)"_L1,
    "%(objecttype)"_L1,
    "%(upstream)"_L1,
    "%(upstream:track)"_L1,
    "%(subject)"_L1,
    "%(authorname)"_L1,
    "%(authordate:unix)"_L1,
    "%(committername)"_L1,
    "%(committerdate:unix)"_L1,
    "%(creator)"_L1,
    "%(creatordate:unix)"_L1,
};

constexpr auto branchPrefix = "branch."_L1;
constexpr auto descriptionSuffix = ".description"_L1;
constexpr auto remotePrefix = "remote."_L1;
constexpr auto urlSuffix = ".url"_L1;
constexpr auto headsPrefix = "refs/heads/"_L1;

/**
 * @param seconds A Unix time as git prints it.
 * @returns It as a date, or an invalid one if empty.
 */
QDateTime unixDate(const QString& seconds)
{
    bool isNumber = false;
    const qint64 value = seconds.toLongLong(&isNumber);
    return isNumber ? QDateTime::fromSecsSinceEpoch(value, QTimeZone::LocalTime) : QDateTime();
}

/**
 * @param objectType %(objecttype).
 * @returns Its git_object_t.
 */
GitObjectType objectType(const QString& objectType)
{
    if (objectType == "commit"_L1) {
        return GitObjectType::Commit;
    }
    if (objectType == "tag"_L1) {
        return GitObjectType::Tag;
    }
    if (objectType == "tree"_L1) {
        return GitObjectType::Tree;
    }
    return GitObjectType::Unknown;
}

/**
 * Builds one ref from its fields, as GetGitRevRefMap's loop body does.
 * @param fields The record's fields, fieldCount of them.
 * @returns The ref.
 */
GitRevRefBrowser makeRef(const QList<QString>& fields)
{
    GitRevRefBrowser ref{
        .hash = fields.at(objectNameField).trimmed(),
        .objectType = objectType(fields.at(objectTypeField).trimmed()),
        .upstreamRef = fields.at(upstreamField).trimmed(),
        .upstreamGone = fields.at(upstreamTrackField).trimmed() == "[gone]"_L1,
        .subject = fields.at(subjectField).trimmed(),
        .authorName = fields.at(authorNameField).trimmed(),
        .authorDate = unixDate(fields.at(authorDateField).trimmed()),
        .committerName = fields.at(committerNameField).trimmed(),
        .committerDate = unixDate(fields.at(committerDateField).trimmed()),
        .description = {},
    };
    if (ref.authorName.isEmpty()) {
        // An annotated tag: its tagger stands for author and committer
        const QString creator = fields.at(creatorField).trimmed();
        const qsizetype emailStart = creator.indexOf(" <"_L1);
        ref.authorName = emailStart >= 0 ? creator.left(emailStart) : creator;
        ref.authorDate = unixDate(fields.at(creatorDateField).trimmed());
        if (ref.committerName.isEmpty()) {
            ref.committerName = ref.authorName;
            ref.committerDate = ref.authorDate;
        }
    }
    return ref;
}

} // namespace

namespace kgt
{

QStringList GitRevRefBrowser::forEachRefArguments(RefMergeFilter filter)
{
    QStringList arguments{QStringLiteral("for-each-ref")};
    switch (filter) {
    case RefMergeFilter::All:
        break;
    case RefMergeFilter::Merged:
        arguments << QStringLiteral("--merged") << QStringLiteral("HEAD");
        break;
    case RefMergeFilter::Unmerged:
        arguments << QStringLiteral("--no-merged") << QStringLiteral("HEAD");
        break;
    }
    QString format;
    for (const QLatin1StringView field : fieldFormats) {
        format += field + "%00"_L1;
    }
    arguments.append(QStringLiteral("--format=") + format);
    // TODO: TortoiseGit applies .mailmap to the names (CGitMailmap); %(authorname:mailmap) needs git 2.47
    return arguments;
}

Result<QMap<QString, GitRevRefBrowser>> GitRevRefBrowser::parseRefMap(const QByteArray& output, const QHash<QString, QString>& descriptions)
{
    QMap<QString, GitRevRefBrowser> refs;
    QList<QByteArray> parts = output.split('\0');
    // The text after the last record's NUL: git's final newline
    if (parts.constLast().trimmed().isEmpty()) {
        parts.removeLast();
    }
    if (parts.size() % fieldCount != 0) {
        return makeError(ErrorCategory::Parse, QStringLiteral("git for-each-ref printed an incomplete record"));
    }
    for (qsizetype start = 0; start < parts.size(); start += fieldCount) {
        QList<QString> fields;
        fields.reserve(fieldCount);
        for (qsizetype i = 0; i < fieldCount; ++i) {
            fields.append(QString::fromUtf8(parts.at(start + i)));
        }
        // Each record after the first starts with the newline git prints after the one before
        const QString refName = fields.at(refNameField).trimmed();
        if (refName.isEmpty()) {
            continue;
        }
        GitRevRefBrowser ref = makeRef(fields);
        if (refName.startsWith(headsPrefix)) {
            ref.description = descriptions.value(refName.mid(headsPrefix.size()));
        }
        refs.insert(refName, ref);
    }
    return refs;
}

QStringList GitRevRefBrowser::configArguments()
{
    return {QStringLiteral("config"), QStringLiteral("-z"), QStringLiteral("--get-regexp"), QStringLiteral(R"(^(branch\..*\.description|remote\..*\.url)$)")};
}

RefBrowserConfig GitRevRefBrowser::parseConfig(const QByteArray& output)
{
    RefBrowserConfig config;
    // With -z each entry is "key\nvalue\0"
    for (const QByteArray& entry : output.split('\0')) {
        const qsizetype newline = entry.indexOf('\n');
        if (newline < 0) {
            continue;
        }
        const QString key = QString::fromUtf8(entry.left(newline));
        const QString value = QString::fromUtf8(entry.mid(newline + 1));
        if (key.startsWith(branchPrefix) && key.endsWith(descriptionSuffix)) {
            config.descriptions.insert(key.mid(branchPrefix.size(), key.size() - branchPrefix.size() - descriptionSuffix.size()), value);
        } else if (key.startsWith(remotePrefix) && key.endsWith(urlSuffix)) {
            const QString remote = key.mid(remotePrefix.size(), key.size() - remotePrefix.size() - urlSuffix.size());
            if (!config.remotes.contains(remote)) {
                config.remotes.append(remote);
            }
        }
    }
    config.remotes.sort();
    return config;
}

} // namespace kgt
