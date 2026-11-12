// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp and GitLogList.cpp

#include "gitloglist.hpp"

#include "apputils.hpp"
#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QApplication>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QStyledItemDelegate>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

constexpr int labelPadding = 3; // Between a ref label's border and its text
constexpr int labelSpacing = 3; // Between two ref labels, and between the last one and the subject
constexpr int maxIconSize = 16; // TortoiseGit's small icons
constexpr int iconSpacing = 2; // Between two action icons
constexpr int iconItemBorder = 5; // ICONITEMBORDER
constexpr int graphColumnWidth = iconItemBorder + (maxIconSize * 4); // CGitLogListBase::InsertGitColumn's columnWidth
constexpr int graphLineWidth = 2; // Graph\LogLineWidth's default
constexpr int graphNodeSize = 10; // Graph\LogNodeSize's default
constexpr int authorColumnWidth = 150;
constexpr int dateColumnWidth = 140;
constexpr int maxLabelBrightness = 128; // Labels with a lightness below this get white text

/**
 * @param ref A ref.
 * @returns Its label's colour, TortoiseGit's defaults from CColors.
 */
QColor labelColor(const GitRef& ref)
{
    if (ref.isCurrent) {
        return {200, 0, 0}; // CColors::CurrentBranch
    }
    switch (ref.type) {
    case GitRef::Type::LocalBranch:
        return {0, 195, 0}; // CColors::LocalBranch
    case GitRef::Type::RemoteBranch:
        return {255, 221, 170}; // CColors::RemoteBranch
    case GitRef::Type::Tag:
        return {255, 255, 0}; // CColors::Tag
    case GitRef::Type::Stash:
        return {128, 128, 128}; // CColors::Stash
    case GitRef::Type::Head:
    case GitRef::Type::Other:
        return {224, 224, 224}; // CColors::OtherRef
    }
    return {224, 224, 224};
}

/**
 * The lane colours, following the order of CColors::BranchLine1 to BranchLine8 (TortoiseGit's Colors.cpp defaults:
 * black, red, green, blue, grey, olive, teal, purple). TortoiseGit's values are black or fully saturated, harsh on a
 * light background and muddy once CTheme::GetThemeColor inverts them for a dark one. These are pastel shades of
 * the same hues instead (grey for black and orange for the second grey), all at OKLCh lightness 0.66 with chroma
 * up to 0.13, about 3:1 contrast on white and 5:1 on Breeze Dark's #202326, so one palette serves both themes.
 */
constexpr std::array<QRgb, Lanes::colorsNum> branchLineColors{
    qRgb(0x92, 0x92, 0x92), // grey
    qRgb(0xd6, 0x70, 0x69), // red
    qRgb(0x5a, 0xa7, 0x5e), // green
    qRgb(0x5d, 0x94, 0xda), // blue
    qRgb(0xc7, 0x7f, 0x3e), // orange
    qRgb(0x9c, 0x96, 0x3e), // olive
    qRgb(0x31, 0xa5, 0xa5), // teal
    qRgb(0xb0, 0x78, 0xc9), // purple
};

/**
 * One lane of one row for paintGraphLane.
 */
struct GraphLane {
    Lanes::LaneType type = Lanes::LaneType::Empty; // What to draw
    int left = 0; // The lane's left edge
    int right = 0; // The lane's right edge
    int top = 0; // The row's top
    int laneHeight = 0; // The row's height
    QColor color; // The lane's colour
    QColor activeColor; // The colour of the commit's lane, for the lines that reach it
};

/**
 * @param lane A lane's index.
 * @returns The lane's colour. TortoiseGit passes it through CTheme::GetThemeColor(color, true), which inverts its
 *          lightness in the dark theme; the pastel branchLineColors read on both, so they are used as they are.
 */
QColor laneColor(std::size_t lane)
{
    return QColor::fromRgb(branchLineColors.at(lane % Lanes::colorsNum));
}

/**
 * Draws the curve that joins a lane to the commit's horizontal line (paintGraphLane's arcs). TortoiseGit's GDI+
 * angles run clockwise, Qt's anticlockwise, so they are negated.
 *
 * paintGraphLane offsets its DrawArc rectangles by -1, which suits GDI+'s pixel centres on integer coordinates. Qt's
 * pixel centres are at .5 and a 2 pixel pen on an integer coordinate fills the pixels either side of it, so here the
 * quarter ellipse is built from the two points it joins: the commit's horizontal line at the lane's edge, and the
 * lane's axis at the row's edge. The two radii are separate because odd row heights and lane widths make them differ.
 * @param painter Draws.
 * @param lane The lane.
 */
