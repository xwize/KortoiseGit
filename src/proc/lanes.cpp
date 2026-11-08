// SPDX-FileCopyrightText: 2005-2007 Marco Costalba
// SPDX-FileCopyrightText: 2008-2015, 2017, 2022 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only

// Ported from TortoiseGit src/TortoiseProc/lanes.cpp (history graph computation, from QGit). GPL-2.0-only.

#include "lanes.hpp"

#include <algorithm>
#include <cstddef>

namespace kgt
{

bool Lanes::isHead(LaneType type)
{
    return type == LaneType::Head || type == LaneType::HeadR || type == LaneType::HeadL;
}

bool Lanes::isTail(LaneType type)
{
    return type == LaneType::Tail || type == LaneType::TailR || type == LaneType::TailL;
}

bool Lanes::isJoin(LaneType type)
{
    return type == LaneType::Join || type == LaneType::JoinR || type == LaneType::JoinL;
}

bool Lanes::isFreeLane(LaneType type)
{
    return type == LaneType::NotActive || type == LaneType::Cross || isJoin(type);
}

bool Lanes::isBoundary(LaneType type)
{
    return type == LaneType::Boundary || type == LaneType::BoundaryC || type == LaneType::BoundaryR || type == LaneType::BoundaryL;
}

bool Lanes::isMerge(LaneType type)
{
    return type == LaneType::MergeFork || type == LaneType::MergeForkR || type == LaneType::MergeForkL || isBoundary(type);
}

bool Lanes::isActive(LaneType type)
{
    return type == LaneType::Active || type == LaneType::Initial || type == LaneType::Branch || isMerge(type);
}

bool Lanes::isEmpty() const
{
    return m_typeVec.empty();
}

void Lanes::init(const QString& expectedSha)
{
    clear();
    m_activeLane = 0;
    setBoundary(false, false);
    bool wasEmptyCross = false;
    add(LaneType::Branch, expectedSha, m_activeLane, wasEmptyCross);
}

void Lanes::clear()
{
    m_typeVec.clear();
    m_nextShaVec.clear();
}

void Lanes::setBoundary(bool isBoundary, bool isInitial)
{
    // Changes the state, so must be called as first one
    m_node = isBoundary ? LaneType::BoundaryC : LaneType::MergeFork;
    m_nodeR = isBoundary ? LaneType::BoundaryR : LaneType::MergeForkR;
    if (isBoundary) {
        m_nodeL = LaneType::BoundaryL;
    } else {
        m_nodeL = isInitial ? LaneType::MergeForkLInitial : LaneType::MergeForkL;
    }
    m_boundary = isBoundary;

    if (m_boundary) {
        typeAt(m_activeLane) = LaneType::Boundary;
    }
}

bool Lanes::isFork(const QString& sha, bool& isDiscontinuity) const
{
    const int pos = findNextSha(sha, 0);
    isDiscontinuity = m_activeLane != pos;
    if (pos == -1) { // New branch case
        return false;
    }
    return findNextSha(sha, pos + 1) != -1;
}

void Lanes::setFork(const QString& sha)
{
    const int rangeStart = findNextSha(sha, 0);
    int rangeEnd = rangeStart;
    int idx = rangeStart;
    while (idx != -1) {
        rangeEnd = idx;
        typeAt(idx) = LaneType::Tail;
        idx = findNextSha(sha, idx + 1);
    }
    typeAt(m_activeLane) = m_node;

    LaneType& startT = typeAt(rangeStart);
    LaneType& endT = typeAt(rangeEnd);

    if (startT == m_node) {
        startT = m_nodeL;
    }
    if (endT == m_node) {
        endT = m_nodeR;
    }
    if (startT == LaneType::Tail) {
        startT = LaneType::TailL;
    }
    if (endT == LaneType::Tail) {
        endT = LaneType::TailR;
    }

    for (int i = rangeStart + 1; i < rangeEnd; ++i) {
        LaneType& type = typeAt(i);
        if (type == LaneType::NotActive) {
            type = LaneType::Cross;
        } else if (type == LaneType::Empty) {
            type = LaneType::CrossEmpty;
        }
    }
}

void Lanes::setMerge(const QStringList& parents, bool onlyFirstParent)
{
    // setFork() must be called before setMerge()
    if (m_boundary) {
        return; // Handle as a simple active line
    }

    LaneType& active = typeAt(m_activeLane);
    const bool wasFork = active == m_node;
    const bool wasForkL = active == m_nodeL;
    const bool wasForkR = active == m_nodeR;

    active = m_node;

    MergeRange range{.start = m_activeLane, .end = m_activeLane};
    if (!onlyFirstParent) {
        for (qsizetype p = 1; p < parents.size(); ++p) { // Skip first parent
            joinParent(parents.at(p), range);
        }
    }
    LaneType& startT = typeAt(range.start);
    LaneType& endT = typeAt(range.end);

    if (startT == m_node && !wasFork && !wasForkR) {
        startT = m_nodeL;
    }
    if (endT == m_node && !wasFork && !wasForkL) {
        endT = m_nodeR;
    }
    if (startT == LaneType::Join && !range.startJoinWasACross) {
        startT = LaneType::JoinL;
    }
    if (endT == LaneType::Join && !range.endJoinWasACross) {
        endT = LaneType::JoinR;
    }
    if (startT == LaneType::Head) {
        startT = LaneType::HeadL;
    }
    if (endT == LaneType::Head && !range.endWasEmptyCross) {
        endT = LaneType::HeadR;
    }

    for (int i = range.start + 1; i < range.end; ++i) {
        LaneType& type = typeAt(i);
        if (type == LaneType::NotActive) {
            type = LaneType::Cross;
        } else if (type == LaneType::Empty) {
            type = LaneType::CrossEmpty;
        } else if (type == LaneType::TailR || type == LaneType::TailL) {
            type = LaneType::Tail;
        }
    }
}

void Lanes::joinParent(const QString& parent, MergeRange& range)
{
    // The body of setMerge's loop over the parents after the first
    const int idx = findNextSha(parent, 0);
    if (idx == -1) {
        range.end = add(LaneType::Head, parent, range.end + 1, range.endWasEmptyCross);
        return;
    }
    if (idx > range.end) {
        range.end = idx;
        range.endJoinWasACross = typeAt(idx) == LaneType::Cross;
    }
    if (idx < range.start) {
        range.start = idx;
        range.startJoinWasACross = typeAt(idx) == LaneType::Cross;
    }
    typeAt(idx) = LaneType::Join;
}

void Lanes::setInitial()
{
    LaneType& type = typeAt(m_activeLane);
    if (!isNode(type) && type != LaneType::Applied) {
        type = m_boundary ? LaneType::Boundary : LaneType::Initial;
    }
}

void Lanes::setApplied()
{
    // Applied patches are not merges, nor forks
    typeAt(m_activeLane) = LaneType::Applied;
}

void Lanes::changeActiveLane(const QString& sha)
{
    LaneType& type = typeAt(m_activeLane);
    if (type == LaneType::Initial || isBoundary(type)) {
        type = LaneType::Empty;
    } else {
        type = LaneType::NotActive;
    }

    int idx = findNextSha(sha, 0); // Find first sha
    if (idx != -1) {
        typeAt(idx) = LaneType::Active; // Called before setBoundary()
    } else {
        bool wasEmptyCross = false;
        idx = add(LaneType::Branch, sha, m_activeLane, wasEmptyCross); // New branch
    }
    m_activeLane = idx;
}

void Lanes::afterMerge()
{
    if (m_boundary) {
        return; // Will be reset by changeActiveLane()
    }
    for (LaneType& type : m_typeVec) {
        if (isHead(type) || isJoin(type) || type == LaneType::Cross) {
            type = LaneType::NotActive;
        } else if (type == LaneType::CrossEmpty) {
            type = LaneType::Empty;
        } else if (isNode(type)) {
            type = LaneType::Active;
        }
    }
}

void Lanes::afterFork()
{
    for (LaneType& type : m_typeVec) {
        if (type == LaneType::Cross) {
            type = LaneType::NotActive;
        } else if (isTail(type) || type == LaneType::CrossEmpty) {
            type = LaneType::Empty;
        }
        if (!m_boundary && isNode(type)) {
            type = LaneType::Active; // Boundary will be reset by changeActiveLane()
        }
    }
    while (!m_typeVec.empty() && m_typeVec.back() == LaneType::Empty) {
        m_typeVec.pop_back();
        m_nextShaVec.pop_back();
    }
}

bool Lanes::isBranch() const
{
    return m_typeVec.at(static_cast<std::size_t>(m_activeLane)) == LaneType::Branch;
}

void Lanes::afterBranch()
{
    typeAt(m_activeLane) = LaneType::Active;
}

void Lanes::afterApplied()
{
    typeAt(m_activeLane) = LaneType::Active;
}

void Lanes::nextParent(const QString& sha)
{
    QString& next = m_nextShaVec.at(static_cast<std::size_t>(m_activeLane));
    if (m_boundary) {
        next.clear();
    } else {
        next = sha;
    }
}

const std::vector<Lanes::LaneType>& Lanes::lanes() const
{
    return m_typeVec;
}

int Lanes::findNextSha(const QString& next, int pos) const
{
    for (auto i = static_cast<std::size_t>(pos); i < m_nextShaVec.size(); ++i) {
        if (m_nextShaVec.at(i) == next) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Lanes::findType(LaneType type, int pos) const
{
    for (auto i = static_cast<std::size_t>(pos); i < m_typeVec.size(); ++i) {
        if (m_typeVec.at(i) == type) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Lanes::add(LaneType type, const QString& next, int pos, bool& wasEmptyCross)
{
    wasEmptyCross = false;
    // First check empty lanes starting from pos
    if (pos < static_cast<int>(m_typeVec.size())) {
        const int posEmpty = findType(LaneType::Empty, pos);
        const int posCrossEmpty = findType(LaneType::CrossEmpty, pos);
        // Use first "empty" position
        int found = -1;
        if (posEmpty != -1 && posCrossEmpty != -1) {
            found = std::min(posEmpty, posCrossEmpty);
        } else if (posEmpty != -1) {
            found = posEmpty;
        } else {
            found = posCrossEmpty;
        }
        if (found != -1) {
            wasEmptyCross = found == posCrossEmpty;
            typeAt(found) = type;
            m_nextShaVec.at(static_cast<std::size_t>(found)) = next;
            return found;
        }
    }
    // If all lanes are occupied add a new lane
    m_typeVec.push_back(type);
    m_nextShaVec.push_back(next);
    return static_cast<int>(m_typeVec.size()) - 1;
}

bool Lanes::isNode(LaneType type) const
{
    return type == m_node || type == m_nodeR || type == m_nodeL;
}

Lanes::LaneType& Lanes::typeAt(int pos)
{
    return m_typeVec.at(static_cast<std::size_t>(pos));
}

} // namespace kgt
