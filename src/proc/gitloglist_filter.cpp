// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp: which commits of the walk LogThread lists

#include "gitloglist.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "utils/runtimeerror.hpp"

#include <algorithm>
#include <utility>

namespace
{

constexpr qsizetype simpleListPageSize = 512; // Commits per diff-tree, so a filtered list keeps growing as it loads

} // namespace

namespace kgt
{

void GitLogList::setWalkOptions(const GitLogOptions& options)
{
    m_walkOptions = options;
}

const GitLogOptions& GitLogList::walkOptions() const
{
    return m_walkOptions;
}

void GitLogList::setShowFilter(unsigned int showFilter)
{
    m_showFilter = showFilter;
}

void GitLogList::setLogFilter(const LogDlgFilter& filter)
{
    m_logFilter = filter;
}

const LogDlgFilter& GitLogList::logFilter() const
{
    return m_logFilter;
}

std::pair<QDateTime, QDateTime> GitLogList::timeRange() const
{
    return {m_oldestCommit, std::max(m_oldestCommit, m_latestCommit)};
}

void GitLogList::finishLoadIfDone()
{
    if (!m_isLoading || m_logCall || m_tagInfoCall || m_simpleListCall || !m_simpleListQueue.isEmpty()) {
        return;
    }
    m_isLoading = false;
    viewport()->update();
    Q_EMIT loaded();
}

void GitLogList::handleRevs(const QList<GitRevLoglist>& revs)
{
    for (const GitRevLoglist& rev : revs) {
        m_oldestCommit = std::min(m_oldestCommit, rev.commitDate);
        m_latestCommit = std::max(m_latestCommit, rev.commitDate);
    }
    if (m_logFilter.needsPaths()) {
        m_simpleListQueue.append(revs);
        startNextSimpleLists();
        return;
    }
    showRevs(revs);
}

void GitLogList::showRevs(const QList<GitRevLoglist>& revs)
{
    const int firstRow = m_model->rowCount();
    if (m_showFilter == FilterShowAll && !m_logFilter.isFilterActive()) {
        m_model->appendRevs(revs);
    } else {
        QList<bool> visible;
        visible.reserve(revs.size());
        for (const GitRevLoglist& rev : revs) {
            // LogThread: the walk's filter, then the search box
            visible.append(shouldShowRevInGraph(rev) && m_logFilter(rev, m_simpleLists.value(rev.hash), m_tagInfo));
        }
        m_model->appendFiltered(revs, visible);
    }
    restoreSelection(firstRow);
}

bool GitLogList::shouldShowRevInGraph(const GitRevLoglist& rev)
{
    // CGitLogListBase::ShouldShowFilter; commitChildren is only filled for FILTERSHOW_MERGEPOINTS
    if (m_showFilter & FilterShowMergePoints) {
        for (const QString& parent : rev.parents) {
            m_childCount.insert(parent, m_childCount.value(parent) + 1);
        }
    }
    if ((m_showFilter & FilterShowAnyCommit) || shouldShowRefs(rev)) {
        return true;
    }
    return (m_showFilter & FilterShowMergePoints) && (rev.parents.size() > 1 || m_childCount.value(rev.hash) > 1);
}

bool GitLogList::shouldShowRefs(const GitRevLoglist& rev) const
{
    // CGitLogListBase::ShouldShowRefsFilter
    if (!(m_showFilter & FilterShowRefs)) {
        return false;
    }
    return std::ranges::any_of(rev.refs, [this](const GitRef& ref) {
        if (ref.isCurrent) {
            return true; // Keep the head
        }
        const bool counts = ref.type != GitRef::Type::Other || ref.name.startsWith(QLatin1String("refs/bisect/"));
        return counts && m_model->isRefShown(ref);
    });
}

void GitLogList::restoreSelection(int firstRow)
{
    if (m_lastSelectedHash.isEmpty()) {
        return;
    }
    for (int row = firstRow; row < m_model->rowCount(); ++row) {
        if (m_model->rev(row).hash != m_lastSelectedHash) {
            continue;
        }
        m_lastSelectedHash.clear();
        const QModelIndex cell = m_model->index(row, GitLogListModel::MessageColumn);
        selectionModel()->setCurrentIndex(cell, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        scrollTo(cell);
        return;
    }
}

void GitLogList::onTagInfoFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_tagInfoCall.release();
    Q_ASSERT(call);
    call->deleteLater();
    try {
        m_tagInfo = GitRevLoglist::parseTagInfo(call->output());
    } catch (const RuntimeError& e) {
        // As in TortoiseGit, a tag that can't be read just doesn't match
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    startLog();
}

void GitLogList::startNextSimpleLists()
{
    while (!m_simpleListCall && !m_simpleListQueue.isEmpty()) {
        const qsizetype pageSize = std::min(simpleListPageSize, m_simpleListQueue.size());
        QByteArray hashes;
        for (qsizetype i = 0; i < pageSize; ++i) {
            const QString& hash = m_simpleListQueue.at(i).hash;
            if (!m_simpleLists.contains(hash)) {
                hashes.append(hash.toLatin1());
                hashes.append('\n');
            }
        }
        if (hashes.isEmpty()) {
            showQueuedRevs(pageSize);
            continue;
        }
        m_simpleListPage = pageSize;
        m_simpleListCall = Git(m_workingTree).runAsync(GitRevLoglist::simpleListArguments(), RunMode::ReadOnly, hashes);
        connect(m_simpleListCall.get(), &GitCall::finished, this, &GitLogList::onSimpleListFinished);
    }
    finishLoadIfDone();
}

void GitLogList::onSimpleListFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_simpleListCall.release();
    Q_ASSERT(call);
    call->deleteLater();
    try {
        m_simpleLists.insert(GitRevLoglist::parseSimpleList(call->output()));
        // A commit that changes nothing gets no output; record that, so it isn't asked for again
        for (qsizetype i = 0; i < m_simpleListPage; ++i) {
            const QString& hash = m_simpleListQueue.at(i).hash;
            if (!m_simpleLists.contains(hash)) {
                m_simpleLists.insert(hash, {});
            }
        }
    } catch (const RuntimeError& e) {
        // As SafeGetSimpleList's failure in TortoiseGit: the commits are matched without their paths
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    showQueuedRevs(std::exchange(m_simpleListPage, 0));
    startNextSimpleLists();
}

void GitLogList::showQueuedRevs(qsizetype count)
{
    const QList<GitRevLoglist> page = m_simpleListQueue.first(count);
    m_simpleListQueue.remove(0, count);
    showRevs(page);
}

} // namespace kgt