void paintLaneArc(QPainter *painter, const GraphLane& lane)
{
    using LaneType = Lanes::LaneType;
    const int half = lane.laneHeight / 2;
    const int mid = (lane.left + lane.right) / 2;
    const int top = lane.top;
    const int line = top + half; // The commit's horizontal line
    const int bottom = top + lane.laneHeight;
    QLinearGradient gradient;
    QPoint center; // Of the ellipse: the corner the curve bends around
    QSize radii;
    int startAngle = 0;
    switch (lane.type) {
    case LaneType::Join:
    case LaneType::JoinR:
    case LaneType::Head:
    case LaneType::HeadR:
        gradient = QLinearGradient(QPoint(lane.left - 2, half + top - 2), QPoint(mid, (2 * half) + top + 1));
        gradient.setColorAt(0, lane.activeColor);
        gradient.setColorAt(1, lane.color);
        center = QPoint(lane.left, bottom);
        radii = QSize(mid - lane.left, bottom - line);
        startAngle = 90; // GDI+ 270 to 360: from the top to the right
        break;
    case LaneType::JoinL:
        gradient = QLinearGradient(QPoint(mid, (2 * half) + top + 1), QPoint(lane.right + 1, half + top - 1));
        gradient.setColorAt(0, lane.color);
        gradient.setColorAt(1, lane.activeColor);
        center = QPoint(lane.right, bottom);
        radii = QSize(lane.right - mid, bottom - line);
        startAngle = 180; // GDI+ 180 to 270: from the left to the top
        break;
    case LaneType::Tail:
    case LaneType::TailR:
        gradient = QLinearGradient(QPoint(lane.left - 2, half + top - 2), QPoint(mid, top - 1));
        gradient.setColorAt(0, lane.activeColor);
        gradient.setColorAt(1, lane.color);
        center = QPoint(lane.left, top);
        radii = QSize(mid - lane.left, line - top);
        startAngle = 0; // GDI+ 0 to 90: from the right to the bottom
        break;
    default:
        return;
    }
    const QRect arc(center.x() - radii.width(), center.y() - radii.height(), 2 * radii.width(), 2 * radii.height());
    constexpr int degree = 16; // Qt's arc angles are in 1/16 degrees
    constexpr int quarter = 90;
    painter->setPen(QPen(QBrush(gradient), graphLineWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(arc, startAngle * degree, -quarter * degree);
}

/**
 * Draws a lane's vertical line, in its own colour (paintGraphLane).
 * @param painter Draws.
 * @param lane The lane.
 */
void paintLaneVertical(QPainter *painter, const GraphLane& lane)
{
    using LaneType = Lanes::LaneType;
    const int half = lane.laneHeight / 2;
    const int mid = (lane.left + lane.right) / 2;
    const QPoint center(mid, half + lane.top);
    const QPoint above(mid, lane.top - 1); // P_90
    const QPoint below(mid, (2 * half) + lane.top + 1); // P_270
    painter->setPen(QPen(lane.color, graphLineWidth));
    switch (lane.type) {
    case LaneType::Active:
    case LaneType::MergeFork:
    case LaneType::MergeForkR:
    case LaneType::MergeForkL:
    case LaneType::NotActive:
    case LaneType::Join:
    case LaneType::JoinR:
    case LaneType::JoinL:
    case LaneType::Cross:
        painter->drawLine(above, below);
        break;
    case LaneType::Branch:
    case LaneType::HeadL:
        painter->drawLine(center, below);
        break;
    case LaneType::Initial:
    case LaneType::MergeForkLInitial:
    case LaneType::Boundary:
    case LaneType::BoundaryC:
    case LaneType::BoundaryR:
    case LaneType::BoundaryL:
    case LaneType::TailL:
        painter->drawLine(above, center);
        break;
    default:
        break;
    }
}

/**
 * Draws the horizontal line that reaches the commit's node, in the active lane's colour (paintGraphLane).
 * @param painter Draws.
 * @param lane The lane.
 */
void paintLaneHorizontal(QPainter *painter, const GraphLane& lane)
{
    using LaneType = Lanes::LaneType;
    const int y = (lane.laneHeight / 2) + lane.top;
    const QPoint center((lane.left + lane.right) / 2, y);
    const QPoint left(lane.left, y); // P_180
    const QPoint right(lane.right, y); // P_0
    painter->setPen(QPen(lane.activeColor, graphLineWidth));
    switch (lane.type) {
    case LaneType::MergeFork:
    case LaneType::BoundaryC:
    case LaneType::Join:
    case LaneType::Head:
    case LaneType::Tail:
    case LaneType::Cross:
    case LaneType::CrossEmpty:
        painter->drawLine(left, right);
        break;
    case LaneType::MergeForkR:
    case LaneType::BoundaryR:
        painter->drawLine(left, center);
        break;
    case LaneType::MergeForkL:
    case LaneType::MergeForkLInitial:
    case LaneType::BoundaryL:
    case LaneType::HeadL:
    case LaneType::TailL:
        painter->drawLine(center, right);
        break;
    default:
        break;
    }
}

/**
 * Draws the commit's node: a circle, a square for a merge or fork, an outlined circle for a boundary commit
 * (paintGraphLane's center symbol).
 * @param painter Draws.
 * @param lane The lane.
 */
void paintLaneNode(QPainter *painter, const GraphLane& lane)
{
    using LaneType = Lanes::LaneType;
    const int half = lane.laneHeight / 2;
    const int mid = (lane.left + lane.right) / 2;
    const int radius = (lane.right - lane.left) * graphNodeSize / 30;
    const int diameter = 2 * radius;
    const QRect node(mid - radius, half - radius + lane.top, diameter, diameter); // R_CENTER
    switch (lane.type) {
    case LaneType::Active:
    case LaneType::Initial:
    case LaneType::Branch:
        painter->setPen(Qt::NoPen);
        painter->setBrush(lane.color);
        painter->drawEllipse(node);
        break;
    case LaneType::MergeFork:
    case LaneType::MergeForkR:
    case LaneType::MergeForkL:
    case LaneType::MergeForkLInitial:
    case LaneType::BoundaryC:
    case LaneType::BoundaryR:
    case LaneType::BoundaryL:
        painter->fillRect(node, lane.color);
        break;
    case LaneType::Unapplied:
        // TortoiseGit leaves out the row's top here, which only works for the first row
        painter->fillRect(QRect(mid - radius, half - 1 + lane.top, diameter, 2), lane.color);
        break;
    case LaneType::Applied:
        painter->fillRect(QRect(mid - radius, half - 1 + lane.top, diameter, 2), lane.color);
        painter->fillRect(QRect(mid - 1, half - radius + lane.top, 2, diameter), lane.color);
        break;
    case LaneType::Boundary:
        painter->setPen(QPen(lane.activeColor, graphLineWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(node);
        break;
    default:
        break;
    }
}

/**
 * CGitLogListBase::paintGraphLane: one lane of one row. Roll-up (m_RolledUp) isn't ported yet.
 * @param painter Draws, antialiased.
 * @param lane The lane.
 */
void paintGraphLane(QPainter *painter, const GraphLane& lane)
{
    paintLaneArc(painter, lane);
    paintLaneVertical(painter, lane);
    paintLaneHorizontal(painter, lane);
    paintLaneNode(painter, lane);
}

/**
 * CGitLogListBase::DrawGraph: a row's lanes, each 3/4 of the row height wide, until the cell is full.
 * @param painter Draws.
 * @param rect The cell.
 * @param lanes The row's lanes.
 */
void drawGraph(QPainter *painter, const QRect& rect, const std::vector<Lanes::LaneType>& lanes)
{
    // The active lane is the first merge lane: the commit's node
    const auto merge = std::ranges::find_if(lanes, &Lanes::isMerge);
    const std::size_t activeLane = merge == lanes.end() ? 0 : static_cast<std::size_t>(merge - lanes.begin());

    int laneRight = 0;
    const int maxWidth = rect.width();
    const int laneWidth = 3 * rect.height() / 4;
    const QColor activeColor = laneColor(activeLane);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(rect);
    for (std::size_t i = 0; i < lanes.size() && laneRight < maxWidth; ++i) {
        const int laneLeft = laneRight;
        laneRight += laneWidth;
        const Lanes::LaneType type = lanes.at(i);
        if (type == Lanes::LaneType::Empty) {
            continue;
        }
        paintGraphLane(painter,
                       {
                           .type = type,
                           .left = laneLeft + rect.left(),
                           .right = laneRight + rect.left(),
                           .top = rect.top(),
                           .laneHeight = rect.height(),
                           .color = i == activeLane ? activeColor : laneColor(i),
                           .activeColor = activeColor,
                       });
    }
}

/**
 * @param name The icon's name in TortoiseGit src/Resources, without ".ico".
 * @returns The icon, with every size the .ico holds, from icons.qrc.
 */
QIcon tortoiseGitIcon(const QString& name)
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 64}) {
        icon.addFile(QStringLiteral(":/kortoisegit/icons/%1-%2.png").arg(name).arg(size), QSize(size, size));
    }
    return icon;
}

/**
 * The icons CGitLogListBase's constructor loads for the Actions column (IDI_ACTION*).
 */
struct ActionIcons {
    QIcon modified = tortoiseGitIcon(QStringLiteral("actionmodified"));
    QIcon replaced = tortoiseGitIcon(QStringLiteral("actionreplaced"));
    QIcon conflicted = tortoiseGitIcon(QStringLiteral("actionconflicted"));
    QIcon added = tortoiseGitIcon(QStringLiteral("actionadded"));
    QIcon deleted = tortoiseGitIcon(QStringLiteral("actiondeleted"));
    QIcon fetching = tortoiseGitIcon(QStringLiteral("actionfetching"));
    QIcon error = tortoiseGitIcon(QStringLiteral("actionerror"));

    /**
     * @returns The icons, loaded once.
     */
    static const ActionIcons& instance()
    {
        static const ActionIcons icons;
        return icons;
    }
};

/**
 * @param action One of the actions the Actions column shows.
 * @returns The icon TortoiseGit draws for it.
 */
const QIcon& actionIcon(Action action)
{
    const ActionIcons& icons = ActionIcons::instance();
    switch (action) {
    case Action::Modified:
        return icons.modified;
    case Action::Added:
        return icons.added;
    case Action::Deleted:
        return icons.deleted;
    case Action::Replaced:
        return icons.replaced;
    case Action::Unmerged:
    default:
        return icons.conflicted;
    }
}

/**
 * Where one ref's label goes in a row (a value of CGitLogListBase::m_RefLabelPosMap).
 */
struct RefLabel {
    const GitRef *ref = nullptr; // The ref, owned by the list passed to layOutRefLabels()
    QRect rect; // The label
};

/**
 * Lays the ref labels out from the left of the cell. TortoiseGit records m_RefLabelPosMap while painting and clears
 * it per paint; Qt repaints only parts of the view, so painting and IsMouseOnRefLabel both ask this instead.
 * @param rect The cell's text area.
 * @param opt The cell's style.
 * @param index The cell.
 * @returns The labels that fit, left to right.
 */
QList<RefLabel> layOutRefLabels(const QRect& rect, const QStyleOptionViewItem& opt, const QModelIndex& index)
{
    const auto *model = qobject_cast<const GitLogListModel *>(index.model());
    Q_ASSERT(model);
    QList<RefLabel> labels;
    QRect rest = rect;
    for (const GitRef& ref : model->rev(index.row()).refs) {
        if (rest.width() <= 0) {
            break;
        }
        if (!model->isRefShown(ref)) {
            continue;
        }
        const int width = opt.fontMetrics.horizontalAdvance(ref.name) + (2 * labelPadding);
        const QRect label(rest.left(), rest.top() + 1, std::min(width, rest.width()), rest.height() - 2);
        labels.append({.ref = &ref, .rect = label});
        rest.setLeft(label.right() + 1 + labelSpacing);
    }
    return labels;
}

/**
 * Draws the Actions and Message columns, which TortoiseGit owner-draws (CGitLogListBase::OnNMCustomdrawLoglist):
 * an icon per kind of change, and the ref labels before the subject (DrawTagBranchMessage).
 */
class GitLogListDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const auto *model = qobject_cast<const GitLogListModel *>(index.model());
        const int column = index.column();
        if (model && column == GitLogListModel::GraphColumn && !model->isRebaseMode()) {
            paintGraph(painter, option, index, *model);
            return;
        }
        const bool drawn = column == GitLogListModel::MessageColumn || (column == GitLogListModel::ActionsColumn && model && !model->isRebaseMode());
        if (!model || !drawn) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString subject = std::exchange(opt.text, QString());
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const QRect rect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

        painter->save();
        if (column == GitLogListModel::ActionsColumn) {
            drawActions(painter, rect, model->rev(index.row()));
        } else {
            const QRect textRect = drawRefs(painter, rect, layOutRefLabels(rect, opt, index), opt.font);
            const bool isSelected = opt.state.testFlag(QStyle::State_Selected);
            painter->setPen(opt.palette.color(isSelected ? QPalette::HighlightedText : QPalette::Text));
            painter->setFont(opt.font);
            const QString elided = opt.fontMetrics.elidedText(subject, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, elided);
        }
        painter->restore();
    }

    /**
     * Where paint() draws a row's labels, for CGitLogListBase::IsMouseOnRefLabel.
     * @param option The Message cell's style and rectangle.
     * @param index The Message cell.
     * @returns The labels.
     */
    [[nodiscard]] QList<RefLabel> refLabels(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        return layOutRefLabels(style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget), opt, index);
    }

