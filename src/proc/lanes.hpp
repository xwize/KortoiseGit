// SPDX-FileCopyrightText: 2005-2007 Marco Costalba
// SPDX-FileCopyrightText: 2008-2013, 2017, 2021, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/lanes.h, which comes from QGit (Marco Costalba). Unlike the rest of
 * KortoiseGit this file is GPL-2.0-only, which makes the combined program GPL-2.0-only.
 *
 * The commit graph's lanes: walking the log in topological order, Lanes keeps one lane per line of history still
 * waiting for its next commit, and gives each commit the shape of every lane on its row.
 */

#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace kgt
{

/**
 * The lane state of the commit graph (QGit's Lanes), advanced one commit at a time by
 * GitLogListModel::updateLanes.
 */
class Lanes
{
public:
    /**
     * What a lane looks like on one row: the graph elements paintGraphLane draws.
     */
    enum class LaneType : std::uint8_t {
        Empty, // Nothing in this lane
        Active, // The commit's own lane, a plain node
        NotActive, // A line passing by
        MergeFork, // The commit's node, joined by lanes on both sides
        MergeForkR, // The commit's node, joined by lanes on its left
        MergeForkL, // The commit's node, joined by lanes on its right
        MergeForkLInitial, // MergeForkL on a root commit
        Join, // A merged parent's lane, between others
        JoinR, // The rightmost merged parent's lane
        JoinL, // The leftmost merged parent's lane
        Head, // A merged parent's new lane, between others
        HeadR, // The rightmost merged parent's new lane
        HeadL, // The leftmost merged parent's new lane
        Tail, // A lane that ends at the commit (a fork), between others
        TailR, // The rightmost lane ending at the commit
        TailL, // The leftmost lane ending at the commit
        Cross, // A line passing by, crossed by a join or fork
        CrossEmpty, // An empty lane crossed by a join or fork
        Initial, // A root commit
        Branch, // The first commit of a line of history: its lane starts here
        Unapplied, // A StGit patch, unused
        Applied, // A StGit patch, unused
        Boundary, // A boundary commit (git log --boundary)
        BoundaryC, // Corresponds to MergeFork
        BoundaryR, // Corresponds to MergeForkR
        BoundaryL, // Corresponds to MergeForkL
    };

    static constexpr int colorsNum = 8; // COLORS_NUM: how many lane colours cycle

    /**
     * @param type A lane.
     * @returns Whether it is a Head, HeadR or HeadL.
     */
    [[nodiscard]] static bool isHead(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether it is a Tail, TailR or TailL.
     */
    [[nodiscard]] static bool isTail(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether it is a Join, JoinR or JoinL.
     */
    [[nodiscard]] static bool isJoin(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether a line passes through it without a commit on it.
     */
    [[nodiscard]] static bool isFreeLane(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether it is one of the boundary nodes.
     */
    [[nodiscard]] static bool isBoundary(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether it is a merge or fork node, or a boundary node.
     */
    [[nodiscard]] static bool isMerge(LaneType type);

    /**
     * @param type A lane.
     * @returns Whether the commit's node is in it.
     */
    [[nodiscard]] static bool isActive(LaneType type);

    /**
     * @returns Whether init() hasn't been called since the last clear().
     */
    [[nodiscard]] bool isEmpty() const;

    /**
     * Starts the graph with one lane, waiting for the first commit.
     * @param expectedSha The first commit's hash.
     */
    void init(const QString& expectedSha);

    /**
     * Removes every lane.
     */
    void clear();

    /**
     * @param sha The commit about to be added.
     * @param[out] isDiscontinuity Whether it is not on the active lane, so the active lane must change.
     * @returns Whether more than one lane waits for it: lines of history fork from it.
     */
    [[nodiscard]] bool isFork(const QString& sha, bool& isDiscontinuity) const;

    /**
     * Sets whether the commit is a boundary commit. Changes the state, so it is called first.
     * @param isBoundary Whether it is a boundary commit.
     * @param isInitial Whether it is a root commit.
     */
    void setBoundary(bool isBoundary, bool isInitial);

    /**
     * Ends every lane waiting for the commit but the active one.
     * @param sha The commit.
     */
    void setFork(const QString& sha);

    /**
     * Joins or starts a lane for each parent after the first. setFork() comes first.
     * @param parents The commit's parents, first parent first.
     * @param onlyFirstParent Whether the log shows first parents only, so nothing is joined.
     */
    void setMerge(const QStringList& parents, bool onlyFirstParent);

    /**
     * Marks the active lane as a root commit's.
     */
    void setInitial();

    /**
     * Marks the active lane as an applied StGit patch.
     */
    void setApplied();

    /**
     * Moves the active lane to the one waiting for the commit, or starts a new one.
     * @param sha The commit.
     */
    void changeActiveLane(const QString& sha);

    /**
     * Turns the joins and heads of a merge into plain lines for the next row.
     */
    void afterMerge();

    /**
     * Frees the lanes a fork ended for the next row.
     */
    void afterFork();

    /**
     * @returns Whether the active lane starts at the commit.
     */
    [[nodiscard]] bool isBranch() const;

    /**
     * Turns a new branch's lane into a plain line for the next row.
     */
    void afterBranch();

    /**
     * Turns an applied patch's lane into a plain line for the next row.
     */
    void afterApplied();

    /**
     * Makes the active lane wait for the commit's first parent.
     * @param sha The first parent's hash, or empty for a root commit.
     */
    void nextParent(const QString& sha);

    /**
     * @returns The type of each lane on the current row (getLanes).
     */
    [[nodiscard]] const std::vector<LaneType>& lanes() const;

private:
    /**
     * The lanes a merge spans, while setMerge() joins its parents.
     */
    struct MergeRange {
        int start = 0; // The leftmost lane
        int end = 0; // The rightmost lane
        bool startJoinWasACross = false; // The leftmost join was a Cross
        bool endJoinWasACross = false; // The rightmost join was a Cross
        bool endWasEmptyCross = false; // The rightmost new head took a CrossEmpty lane
    };

    void joinParent(const QString& parent, MergeRange& range);
    [[nodiscard]] int findNextSha(const QString& next, int pos) const;
    [[nodiscard]] int findType(LaneType type, int pos) const;
    int add(LaneType type, const QString& next, int pos, bool& wasEmptyCross);
    [[nodiscard]] bool isNode(LaneType type) const;
    [[nodiscard]] LaneType& typeAt(int pos);

    int m_activeLane = 0;
    std::vector<LaneType> m_typeVec;
    std::vector<QString> m_nextShaVec; // The commit each lane waits for
    bool m_boundary = false;
    LaneType m_node = LaneType::Empty; // What the commit's node is: MergeFork, or BoundaryC on a boundary
    LaneType m_nodeL = LaneType::Empty;
    LaneType m_nodeR = LaneType::Empty;
};

} // namespace kgt
