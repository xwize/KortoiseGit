// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp and GitLogList.cpp

#include "gitloglist.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QPainter>
#include <QStyledItemDelegate>

#include <algorithm>
#include <tuple>
#include <utility>

/**
 * Registers icons.qrc, which a static library has to do itself. Q_INIT_RESOURCE must be outside any namespace.
 */
static void initIconResources()
{
    Q_INIT_RESOURCE(icons);
}

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

constexpr int labelPadding = 3; // Between a ref label's border and its text
constexpr int labelSpacing = 3; // Between two ref labels, and between the last one and the subject
constexpr int maxIconSize = 16; // TortoiseGit's small icons
constexpr int iconSpacing = 2; // Between two action icons
constexpr int graphColumnWidth = 40;
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
        if (!model || (column != GitLogListModel::ActionsColumn && column != GitLogListModel::MessageColumn)) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString subject = std::exchange(opt.text, QString());
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const QRect rect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

        const GitRevLoglist& rev = model->rev(index.row());
        painter->save();
        if (column == GitLogListModel::ActionsColumn) {
            drawActions(painter, rect, rev);
        } else {
            const QRect textRect = drawRefs(painter, rect, rev.refs, opt);
            const bool isSelected = opt.state.testFlag(QStyle::State_Selected);
            painter->setPen(opt.palette.color(isSelected ? QPalette::HighlightedText : QPalette::Text));
            painter->setFont(opt.font);
            const QString elided = opt.fontMetrics.elidedText(subject, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, elided);
        }
        painter->restore();
    }

private:
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
     * @param refs The refs pointing at the commit.
     * @param opt The cell's style.
     * @returns What is left of the text area for the subject.
     */
    static QRect drawRefs(QPainter *painter, const QRect& rect, const QList<GitRef>& refs, const QStyleOptionViewItem& opt)
    {
        QRect rest = rect;
        painter->setFont(opt.font);
        for (const GitRef& ref : refs) {
            if (rest.width() <= 0) {
                break;
            }
            const int width = opt.fontMetrics.horizontalAdvance(ref.name) + (2 * labelPadding);
            const QRect label(rest.left(), rest.top() + 1, std::min(width, rest.width()), rest.height() - 2);
            const QColor color = labelColor(ref);
            painter->setPen(color.darker());
            painter->setBrush(color);
            painter->drawRect(label.adjusted(0, 0, -1, -1));
            painter->setPen(color.lightness() < maxLabelBrightness ? Qt::white : Qt::black);
            painter->drawText(label, Qt::AlignCenter | Qt::TextSingleLine, ref.name);
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
    initIconResources();
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

void GitLogList::reload()
{
    Q_ASSERT(!m_workingTree.isEmpty());
    m_logCall.reset();
    m_filesCall.reset();
    m_filesQueue.clear();
    m_filesRow = -1;
    m_pending.clear();
    m_errorMessage.clear();
    m_model->clear();
    m_logCall = Git(m_workingTree).runAsync(GitRevLoglist::logArguments(m_pathFilter), RunMode::ReadOnly);
    connect(m_logCall.get(), &GitCall::outputAvailable, this, &GitLogList::onOutputAvailable);
    connect(m_logCall.get(), &GitCall::finished, this, &GitLogList::onLogFinished);
    viewport()->update();
}

bool GitLogList::isBusy() const
{
    return m_logCall != nullptr;
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
    viewport()->update();
    Q_EMIT loaded();
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
    startNextFiles();
}

bool GitLogList::takeRecords()
{
    Result<QList<GitRevLoglist>> revs = GitRevLoglist::takeRecords(m_pending);
    if (!revs) {
        failLoad(revs.error());
        return false;
    }
    m_model->appendRevs(*revs);
    return true;
}

void GitLogList::failLoad(const Error& error)
{
    if (GitCall *call = m_logCall.release()) {
        // It may be the one emitting: stop it reaching this list, and delete it from the event loop
        call->disconnect(this);
        call->deleteLater();
    }
    m_pending.clear();
    m_errorMessage = error.message;
    viewport()->update();
    Q_EMIT loadFailed(error);
}

} // namespace kgt