private:
    /**
     * Draws the Graph column as CGitLogListBase::OnNMCustomdrawLoglist does: the cell's background, then the
     * graph, unless the model says to leave it out.
     * @param painter Draws.
     * @param option The cell's style.
     * @param index The cell.
     * @param model The log.
     */
    void paintGraph(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index, const GitLogListModel& model) const
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        if (!model.isGraphShown()) {
            return;
        }
        painter->save();
        drawGraph(painter, opt.rect, model.lanes(index.row()));
        painter->restore();
    }

    /**
     * Draws the icons as CGitLogListBase::OnNMCustomdrawLoglist does: the fetching or error icon until the
     * commit's files are known, then one icon per action in a fixed slot, so each kind lines up down the column.
     * @param painter Draws.
     * @param rect The cell's text area.
     * @param rev The commit.
     */
    static void drawActions(QPainter *painter, const QRect& rect, const GitRevLoglist& rev)
    {
        const int size = std::min(maxIconSize, rect.height());
        const auto slotRect = [&](int slot) {
            return QRect(rect.left() + (slot * (size + iconSpacing)), rect.top() + ((rect.height() - size) / 2), size, size);
        };
        switch (rev.filesState) {
        case GitRevLoglist::FilesState::NotLoaded:
            ActionIcons::instance().fetching.paint(painter, slotRect(0));
            return;
        case GitRevLoglist::FilesState::Failed:
            ActionIcons::instance().error.paint(painter, slotRect(0));
            return;
        case GitRevLoglist::FilesState::Loaded:
            break;
        }
        int slot = 0;
        for (const Action action : GitLogListModel::shownActions()) {
            // A copy shares the added icon (LOGACTIONS_ADDED | LOGACTIONS_COPY)
            const TGitPath::Actions matches = action == Action::Added ? (Action::Added | Action::Copy) : TGitPath::Actions(action);
            if (rev.actions & matches) {
                actionIcon(action).paint(painter, slotRect(slot));
            }
            ++slot;
        }
    }

    /**
     * Draws the ref labels from the left of the cell.
     * @param painter Draws.
     * @param rect The cell's text area.
     * @param labels The labels and their places.
     * @param font The cell's font.
     * @returns What is left of the text area for the subject.
     */
    static QRect drawRefs(QPainter *painter, const QRect& rect, const QList<RefLabel>& labels, const QFont& font)
    {
        QRect rest = rect;
        painter->setFont(font);
        for (const RefLabel& refLabel : labels) {
            const QRect& label = refLabel.rect;
            const QColor color = labelColor(*refLabel.ref);
            painter->setPen(color.darker());
            painter->setBrush(color);
            painter->drawRect(label.adjusted(0, 0, -1, -1));
            painter->setPen(color.lightness() < maxLabelBrightness ? Qt::white : Qt::black);
            painter->drawText(label, Qt::AlignCenter | Qt::TextSingleLine, refLabel.ref->name);
            rest.setLeft(label.right() + 1 + labelSpacing);
        }
        return rest;
    }
};

} // namespace

