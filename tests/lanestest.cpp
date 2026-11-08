// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "lanes.hpp"
#include "gitloglistmodel.hpp"

#include <QTest>

#include <vector>

namespace
{

using namespace kgt;
using LaneType = Lanes::LaneType;
using LaneRow = std::vector<LaneType>;

/**
 * Hand-built commits for the model, listed in topological order as git log --topo-order lists them.
 */
struct Commit {
    QString hash; // The commit
    QStringList parents; // Its parents, first parent first
    bool isBoundary = false; // Whether git log --boundary marked it
};

/**
 * @param commits The log, in list order.
 * @returns The commits as the model holds them.
 */
QList<GitRevLoglist> toRevs(const QList<Commit>& commits)
{
    QList<GitRevLoglist> revs;
    for (const Commit& commit : commits) {
        GitRevLoglist rev;
        rev.hash = commit.hash;
        rev.parents = commit.parents;
        rev.isBoundary = commit.isBoundary;
        revs.append(rev);
    }
    return revs;
}

/**
 * @param commits The log, in list order.
 * @returns Every row's lanes, as GitLogListModel computes them.
 */
std::vector<LaneRow> laneRows(const QList<Commit>& commits)
{
    GitLogListModel model;
    model.appendRevs(toRevs(commits));
    std::vector<LaneRow> rows;
    for (int row = 0; row < model.rowCount(); ++row) {
        rows.push_back(model.lanes(row));
    }
    return rows;
}

} // namespace

/**
 * The graph's lanes (lanes.cpp and CLogDataVector::updateLanes) on small histories.
 */
class LanesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void straightLine()
    {
        const std::vector<LaneRow> expected{
            {LaneType::Branch},
            {LaneType::Active},
            {LaneType::Initial},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("c"), .parents = {QStringLiteral("b")}},
                     {.hash = QStringLiteral("b"), .parents = {QStringLiteral("a")}},
                     {.hash = QStringLiteral("a"), .parents = {}},
                 }),
                 expected);
    }

    void branchAndMerge()
    {
        // m merges c into b; both fork from the root a
        const std::vector<LaneRow> expected{
            {LaneType::MergeForkL, LaneType::HeadR},
            {LaneType::Active, LaneType::NotActive},
            {LaneType::NotActive, LaneType::Active},
            {LaneType::MergeForkLInitial, LaneType::TailR},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("m"), .parents = {QStringLiteral("b"), QStringLiteral("c")}},
                     {.hash = QStringLiteral("b"), .parents = {QStringLiteral("a")}},
                     {.hash = QStringLiteral("c"), .parents = {QStringLiteral("a")}},
                     {.hash = QStringLiteral("a"), .parents = {}},
                 }),
                 expected);
    }

    void octopusMerge()
    {
        // m merges three roots
        const std::vector<LaneRow> expected{
            {LaneType::MergeForkL, LaneType::Head, LaneType::HeadR},
            {LaneType::Initial, LaneType::NotActive, LaneType::NotActive},
            {LaneType::Empty, LaneType::Initial, LaneType::NotActive},
            {LaneType::Empty, LaneType::Empty, LaneType::Initial},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("m"), .parents = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}},
                     {.hash = QStringLiteral("a"), .parents = {}},
                     {.hash = QStringLiteral("b"), .parents = {}},
                     {.hash = QStringLiteral("c"), .parents = {}},
                 }),
                 expected);
    }

    void twoRoots()
    {
        // Two unrelated histories, interleaved: b1 -> a1 and b2 -> a2
        const std::vector<LaneRow> expected{
            {LaneType::Branch},
            {LaneType::NotActive, LaneType::Branch},
            {LaneType::Initial, LaneType::NotActive},
            {LaneType::Empty, LaneType::Initial},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("b1"), .parents = {QStringLiteral("a1")}},
                     {.hash = QStringLiteral("b2"), .parents = {QStringLiteral("a2")}},
                     {.hash = QStringLiteral("a1"), .parents = {}},
                     {.hash = QStringLiteral("a2"), .parents = {}},
                 }),
                 expected);
    }

    void crossing()
    {
        // b, on the third lane, merges a from the first lane across the second, which waits for c
        const std::vector<LaneRow> expected{
            {LaneType::MergeForkL, LaneType::HeadR},
            {LaneType::NotActive, LaneType::NotActive, LaneType::Branch},
            {LaneType::JoinL, LaneType::Cross, LaneType::MergeForkR},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("m"), .parents = {QStringLiteral("a"), QStringLiteral("c")}},
                     {.hash = QStringLiteral("n"), .parents = {QStringLiteral("b")}},
                     {.hash = QStringLiteral("b"), .parents = {QStringLiteral("z"), QStringLiteral("a")}},
                 }),
                 expected);
    }

    void boundaryParent()
    {
        // a is outside the range and marked as its boundary; its lane is free again for the unrelated x
        const std::vector<LaneRow> expected{
            {LaneType::Branch},
            {LaneType::Boundary},
            {LaneType::Initial},
        };
        QCOMPARE(laneRows({
                     {.hash = QStringLiteral("b"), .parents = {QStringLiteral("a")}},
                     {.hash = QStringLiteral("a"), .parents = {QStringLiteral("z")}, .isBoundary = true},
                     {.hash = QStringLiteral("x"), .parents = {}},
                 }),
                 expected);
    }

    void computesLazilyInListOrder()
    {
        // CLogDataVector::setLane: asking for a row computes every row above it first, then nothing again
        const QList<GitRevLoglist> revs = toRevs({
            {.hash = QStringLiteral("c"), .parents = {QStringLiteral("b")}},
            {.hash = QStringLiteral("b"), .parents = {QStringLiteral("a")}},
            {.hash = QStringLiteral("a"), .parents = {}},
        });
        GitLogListModel model;
        model.appendRevs(revs.first(2));
        QCOMPARE(model.lanes(1), LaneRow{LaneType::Active});
        QCOMPARE(model.lanes(0), LaneRow{LaneType::Branch});
        // Rows appended while git log runs continue the same lanes
        model.appendRevs(revs.mid(2));
        QCOMPARE(model.lanes(2), LaneRow{LaneType::Initial});
        // A reload starts the graph again
        model.clear();
        model.appendRevs(revs.mid(1));
        QCOMPARE(model.lanes(0), LaneRow{LaneType::Branch});
    }
};

QTEST_GUILESS_MAIN(LanesTest)

#include "lanestest.moc"
