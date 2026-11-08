// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gitloglist.hpp"
#include "gitloglistmodel.hpp"
#include "lanes.hpp"

#include <QByteArray>
#include <QDir>
#include <QHeaderView>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QTest>

#include <algorithm>
#include <set>
#include <vector>

namespace
{

using namespace kgt;
using LaneType = Lanes::LaneType;

constexpr int graphWidth = 240; // Wide enough for the fixture's lanes at every row height
constexpr int coveredLevel = 96; // A pixel the stroke fills reaches at least this in its brightest channel
constexpr int emptyLevel = 48; // A pixel beside the stroke stays below this
constexpr int zoom = 6; // Of the saved captures

/**
 * Gives every row a chosen height, and paints with the log list's own delegate.
 */
class FixedRowHeightDelegate : public QStyledItemDelegate
{
public:
    /**
     * @param painting The delegate that paints.
     * @param rowHeight The height of every row.
     * @param parent The owner.
     */
    FixedRowHeightDelegate(QAbstractItemDelegate *painting, int rowHeight, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_painting(painting)
        , m_rowHeight(rowHeight)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        m_painting->paint(painter, option, index);
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return {m_painting->sizeHint(option, index).width(), m_rowHeight};
    }

private:
    QAbstractItemDelegate *m_painting; // Not owned
    int m_rowHeight;
};

/**
 * @param hash A commit.
 * @param parents Its parents, first parent first.
 * @returns The commit as the model holds it.
 */
GitRevLoglist commit(const QString& hash, const QStringList& parents = {})
{
    GitRevLoglist rev;
    rev.hash = hash;
    rev.parents = parents;
    return rev;
}

/**
 * @returns A log, in list order, with every kind of lane that has a curve.
 */
QList<GitRevLoglist> historyWithCurves()
{
    return {
        // An octopus merge opening two new lanes, which end again at a fork: Head, HeadR, Tail, TailR
        commit(QStringLiteral("octopus"), {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}),
        commit(QStringLiteral("first"), {QStringLiteral("fork")}),
        commit(QStringLiteral("second"), {QStringLiteral("fork")}),
        commit(QStringLiteral("third"), {QStringLiteral("fork")}),
        commit(QStringLiteral("fork")),
        // A merge to the right of the lanes of its other parents: JoinL, Join
        commit(QStringLiteral("tip1"), {QStringLiteral("left")}),
        commit(QStringLiteral("tip2"), {QStringLiteral("middle")}),
        commit(QStringLiteral("merge1"), {QStringLiteral("own"), QStringLiteral("left"), QStringLiteral("middle")}),
        commit(QStringLiteral("left")),
        commit(QStringLiteral("middle")),
        commit(QStringLiteral("own")),
        // A merge to the left of the lane of its other parent: JoinR
        commit(QStringLiteral("tip3"), {QStringLiteral("merge2")}),
        commit(QStringLiteral("tip4"), {QStringLiteral("other")}),
        commit(QStringLiteral("merge2"), {QStringLiteral("base"), QStringLiteral("other")}),
        commit(QStringLiteral("other")),
        commit(QStringLiteral("base")),
    };
}

/**
 * A captured log list, read in logical coordinates.
 */
struct Capture {
    QImage image; // The viewport as painted, on a black background
    int ratio = 1; // Device pixels per logical pixel

    /**
     * @param device A device pixel.
     * @returns How bright that pixel is: 0 where nothing was drawn.
     */
    [[nodiscard]] int level(const QPoint& device) const
    {
        const QRgb pixel = image.pixel(device);
        return std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)});
    }

    /**
     * Checks that a stroke sits exactly on a 2 logical pixels wide line.
     * @param first The first device pixel of the line, across the stroke.
     * @param across One device pixel across the stroke.
     * @returns Whether the stroke fills the line and nothing lies beside it.
     */
    [[nodiscard]] bool isOnLine(const QPoint& first, const QPoint& across) const
    {
        const QPoint last = first + (across * ((2 * ratio) - 1));
        return level(first) >= coveredLevel && level(last) >= coveredLevel && level(first - across) < emptyLevel && level(last + across) < emptyLevel;
    }
};

/**
 * @param type A lane.
 * @returns Whether its curve ends at the row's bottom, on the lane's axis.
 */
bool curvesToBottom(LaneType type)
{
    return type == LaneType::Join || type == LaneType::JoinR || type == LaneType::JoinL || type == LaneType::Head || type == LaneType::HeadR;
}

/**
 * @param type A lane.
 * @returns Whether its curve ends at the row's top, on the lane's axis.
 */
bool curvesToTop(LaneType type)
{
    return type == LaneType::Tail || type == LaneType::TailR;
}

/**
 * @param capture The captured list.
 * @param type A lane with a curve.
 * @param rect The row's cell, starting at the lane's left edge.
 * @param laneWidth The lane's width.
 * @returns Whether both ends of the curve lie exactly on the lines they join.
 */