namespace kgt
{

GitLogList::GitLogList(QWidget *parent)
    : QTreeView(parent)
    , m_model(new GitLogListModel(this))
{
    AppUtils::initIconResources();
    setModel(m_model);
    setItemDelegate(new GitLogListDelegate(this));
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    QHeaderView *columns = header();
    columns->setStretchLastSection(false);
    columns->setSectionResizeMode(QHeaderView::Interactive);
    columns->setSectionResizeMode(GitLogListModel::MessageColumn, QHeaderView::Stretch);
    columns->resizeSection(GitLogListModel::GraphColumn, graphColumnWidth);
    // Room for every icon, as TortoiseGit sizes the column
    const auto iconCount = static_cast<int>(GitLogListModel::shownActions().size());
    columns->resizeSection(GitLogListModel::ActionsColumn, (iconCount * (maxIconSize + iconSpacing)) + (2 * labelPadding));
    columns->resizeSection(GitLogListModel::AuthorColumn, authorColumnWidth);
    columns->resizeSection(GitLogListModel::DateColumn, dateColumnWidth);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &GitLogList::selectedRevChanged);
}

GitLogList::~GitLogList() = default;

void GitLogList::setWorkingTree(const QString& root)
{
    m_workingTree = root;
}

void GitLogList::setPathFilter(const QStringList& paths)
{
    m_pathFilter = paths;
}

