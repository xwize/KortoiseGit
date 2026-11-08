// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/LogDlgFilter.cpp

#include "logdlgfilter.hpp"

#include "git/gitrevloglist.hpp"

namespace
{

using namespace kgt;

/**
 * @param[in,out] scratch The text being built.
 * @param field One field of it, ended with a newline.
 */
void addField(QString& scratch, const QString& field)
{
    scratch += field;
    scratch += QLatin1Char('\n');
}

/**
 * Adds the fields of the commit itself to the text to search.
 * @param[in,out] scratch The text being built.
 * @param rev The commit.
 * @param selected The LogFilter bits.
 */
void addCommitFields(QString& scratch, const GitRevLoglist& rev, unsigned int selected)
{
    if (selected & (LogFilterSubject | LogFilterMessages)) {
        addField(scratch, rev.subject);
    }
    if (selected & LogFilterMessages) {
        addField(scratch, rev.body);
    }
    if (selected & LogFilterAuthors) {
        addField(scratch, rev.authorName);
        addField(scratch, rev.committerName);
    }
    if (selected & LogFilterEmails) {
        addField(scratch, rev.authorEmail);
        addField(scratch, rev.committerEmail);
    }
    if (selected & LogFilterRevs) {
        addField(scratch, rev.hash);
    }
    if (selected & LogFilterNotes) {
        addField(scratch, rev.notes);
    }
}

/**
 * Adds the refs pointing at the commit to the text to search. TortoiseGit's map of refs has no HEAD.
 * @param[in,out] scratch The text being built.
 * @param rev The commit.
 * @param selected The LogFilter bits.
 * @param tagInfo The annotated tags' text by full ref name.
 */
void addRefFields(QString& scratch, const GitRevLoglist& rev, unsigned int selected, const QHash<QString, QString>& tagInfo)
{
    if (selected & LogFilterRefName) {
        for (const GitRef& ref : rev.refs) {
            if (ref.type != GitRef::Type::Head) {
                addField(scratch, ref.fullName());
            }
        }
    }
    if (selected & LogFilterAnnotatedTag) {
        for (const GitRef& ref : rev.refs) {
            if (ref.type == GitRef::Type::Tag) {
                scratch += tagInfo.value(ref.fullName());
            }
        }
        scratch += QLatin1Char('\n');
    }
}

} // namespace

namespace kgt
{

bool LogDlgFilter::needsPaths() const
{
    return isFilterActive() && (selectedFilters() & LogFilterPaths);
}

bool LogDlgFilter::needsTagInfo() const
{
    return isFilterActive() && (selectedFilters() & LogFilterAnnotatedTag);
}

bool LogDlgFilter::operator()(const GitRevLoglist& rev, const QStringList& paths, const QHash<QString, QString>& tagInfo) const
{
    if (!isFilterActive()) {
        return calculateFinalResult(true);
    }
    QString scratch;
    addCommitFields(scratch, rev, selectedFilters());
    addRefFields(scratch, rev, selectedFilters(), tagInfo);
    if (selectedFilters() & LogFilterPaths) {
        for (const QString& path : paths) {
            addField(scratch, path);
        }
    }
    return calculateFinalResult(match(scratch));
}

} // namespace kgt