bool curveMeetsLines(const Capture& capture, LaneType type, const QRect& rect, int laneWidth)
{
    const int left = rect.left();
    const int right = left + laneWidth;
    const int axis = (left + right) / 2;
    const int line = rect.top() + (rect.height() / 2);
    const int ratio = capture.ratio;
    const int edgeRow = curvesToTop(type) ? rect.top() * ratio : ((rect.top() + rect.height()) * ratio) - 1;
    const int edgeColumn = type == LaneType::JoinL ? (right * ratio) - 1 : left * ratio;
    return capture.isOnLine(QPoint((axis - 1) * ratio, edgeRow), QPoint(1, 0)) && capture.isOnLine(QPoint(edgeColumn, (line - 1) * ratio), QPoint(0, 1));
}

/**
 * @param list The captured list.
 * @param capture Its capture.
 * @param seen Filled with the kinds of curve found.
 * @returns Where the first curve that misses a line it joins is, or nothing if they all meet.
 */
QString firstMisjoinedCurve(const GitLogList& list, const Capture& capture, std::set<LaneType>& seen)
{
    const GitLogListModel *model = list.logModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QRect rect = list.visualRect(model->index(row, GitLogListModel::GraphColumn));
        const int laneWidth = 3 * rect.height() / 4;
        const std::vector<LaneType>& lanes = model->lanes(row);
        for (int lane = 0; lane < static_cast<int>(lanes.size()); ++lane) {
            const LaneType type = lanes.at(static_cast<std::size_t>(lane));
            if (!curvesToBottom(type) && !curvesToTop(type)) {
                continue;
            }
            seen.insert(type);
            if (!curveMeetsLines(capture, type, rect.adjusted(lane * laneWidth, 0, 0, 0), laneWidth)) {
                return QStringLiteral("row %1, lane %2").arg(row).arg(lane);
            }
        }
    }
    return {};
}

/**
 * Sets a log list up to be captured: fixed row heights, only the graph to read, on black.
 * @param list The list, already filled.
 * @param rowHeight The height of every row.
 */
void layOutForCapture(GitLogList& list, int rowHeight)
{
    list.setItemDelegate(new FixedRowHeightDelegate(list.itemDelegate(), rowHeight, &list));
    list.header()->hide();
    list.setFrameShape(QFrame::NoFrame);
    list.setRootIsDecorated(false);
    list.setColumnWidth(GitLogListModel::GraphColumn, graphWidth);
    QPalette black = list.palette();
    black.setColor(QPalette::Base, Qt::black);
    list.setPalette(black);
    list.resize(graphWidth * 2, (list.logModel()->rowCount() + 1) * rowHeight);
}

/**
 * Saves a zoomed copy of the graph when KGT_SCREENSHOT_DIR is set.
 * @param capture The captured list.
 * @param rowHeight The height of every row.
 * @returns Whether that worked, or there was nothing to do.
 */
bool saveCapture(const Capture& capture, int rowHeight)
{
    const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
    if (screenshotDir.isEmpty()) {
        return true;
    }
    const QImage graph = capture.image.copy(0, 0, graphWidth * capture.ratio, capture.image.height());
    const QString name = QStringLiteral("loggraph-%1-dpr%2.png").arg(rowHeight).arg(capture.ratio);
    return graph.scaled(graph.size() * zoom, Qt::KeepAspectRatio, Qt::FastTransformation).save(QDir(screenshotDir).absoluteFilePath(name));
}

} // namespace

class GitLogListGraphTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void curvesMeetTheStraightLines_data()
    {
        QTest::addColumn<int>("rowHeight");
        // laneWidth = 3 * height / 4: even and odd heights with even and odd lane widths
        for (const int height : {22, 23, 24, 25, 30, 31}) {
            QTest::newRow(QByteArray::number(height).constData()) << height;
        }
    }

    void curvesMeetTheStraightLines()
    {
        QFETCH(const int, rowHeight);

        GitLogList list;
        GitLogListModel *model = list.logModel();
        model->appendRevs(historyWithCurves());
        QVERIFY(model->isGraphShown());
        layOutForCapture(list, rowHeight);

        const QPixmap shot = list.viewport()->grab();
        const Capture capture{.image = shot.toImage().convertToFormat(QImage::Format_RGB32), .ratio = qRound(shot.devicePixelRatio())};

        QVERIFY(saveCapture(capture, rowHeight));
        QCOMPARE(list.visualRect(model->index(0, GitLogListModel::GraphColumn)).height(), rowHeight);

        std::set<LaneType> seen;
        const QString misjoined = firstMisjoinedCurve(list, capture, seen);
        QVERIFY2(misjoined.isEmpty(), misjoined.toUtf8().constData());
        const std::set<LaneType> curved{LaneType::Join, LaneType::JoinR, LaneType::JoinL, LaneType::Head, LaneType::HeadR, LaneType::Tail, LaneType::TailR};
        QCOMPARE(seen, curved);
    }
};

QTEST_MAIN(GitLogListGraphTest)

#include "gitloglistgraphtest.moc"