void GitLogList::setLastSelectedHash(const QString& hash)
{
    m_lastSelectedHash = hash;
    restoreSelection(0);
}

void GitLogList::reload()
{
    Q_ASSERT(!m_workingTree.isEmpty());
    if (const int row = selectedRow(); row >= 0) {
        m_lastSelectedHash = m_model->rev(row).hash;
    }
    cancelCopy();
    m_logCall.reset();
    m_tagInfoCall.reset();
    m_simpleListCall.reset();
    m_simpleListQueue.clear();
    m_filesCall.reset();
    m_filesQueue.clear();
    m_filesRow = -1;
    m_pendingCompare.reset();
    m_pending.clear();
    m_errorMessage.clear();
    m_childCount.clear();
    m_tagInfo.clear();
    m_oldestCommit = QDateTime::currentDateTime();
    m_latestCommit = QDate(1971, 1, 2).startOfDay();
    m_model->clear();
    m_model->setOnlyFirstParent(m_walkOptions.firstParent);
    m_model->setGraphShown((m_showFilter & FilterShowMergePoints) && !m_logFilter.isFilterActive());
    setColumnHidden(GitLogListModel::GraphColumn, m_walkOptions.follow); // ShowGraphColumn
    m_isLoading = true;
    if (m_logFilter.needsTagInfo()) {
        m_tagInfoCall = Git(m_workingTree).runAsync(GitRevLoglist::tagInfoArguments(), RunMode::ReadOnly);
        connect(m_tagInfoCall.get(), &GitCall::finished, this, &GitLogList::onTagInfoFinished);
    } else {
        startLog();
    }
    viewport()->update();
}

