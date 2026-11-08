// SPDX-FileCopyrightText: 2005-2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2021-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/CachedDirectory.cpp

#include "cacheddirectory.hpp"

#include "gitstatuscache.hpp"

#include <QLatin1Char>

namespace
{

using namespace kgt;

/**
 * Adds one member's state to a folder's status: GetMoreImportant and AdjustFolderStatus.
 * @param[in,out] folder The status so far.
 * @param status The member's state.
 */
void addMember(RecursiveStatus& folder, GitWcStatus status)
{
    if (status == GitWcStatus::Unversioned) {
        folder.hasUntracked = true;
    } else if (status == GitWcStatus::Ignored) {
        folder.hasIgnored = true;
    } else {
        folder.change = GitStatus::moreImportant(folder.change, GitStatus::adjustFolderStatus(status));
    }
}

} // namespace

namespace kgt
{

CachedDirectory::CachedDirectory(GitStatusCache& cache, const QString& directoryPath, bool isRoot)
    : m_cache(&cache)
    , m_directoryPath(directoryPath)
    , m_isRoot(isRoot)
{
}

void CachedDirectory::rememberMember(const QString& name)
{
    if (!m_refreshedMembers.contains(name)) {
        m_refreshedMembers.insert(name, memberStatus(name));
    }
}

void CachedDirectory::beginRefresh()
{
    for (auto it = m_entryCache.cbegin(); it != m_entryCache.cend(); ++it) {
        rememberMember(it.key());
    }
    m_entryCache.clear();
}

void CachedDirectory::clearMember(const QString& name)
{
    rememberMember(name);
    m_entryCache.remove(name);
}

void CachedDirectory::addEntry(const QString& name, GitWcStatus status)
{
    rememberMember(name);
    const auto current = m_entryCache.constFind(name);
    m_entryCache.insert(name, current == m_entryCache.constEnd() ? status : GitStatus::moreImportant(*current, status));
}

std::optional<GitWcStatus> CachedDirectory::memberStatus(const QString& name) const
{
    const auto found = m_entryCache.constFind(name);
    return found == m_entryCache.constEnd() ? std::nullopt : std::optional(*found);
}

void CachedDirectory::clearTrackedChildren()
{
    m_trackedChildren.clear();
}

void CachedDirectory::addTrackedChild(const QString& name)
{
    m_trackedChildren.insert(name);
}

void CachedDirectory::finishRefresh(bool notify)
{
    if (notify) {
        for (auto it = m_refreshedMembers.cbegin(); it != m_refreshedMembers.cend(); ++it) {
            if (it.value() != memberStatus(it.key())) {
                m_cache->updateShell(m_directoryPath + QLatin1Char('/') + it.key());
            }
        }
    }
    m_refreshedMembers.clear();
}

RecursiveStatus CachedDirectory::calculateRecursiveStatus() const
{
    RecursiveStatus result;
    for (const GitWcStatus status : m_entryCache) {
        addMember(result, status);
    }
    for (const RecursiveStatus& child : m_childDirectories) {
        result.change = GitStatus::moreImportant(result.change, child.change);
        result.hasUntracked = result.hasUntracked || child.hasUntracked;
        result.hasIgnored = result.hasIgnored || child.hasIgnored;
    }
    return result;
}

void CachedDirectory::updateCurrentStatus(bool notify)
{
    const RecursiveStatus newStatus = calculateRecursiveStatus();
    if (newStatus != m_currentFullStatus) {
        m_currentFullStatus = newStatus;
        if (notify) {
            m_cache->updateShell(m_directoryPath);
        }
    }
    // "We tell our parent *always* about our status": it decides itself whether anything changed. A working tree's
    // top folder has none, as a submodule's status doesn't reach the repository around it through the cache
    if (!m_isRoot) {
        m_cache->tellParent(m_directoryPath, m_currentFullStatus, notify);
    }
}

void CachedDirectory::updateChildDirectoryStatus(const QString& name, const RecursiveStatus& status, bool notify)
{
    if (m_childDirectories.value(name) == status) {
        return;
    }
    if (status == RecursiveStatus{}) {
        m_childDirectories.remove(name);
    } else {
        m_childDirectories.insert(name, status);
    }
    updateCurrentStatus(notify);
}

CacheFolderStatus CachedDirectory::folderStatus() const
{
    CacheFolderStatus result;
    result.hasUnversioned = m_currentFullStatus.hasUntracked;
    const auto add = [&result](const QString& name, GitWcStatus status, bool hasUntracked) {
        result.names.append(name);
        result.states.append(packItemState(CacheItemState{.status = status, .hasUntracked = hasUntracked}));
    };
    for (auto it = m_entryCache.cbegin(); it != m_entryCache.cend(); ++it) {
        add(it.key(), it.value(), m_childDirectories.value(it.key()).hasUntracked);
    }
    // As GitFolderStatus::itemVersion rolls up a folder
    for (auto it = m_childDirectories.cbegin(); it != m_childDirectories.cend(); ++it) {
        if (m_entryCache.contains(it.key())) {
            continue;
        }
        const RecursiveStatus& child = it.value();
        GitWcStatus status = GitWcStatus::Unversioned;
        if (child.change != GitWcStatus::None) {
            status = child.change;
        } else if (m_trackedChildren.contains(it.key())) {
            status = GitWcStatus::Normal;
        } else if (!child.hasUntracked && child.hasIgnored) {
            status = GitWcStatus::Ignored;
        }
        add(it.key(), status, child.hasUntracked);
    }
    for (const QString& name : m_trackedChildren) {
        if (!m_entryCache.contains(name) && !m_childDirectories.contains(name)) {
            add(name, GitWcStatus::Normal, false);
        }
    }
    return result;
}

} // namespace kgt