void GitLogList::startLog()
{
    m_logCall = Git(m_workingTree).runAsync(GitRevLoglist::logArguments(m_pathFilter, m_range, m_walkOptions), RunMode::ReadOnly);
    connect(m_logCall.get(), &GitCall::outputAvailable, this, &GitLogList::onOutputAvailable);
    connect(m_logCall.get(), &GitCall::finished, this, &GitLogList::onLogFinished);
}

void GitLogList::setRange(const QString& range, bool noMerges)
{
    m_range = range;
    m_walkOptions.noMerges = noMerges;
}

void GitLogList::showText(const QString& text)
{
    cancelCopy();
    if (GitCall *call = m_logCall.release()) {
        call->disconnect(this);
        call->deleteLater();
    }
    m_tagInfoCall.reset();
    m_simpleListCall.reset();
    m_simpleListQueue.clear();
    m_isLoading = false;
    m_filesCall.reset();
    m_filesQueue.clear();
    m_filesRow = -1;
    m_pending.clear();
    m_pendingCompare.reset();
    m_model->clear();
    m_errorMessage = text;
    viewport()->update();
}

void GitLogList::setRebaseMenuEnabled(bool enabled)
{
    m_rebaseMenu = enabled;
}

bool GitLogList::isRebaseMenuEnabled() const
{
    return m_rebaseMenu;
}

void GitLogList::setCherryPick(bool cherryPick)
{
    m_cherryPick = cherryPick;
}

void GitLogList::setLogMenuEnabled(bool enabled)
{
    m_logMenu = enabled;
}

std::optional<GitRef> GitLogList::refLabelAt(int row, const QPoint& pos) const
{
    Q_ASSERT(row >= 0 && row < m_model->rowCount());
    const QModelIndex index = m_model->index(row, GitLogListModel::MessageColumn);
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.rect = visualRect(index);
    const auto *delegate = dynamic_cast<const GitLogListDelegate *>(itemDelegate());
    Q_ASSERT(delegate);
    for (const RefLabel& label : delegate->refLabels(option, index)) {
        if (label.rect.contains(pos)) {
            return label.ref->type == GitRef::Type::Head ? std::nullopt : std::optional<GitRef>(*label.ref);
        }
    }
    return std::nullopt;
}

bool GitLogList::maySetRebaseAction(int row, unsigned int action) const
{
    // CGitLogList::SetSelectedRebaseAction's conditions
    const GitRevLoglist& rev = m_model->rev(row);
    if (rev.rebaseAction & (GitRevLoglist::RebaseCurrent | GitRevLoglist::RebaseDone)) {
        return false;
    }
    if (action == GitRevLoglist::RebaseSquash && (row == m_model->rowCount() - 1 || (!m_cherryPick && rev.parents.size() != 1))) {
        return false;
    }
    return true;
}

QList<int> GitLogList::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::ranges::sort(rows);
    return rows;
}

void GitLogList::setSelectedRebaseAction(unsigned int action)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    for (const int row : rows) {
        if (maySetRebaseAction(row, action)) {
            m_model->setRebaseAction(row, action);
        }
    }
    Q_EMIT rebaseActionChanged();
}

void GitLogList::setUnselectedRebaseAction(unsigned int action)
{
    const QList<int> selected = selectedRows();
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (!selected.contains(row) && maySetRebaseAction(row, action)) {
            m_model->setRebaseAction(row, action);
        }
    }
    Q_EMIT rebaseActionChanged();
}

void GitLogList::shiftSelectedRebaseAction()
{
    for (const int row : selectedRows()) {
        const GitRevLoglist& rev = m_model->rev(row);
        unsigned int action = rev.rebaseAction;
        switch (action) {
        case GitRevLoglist::RebasePick:
            action = GitRevLoglist::RebaseSkip;
            break;
        case GitRevLoglist::RebaseSkip:
            action = GitRevLoglist::RebaseEdit;
            break;
        case GitRevLoglist::RebaseEdit:
            action = GitRevLoglist::RebaseSquash;
            // TortoiseGit's condition, kept as it is: the last row with one parent goes back to Pick
            if (row == m_model->rowCount() - 1 && (m_cherryPick || rev.parents.size() == 1)) {
                action = GitRevLoglist::RebasePick;
            }
            break;
        case GitRevLoglist::RebaseSquash:
            action = GitRevLoglist::RebasePick;
            break;
        default:
            continue;
        }
        m_model->setRebaseAction(row, action);
    }
}

void GitLogList::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_rebaseMenu) {
        showRebaseMenu(event);
    } else if (m_logMenu) {
        showLogMenu(event);
    }
}

void GitLogList::showRebaseMenu(QContextMenuEvent *event)
{
    // The rebase entries of CGitLogListBase::OnContextMenu
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    // pSelLogEntry: the entry the menu was opened on
    const QModelIndex current = currentIndex();
    const int selRow = current.isValid() && rows.contains(current.row()) ? current.row() : rows.constFirst();
    const GitRevLoglist& selRev = m_model->rev(selRow);
    if (selRev.rebaseAction & (GitRevLoglist::RebaseCurrent | GitRevLoglist::RebaseDone)) {
        return;
    }
    const int lastRow = m_model->rowCount() - 1;
    QMenu menu(this);
    const auto addEntry = [&](const QString& icon, const QString& text, unsigned int action) {
        menu.addAction(QIcon::fromTheme(icon), text, this, [this, action] {
            setSelectedRebaseAction(action);
        });
    };
    addEntry(QStringLiteral("dialog-ok-apply"), i18n("Pick"), GitRevLoglist::RebasePick);
    if (rows.constFirst() != lastRow && rows.constLast() != lastRow && (m_cherryPick || selRev.parents.size() == 1)) {
        addEntry(QStringLiteral("merge"), i18n("Squash (with commit below)"), GitRevLoglist::RebaseSquash);
    }
    addEntry(QStringLiteral("document-edit"), i18n("Edit"), GitRevLoglist::RebaseEdit);
    addEntry(QStringLiteral("go-jump"), i18n("Skip"), GitRevLoglist::RebaseSkip);
    menu.exec(event->globalPos());
}

bool GitLogList::isBusy() const
{
    return m_isLoading;
}

GitLogListModel *GitLogList::logModel() const
{
    return m_model;
}

int GitLogList::selectedRow() const
{
    const QModelIndexList rows = selectionModel()->selectedRows();
    return rows.size() == 1 ? rows.constFirst().row() : -1;
}

void GitLogList::keyPressEvent(QKeyEvent *event)
{
    // IDR_ACC_LOGDLG's ID_EDIT_COPY (CLogDlg::OnEditCopy); the rebase dialog's list keeps the view's own copy
    if (m_logMenu && event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard(CopyFormat::Full);
        return;
    }
    if (event->key() == Qt::Key_F5 || event->matches(QKeySequence::Refresh)) {
        reload();
        return;
    }
    QTreeView::keyPressEvent(event);
}

void GitLogList::requestFiles(int row)
{
    Q_ASSERT(row >= 0 && row < m_model->rowCount());
    if (row == m_filesRow || m_model->rev(row).filesState != GitRevLoglist::FilesState::NotLoaded) {
        return;
    }
    m_filesQueue.removeOne(row);
    m_filesQueue.append(row);
    startNextFiles();
}

void GitLogList::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);
    if (m_model->rowCount() > 0) {
        requestVisibleFiles();
        return;
    }
    QString message;
    if (isBusy()) {
        message = i18n("Loading...");
    } else {
        message = m_errorMessage;
    }
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, message);
}

void GitLogList::onOutputAvailable()
{
    Q_ASSERT(m_logCall);
    m_pending.append(m_logCall->takeAvailableOutput());
    std::ignore = takeRecords();
}

void GitLogList::onLogFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_logCall.release();
    Q_ASSERT(call);
    call->deleteLater();
    try {
        m_pending.append(call->output());
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // CGitLogListBase::BeginFetchLog: a branch without commits has an empty log
        if (m_range.isEmpty() && Git(m_workingTree).isUnbornBranch()) {
            finishLoadIfDone();
            return;
        }
        failLoad(e.error());
        return;
    }
    if (!takeRecords()) {
        return;
    }
    if (!m_pending.isEmpty()) {
        failLoad({.category = ErrorCategory::Parse, .message = QStringLiteral("git log ended inside a record")});
        return;
    }
    finishLoadIfDone();
}

void GitLogList::requestVisibleFiles()
{
    // GitRevLoglist::GetFiles queues a commit's files when its row is drawn
    const int first = indexAt(viewport()->rect().topLeft()).row();
    if (first < 0) {
        return;
    }
    int last = indexAt(viewport()->rect().bottomLeft()).row();
    if (last < 0) {
        last = m_model->rowCount() - 1;
    }
    // Bottom to top, so the topmost row, asked for last, is fetched first
    for (int row = last; row >= first; --row) {
        requestFiles(row);
    }
}

void GitLogList::startNextFiles()
{
    if (m_filesCall || m_filesQueue.isEmpty()) {
        return;
    }
    m_filesRow = m_filesQueue.takeLast();
    m_filesDone.clear();
    startFilesCall();
}

void GitLogList::startFilesCall()
{
    const GitRevLoglist& rev = m_model->rev(m_filesRow);
    m_filesCall = Git(m_workingTree).runAsync(GitRevLoglist::filesArguments(rev, m_filesDone.size()), RunMode::ReadOnly);
    connect(m_filesCall.get(), &GitCall::finished, this, &GitLogList::onFilesFinished);
}

void GitLogList::onFilesFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_filesCall.release();
    Q_ASSERT(call);
    call->deleteLater();
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        finishFiles(false);
        return;
    }
    Result<GitRevFiles> files = GitRevLoglist::parseFiles(output);
    if (!files) {
        finishFiles(false);
        return;
    }
    m_filesDone.append(std::move(*files));
    if (m_filesDone.size() < m_model->rev(m_filesRow).diffCount()) {
        startFilesCall();
        return;
    }
    finishFiles(true);
}

void GitLogList::finishFiles(bool succeeded)
{
    const int row = std::exchange(m_filesRow, -1);
    if (succeeded) {
        m_model->setFiles(row, std::exchange(m_filesDone, {}));
    } else {
        m_filesDone.clear();
        m_model->setFilesFailed(row);
    }
    Q_EMIT filesLoaded(row);
    finishCopyIfReady();
    startNextFiles();
    onCompareFilesLoaded(row);
}

bool GitLogList::takeRecords()
{
    Result<QList<GitRevLoglist>> revs = GitRevLoglist::takeRecords(m_pending);
    if (!revs) {
        failLoad(revs.error());
        return false;
    }
    handleRevs(*revs);
    return true;
}

void GitLogList::failLoad(const Error& error)
{
    if (GitCall *call = m_logCall.release()) {
        // It may be the one emitting: stop it reaching this list, and delete it from the event loop
        call->disconnect(this);
        call->deleteLater();
    }
    m_tagInfoCall.reset();
    m_simpleListCall.reset();
    m_simpleListQueue.clear();
    m_isLoading = false;
    m_pending.clear();
    m_errorMessage = error.message;
    viewport()->update();
    Q_EMIT loadFailed(error);
}

} // namespace kgt
