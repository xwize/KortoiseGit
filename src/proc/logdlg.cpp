// SPDX-FileCopyrightText: 2003-2009, 2015 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/LogDlg.cpp

#include "logdlg.hpp"

#include "apputils.hpp"
#include "gitloglist.hpp"
#include "gitstatuslistctrl.hpp"
#include "logdlgfilter.hpp"

#include "git/git.hpp"
#include "git/gitrevloglist.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QAction>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{

using namespace kgt;

constexpr int logListStretch = 5; // How the splitter shares the dialog's height, as TortoiseGit's layout does
constexpr int msgViewStretch = 2;
constexpr int fileListStretch = 3;
constexpr QSize initialSize(900, 700);
constexpr int filterDelay = 1000; // LOGFILTER_TIMER's timeout in ms
constexpr int dateDelay = 10; // LOGFTIME_TIMER's timeout in ms
constexpr int invalidShiftPercent = 10; // CFilterEdit::Validate's SHIFT_PERCENTAGE
constexpr int fullColor = 255;
constexpr int percent = 100;

/**
 * @param name A per-repository value of TortoiseGit's LogDialog registry key, such as "ShowTags".
 * @returns Where it is kept; the keys are repository paths.
 */
KConfigGroup labelSettings(const QString& name)
{
    return KSharedConfig::openConfig()->group(QStringLiteral("LogDialog")).group(name);
}

/**
 * Ported from CFilterEdit::Validate.
 * @param color The search box's normal background.
 * @returns It slightly shifted to red.
 */
QColor invalidBackground(const QColor& color)
{
    const int red = std::max(std::min(color.red() * (percent + invalidShiftPercent) / percent, fullColor), fullColor * invalidShiftPercent / percent);
    const int green = color.green() * (percent - invalidShiftPercent) / percent;
    const int blue = color.blue() * (percent - invalidShiftPercent) / percent;
    return {red, green, blue};
}

/**
 * The test of CLogDlg::FillLogMessageCtrl for a file outside the log's path.
 * @param entry A changed file.
 * @param matchPath The path the log is limited to; a directory ends in '/'.
 * @returns Whether the file is that path, or under it; for a rename or copy, the source counts too.
 */
bool isRelatedPath(const GitStatusListEntry& entry, const QString& matchPath)
{
    const auto matches = [&matchPath](const QString& path) {
        // A submodule at the directory itself doesn't end with the slash
        return path.startsWith(matchPath) || (matchPath.endsWith(QLatin1Char('/')) && path + QLatin1Char('/') == matchPath);
    };
    return matches(entry.path) || ((entry.actions & (TGitPath::Action::Replaced | TGitPath::Action::Copy)) && matches(entry.origPath));
}

} // namespace

namespace kgt
{

LogDlg::LogDlg(const QStringList& paths, const QString& range, QWidget *parent)
    : QDialog(parent)
{
    Q_ASSERT(!paths.isEmpty());
    m_workingTree = Git::repositoryRoot(paths.constFirst());
    if (QFileInfo(TGitPath::resolvedPath(paths.constFirst())).absoluteFilePath().startsWith(m_workingTree + QLatin1Char('/'))) {
        m_pathFilter = TGitPath::relativePaths(m_workingTree, paths);
    }
    for (const QString& path : std::as_const(m_pathFilter)) {
        const bool isDirectory = QFileInfo(QDir(m_workingTree).filePath(path)).isDir();
        m_matchPaths.append(isDirectory ? path + QLatin1Char('/') : path);
    }
    loadSettings();
    createWidgets();

    // CAppUtils::SetWindowTitle: the path the log was opened for, or the working tree for several
    const QString titlePath = paths.size() == 1 ? QDir::toNativeSeparators(paths.constFirst()) : m_workingTree;
    setWindowTitle(i18nc("@title:window %1 is a path", "%1 - Log Messages", titlePath));

    m_logList->setWorkingTree(m_workingTree);
    m_logList->setPathFilter(m_pathFilter);
    m_logList->setRange(range);
    refresh(false);
    m_logList->setFocus();
}

LogDlg::~LogDlg()
{
    saveLabelSettings();
}

const QString& LogDlg::workingTree() const
{
    return m_workingTree;
}

const QStringList& LogDlg::pathFilter() const
{
    return m_pathFilter;
}

GitLogList *LogDlg::logList() const
{
    return m_logList;
}

GitStatusListCtrl *LogDlg::changedFileList() const
{
    return m_changedFileList;
}

QString LogDlg::messageText() const
{
    return m_msgView->toPlainText();
}

QPushButton *LogDlg::refreshButton() const
{
    return m_refresh;
}

QPushButton *LogDlg::walkBehaviorButton() const
{
    return m_walkBehavior;
}

QPushButton *LogDlg::viewButton() const
{
    return m_view;
}

QMenu *LogDlg::walkBehaviorMenu() const
{
    return m_walkMenu;
}

QMenu *LogDlg::viewMenu() const
{
    return m_viewMenu;
}

QMenu *LogDlg::filterMenu() const
{
    return m_filterMenu;
}

QLineEdit *LogDlg::searchEdit() const
{
    return m_search;
}

QDateEdit *LogDlg::dateFrom() const
{
    return m_dateFrom;
}

QDateEdit *LogDlg::dateTo() const
{
    return m_dateTo;
}

void LogDlg::setSelect(bool select)
{
    m_select = select;
    m_buttons->setStandardButtons(select ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Ok);
    enableOkButton();
}

const QString& LogDlg::selectedHash() const
{
    return m_selectedHash;
}

QDialogButtonBox *LogDlg::buttonBox() const
{
    return m_buttons;
}

void LogDlg::accept()
{
    if (m_select) {
        const int row = m_logList->selectedRow();
        if (row < 0) {
            return;
        }
        m_selectedHash = m_logList->logModel()->rev(row).hash;
    }
    QDialog::accept();
}

void LogDlg::enableOkButton()
{
    // CLogDlg::EnableOKButton, with m_bSelectionMustBeSingle
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_select || m_logList->selectedRow() >= 0);
}

bool LogDlg::eventFilter(QObject *watched, QEvent *event)
{
    // PreTranslateMessage's VK_RETURN: Enter in the search box applies the filter at once, and doesn't close the dialog
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto *keyEvent = dynamic_cast<const QKeyEvent *>(event);
        if (keyEvent && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)) {
            refresh(false);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void LogDlg::loadSettings()
{
    // CLogDlg's constructor
    const KConfigGroup general = AppUtils::generalSettings();
    m_selectedFilters = general.readEntry("SelectedLogFilters", static_cast<unsigned int>(LogFilterAll));
    m_filterWithRegex = general.readEntry("UseRegexFilter", false);
    m_filterCaseSensitively = general.readEntry("FilterCaseSensitively", false);
    m_showTags = labelSettings(QStringLiteral("ShowTags")).readEntry(m_workingTree, true);
    m_showLocalBranches = labelSettings(QStringLiteral("ShowLocalBranches")).readEntry(m_workingTree, true);
    m_showRemoteBranches = labelSettings(QStringLiteral("ShowRemoteBranches")).readEntry(m_workingTree, true);
    m_showOtherRefs = labelSettings(QStringLiteral("ShowOtherRefs")).readEntry(m_workingTree, true);
}

void LogDlg::saveLabelSettings() const
{
    // ~CLogDlg
    labelSettings(QStringLiteral("ShowTags")).writeEntry(m_workingTree, m_showTags);
    labelSettings(QStringLiteral("ShowLocalBranches")).writeEntry(m_workingTree, m_showLocalBranches);
    labelSettings(QStringLiteral("ShowRemoteBranches")).writeEntry(m_workingTree, m_showRemoteBranches);
    labelSettings(QStringLiteral("ShowOtherRefs")).writeEntry(m_workingTree, m_showOtherRefs);
    KSharedConfig::openConfig()->sync();
}

void LogDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(createFilterBar());

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    m_logList = new GitLogList(splitter);
    m_msgView = new QTextEdit(splitter);
    m_msgView->setReadOnly(true);
    m_msgView->setAcceptRichText(false);
    m_changedFileList = new GitStatusListCtrl(splitter);
    m_changedFileList->setHasCheckboxes(false);
    m_changedFileList->setWorkingTree(m_workingTree);
    m_changedFileList->setShowFlags(GitStatusListModel::Show::Versioned, {});

    splitter->setStretchFactor(0, logListStretch);
    splitter->setStretchFactor(1, msgViewStretch);
    splitter->setStretchFactor(2, fileListStretch);
    layout->addWidget(splitter, 1);
    layout->addLayout(createBottomRow());

    createWalkBehaviorMenu();
    createViewMenu();
    createFilterMenu();
    setFilterCueText();
    updateSearchValidity();

    applyRefMask();

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(filterDelay);
    connect(&m_filterTimer, &QTimer::timeout, this, [this] {
        refresh(false);
    });
    m_dateTimer.setSingleShot(true);
    m_dateTimer.setInterval(dateDelay);
    connect(&m_dateTimer, &QTimer::timeout, this, [this] {
        refresh(false);
    });

    // IDR_ACC_LOGDLG's ID_LOGDLG_REFRESH
    for (const QKeySequence& key : {QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_F5), QKeySequence(Qt::SHIFT | Qt::Key_F5)}) {
        connect(new QShortcut(key, this), &QShortcut::activated, this, [this] {
            refresh(true);
        });
    }

    connect(m_logList, &GitLogList::loaded, this, &LogDlg::onLogLoaded);
    connect(m_logList, &GitLogList::loadFailed, this, [this] {
        setWalkControlsEnabled(true);
    });
    connect(m_logList, &GitLogList::selectedRevChanged, this, &LogDlg::onSelectedRevChanged);
    connect(m_logList->logModel(), &QAbstractItemModel::modelReset, this, &LogDlg::onSelectedRevChanged);
    connect(m_logList, &GitLogList::filesLoaded, this, &LogDlg::onFilesLoaded);
    resize(initialSize);
}

QLayout *LogDlg::createFilterBar()
{
    auto *row = new QHBoxLayout;
    m_dateFrom = new QDateEdit(this);
    m_dateTo = new QDateEdit(this);
    for (QDateEdit *edit : {m_dateFrom, m_dateTo}) {
        edit->setCalendarPopup(true);
        edit->setDateRange(QDate(1970, 1, 1), QDate(2100, 1, 2)); // OnInitDialog's SetRange
    }
    auto *fromLabel = new QLabel(i18n("From:"), this);
    fromLabel->setBuddy(m_dateFrom);
    auto *toLabel = new QLabel(i18n("To:"), this);
    toLabel->setBuddy(m_dateTo);
    connect(m_dateFrom, &QDateEdit::dateChanged, this, &LogDlg::onFromDateChanged);
    connect(m_dateTo, &QDateEdit::dateChanged, this, &LogDlg::onToDateChanged);

    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    const QAction *filterIcon = m_search->addAction(QIcon::fromTheme(QStringLiteral("view-filter")), QLineEdit::LeadingPosition);
    connect(filterIcon, &QAction::triggered, this, &LogDlg::onFilterIconClicked);
    connect(m_search, &QLineEdit::textChanged, this, &LogDlg::onSearchTextChanged);

    row->addWidget(fromLabel);
    row->addWidget(m_dateFrom);
    row->addWidget(toLabel);
    row->addWidget(m_dateTo);
    row->addWidget(m_search, 1);
    return row;
}

QLayout *LogDlg::createBottomRow()
{
    auto *row = new QHBoxLayout;
    m_refresh = new QPushButton(i18n("Refresh"), this);
    m_walkBehavior = new QPushButton(i18n("Walk Be&havior"), this);
    m_view = new QPushButton(i18n("&View"), this);
    for (QPushButton *button : {m_refresh, m_walkBehavior, m_view}) {
        button->setAutoDefault(false);
        row->addWidget(button);
    }
    // BS_CHECKBOX: the buttons show as pressed while their state says so
    m_walkBehavior->setCheckable(true);
    m_view->setCheckable(true);
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        refresh(true);
    });
    connect(m_walkBehavior, &QPushButton::clicked, this, &LogDlg::onWalkBehaviorClicked);
    connect(m_view, &QPushButton::clicked, this, &LogDlg::onViewClicked);
    row->addStretch();

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &LogDlg::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    row->addWidget(m_buttons);
    return row;
}

QAction *LogDlg::addCheckAction(QMenu *menu, const QString& text, bool checked)
{
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    action->setChecked(checked);
    return action;
}

void LogDlg::createWalkBehaviorMenu()
{
    // OnBnClickedWalkBehaviour
    m_walkMenu = new QMenu(this);
    const auto addWalk = [this](const QString& text, bool GitLogOptions::*member) {
        return connect(addCheckAction(m_walkMenu, text, false), &QAction::toggled, this, [this, member](bool checked) {
            m_walkOptions.*member = checked;
            updateWalkBehaviorChecked();
            refresh(false);
        });
    };
    addWalk(i18n("First Parent"), &GitLogOptions::firstParent);
    addWalk(i18n("No &merges"), &GitLogOptions::noMerges);
    addWalk(i18n("&Follow renames"), &GitLogOptions::follow);
    // Follow doesn't work for a folder, or without a path (BeginFetchLog)
    const bool isSingleFile = m_matchPaths.size() == 1 && !m_matchPaths.constFirst().endsWith(QLatin1Char('/'));
    m_walkMenu->actions().constLast()->setEnabled(isSingleFile);
    addWalk(i18n("Full history"), &GitLogOptions::fullHistory);
    m_walkMenu->addSeparator();
    m_compressedAction = addCheckAction(m_walkMenu, i18n("&Compressed Graph"), false);
    m_labeledAction = addCheckAction(m_walkMenu, i18n("Show la&beled commits only"), false);
    connect(m_compressedAction, &QAction::triggered, this, [this] {
        onCompressedGraph(m_compressedGraph == 1 ? 0 : 1);
    });
    connect(m_labeledAction, &QAction::triggered, this, [this] {
        onCompressedGraph(m_compressedGraph == 2 ? 0 : 2);
    });
}

void LogDlg::createViewMenu()
{
    // OnBnClickedView, without what needs features not ported yet (unversioned files, Gravatar, View Patch)
    m_viewMenu = new QMenu(this);
    m_hidePathsAction = addCheckAction(m_viewMenu, i18n("H&ide Unrelated Changed Paths"), m_hidePaths == HidePaths::Hide);
    m_grayPathsAction = addCheckAction(m_viewMenu, i18n("&Gray Unrelated Changed Paths"), m_hidePaths == HidePaths::Gray);
    connect(m_hidePathsAction, &QAction::triggered, this, [this] {
        onHidePaths(m_hidePaths == HidePaths::Hide ? HidePaths::None : HidePaths::Hide);
    });
    connect(m_grayPathsAction, &QAction::triggered, this, [this] {
        onHidePaths(m_hidePaths == HidePaths::Gray ? HidePaths::None : HidePaths::Gray);
    });
    m_viewMenu->addSeparator();
    QMenu *labels = m_viewMenu->addMenu(i18n("&Labels"));
    addLabelAction(labels, i18n("&Tags"), &m_showTags);
    addLabelAction(labels, i18n("&Local branches"), &m_showLocalBranches);
    addLabelAction(labels, i18n("&Remote branches"), &m_showRemoteBranches);
    addLabelAction(labels, i18n("Other refs"), &m_showOtherRefs);
}

void LogDlg::addLabelAction(QMenu *menu, const QString& text, bool *shown)
{
    connect(addCheckAction(menu, text, *shown), &QAction::toggled, this, [this, shown](bool checked) {
        *shown = checked;
        handleShowLabels();
    });
}

void LogDlg::addFilterAction(const QString& text, unsigned int bit)
{
    QAction *action = addCheckAction(m_filterMenu, text, m_selectedFilters & bit);
    action->setData(bit);
    connect(action, &QAction::triggered, this, [this, bit] {
        onFilterActionTriggered(bit);
    });
}

void LogDlg::createFilterMenu()
{
    // OnClickedInfoIcon, without Bug-IDs (no bugtraq support)
    m_filterMenu = new QMenu(this);
    addFilterAction(i18n("Subject"), LogFilterSubject);
    addFilterAction(i18n("Messages"), LogFilterMessages);
    addFilterAction(i18n("Paths"), LogFilterPaths);
    addFilterAction(i18n("Authors"), LogFilterAuthors);
    addFilterAction(i18n("Emails"), LogFilterEmails);
    addFilterAction(i18n("SHA-1"), LogFilterRevs);
    addFilterAction(i18n("Refname"), LogFilterRefName);
    addFilterAction(i18n("Tag Info"), LogFilterAnnotatedTag);
    addFilterAction(i18n("Notes"), LogFilterNotes);
    m_filterMenu->addSeparator();
    connect(m_filterMenu->addAction(i18n("Toggle filters")), &QAction::triggered, this, [this] {
        onFilterActionTriggered(LogFilterToggle);
    });
    connect(m_filterMenu->addAction(i18n("All")), &QAction::triggered, this, [this] {
        onFilterActionTriggered(LogFilterAll);
    });
    m_filterMenu->addSeparator();
    m_regexAction = addCheckAction(m_filterMenu, i18n("Use regular expression"), m_filterWithRegex);
    connect(m_regexAction, &QAction::triggered, this, [this] {
        onFilterActionTriggered(LogFilterRegex);
    });
    m_caseAction = addCheckAction(m_filterMenu, i18n("Case-sensitive"), m_filterCaseSensitively);
    connect(m_caseAction, &QAction::triggered, this, [this] {
        onFilterActionTriggered(LogFilterCase);
    });
}

void LogDlg::refresh(bool clearFilter)
{
    // CLogDlg::Refresh and CGitLogListBase::Refresh
    m_filterTimer.stop();
    if (clearFilter) {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
        updateSearchValidity();
    }
    m_logList->setWalkOptions(m_walkOptions);
    m_logList->setLogFilter(LogDlgFilter(m_search->text(), m_filterWithRegex, m_selectedFilters, m_filterCaseSensitively));
    setWalkControlsEnabled(false); // OnLogListLoading's GITLOG_START
    m_logList->reload();
}

void LogDlg::onSearchTextChanged(const QString& text)
{
    // OnEnChangeSearchedit
    updateSearchValidity();
    if (text.isEmpty()) {
        refresh(true);
        return;
    }
    if (!m_filterWithRegex || FilterHelper::isValidRegexp(text)) {
        m_filterTimer.start();
    } else {
        m_filterTimer.stop();
    }
}

void LogDlg::onLogLoaded()
{
    // OnLogListLoading's GITLOG_END
    setWalkControlsEnabled(true);
    auto [begin, end] = m_logList->timeRange();
    if (m_walkOptions.from > 0) {
        begin = QDateTime::fromSecsSinceEpoch(m_walkOptions.from);
    }
    if (m_walkOptions.to != -1) {
        end = QDateTime::fromSecsSinceEpoch(m_walkOptions.to);
    }
    const QSignalBlocker fromBlocker(m_dateFrom);
    const QSignalBlocker toBlocker(m_dateTo);
    m_dateFrom->setDate(begin.date());
    m_dateTo->setDate(end.date());
}

void LogDlg::onFromDateChanged()
{
    // OnDtnDatetimechangeDatefrom
    QDate date = m_dateFrom->date();
    if (date > m_dateTo->date()) {
        date = m_dateTo->date();
        const QSignalBlocker blocker(m_dateFrom);
        m_dateFrom->setDate(date);
    }
    const qint64 from = std::max<qint64>(date.startOfDay().toSecsSinceEpoch(), 0);
    if (from != m_walkOptions.from) {
        m_walkOptions.from = from;
        m_dateTimer.start();
    }
}

void LogDlg::onToDateChanged()
{
    // OnDtnDatetimechangeDateto
    QDate date = m_dateTo->date();
    if (date < m_dateFrom->date()) {
        date = m_dateFrom->date();
        const QSignalBlocker blocker(m_dateTo);
        m_dateTo->setDate(date);
    }
    const qint64 newest = std::max<qint64>(date.endOfDay().toSecsSinceEpoch(), 0);
    if (newest != m_walkOptions.to) {
        m_walkOptions.to = newest;
        m_dateTimer.start();
    }
}

void LogDlg::onWalkBehaviorClicked()
{
    m_walkBehavior->setChecked(true);
    m_walkMenu->exec(m_walkBehavior->mapToGlobal(QPoint(0, -m_walkMenu->sizeHint().height())));
    updateWalkBehaviorChecked();
}

void LogDlg::onViewClicked()
{
    m_view->setChecked(true);
    m_viewMenu->exec(m_view->mapToGlobal(QPoint(0, -m_viewMenu->sizeHint().height())));
    m_view->setChecked(false);
}

void LogDlg::onFilterIconClicked()
{
    m_filterMenu->exec(m_search->mapToGlobal(QPoint(0, m_search->height())));
}

void LogDlg::onFilterActionTriggered(unsigned int bit)
{
    // OnClickedInfoIcon's handling of the selection
    KConfigGroup general = AppUtils::generalSettings();
    switch (bit) {
    case LogFilterRegex:
        m_filterWithRegex = !m_filterWithRegex;
        general.writeEntry("UseRegexFilter", m_filterWithRegex);
        updateSearchValidity();
        break;
    case LogFilterCase:
        m_filterCaseSensitively = !m_filterCaseSensitively;
        general.writeEntry("FilterCaseSensitively", m_filterCaseSensitively);
        break;
    case LogFilterToggle:
        m_selectedFilters = ~m_selectedFilters & LogFilterAll;
        break;
    case LogFilterAll:
        m_selectedFilters = LogFilterAll;
        break;
    default:
        m_selectedFilters ^= bit;
        break;
    }
    updateFilterActions();
    setFilterCueText();
    general.writeEntry("SelectedLogFilters", m_selectedFilters);
    general.sync();
    // Reload only if a search text is entered
    if (m_logList->logFilter().isFilterActive()) {
        m_filterTimer.start();
    }
}

void LogDlg::onCompressedGraph(int mode)
{
    // OnBnClickedCompressedGraph
    m_compressedGraph = mode;
    m_compressedAction->setChecked(mode == 1);
    m_labeledAction->setChecked(mode == 2);
    unsigned int showFilter = GitLogList::FilterShowAll;
    if (mode == 2) {
        showFilter = GitLogList::FilterShowRefs;
    } else if (mode == 1) {
        showFilter = GitLogList::FilterShowRefs | GitLogList::FilterShowMergePoints;
    }
    m_logList->setShowFilter(showFilter);
    updateWalkBehaviorChecked();
    refresh(false);
}

void LogDlg::onHidePaths(HidePaths mode)
{
    // OnBnClickedHidepaths
    m_hidePaths = mode;
    m_hidePathsAction->setChecked(mode == HidePaths::Hide);
    m_grayPathsAction->setChecked(mode == HidePaths::Gray);
    onSelectedRevChanged();
}

void LogDlg::applyRefMask()
{
    // HandleShowLabels' m_ShowRefMask; stash and bisect labels have no menu entry and stay on
    unsigned int refMask = GitLogListModel::ShowAllRefs;
    const auto apply = [&refMask](bool shown, unsigned int bit) {
        refMask = shown ? refMask | bit : refMask & ~bit;
    };
    apply(m_showTags, GitLogListModel::ShowTags);
    apply(m_showLocalBranches, GitLogListModel::ShowLocalBranches);
    apply(m_showRemoteBranches, GitLogListModel::ShowRemoteBranches);
    apply(m_showOtherRefs, GitLogListModel::ShowOtherRefs);
    m_logList->logModel()->setRefMask(refMask);
}

void LogDlg::handleShowLabels()
{
    // CLogDlg::HandleShowLabels
    applyRefMask();
    if (m_compressedGraph != 0) {
        refresh(false); // Remove commits where labels are not shown
    } else {
        m_logList->viewport()->update();
    }
}

void LogDlg::updateWalkBehaviorChecked()
{
    m_walkBehavior->setChecked(m_walkOptions.firstParent || m_walkOptions.noMerges || m_walkOptions.follow || m_walkOptions.fullHistory
                               || m_compressedGraph != 0);
}

void LogDlg::updateFilterActions()
{
    for (QAction *action : m_filterMenu->actions()) {
        if (const unsigned int bit = action->data().toUInt(); bit != 0) {
            action->setChecked(m_selectedFilters & bit);
        }
    }
    m_regexAction->setChecked(m_filterWithRegex);
    m_caseAction->setChecked(m_filterCaseSensitively);
}

void LogDlg::setFilterCueText()
{
    // CLogDlg::SetFilterCueText, which leaves out Tag Info
    QStringList names;
    const auto addName = [this, &names](unsigned int bit, const QString& name) {
        if (m_selectedFilters & bit) {
            names.append(name);
        }
    };
    addName(LogFilterSubject, i18n("Subject"));
    addName(LogFilterMessages, i18n("Messages"));
    addName(LogFilterPaths, i18n("Paths"));
    addName(LogFilterAuthors, i18n("Authors"));
    addName(LogFilterEmails, i18n("Emails"));
    addName(LogFilterRevs, i18n("SHA-1"));
    addName(LogFilterRefName, i18n("Refname"));
    addName(LogFilterNotes, i18n("Notes"));
    m_search->setPlaceholderText(i18n("Filter by") + QLatin1Char(' ') + names.join(QStringLiteral(", ")));
}

void LogDlg::updateSearchValidity()
{
    // CFilterEdit::Validate and CheckRegexpTooltip
    const bool isValid = !m_filterWithRegex || FilterHelper::isValidRegexp(m_search->text());
    QPalette colors; // A default palette follows the dialog's, and the colour scheme
    if (!isValid) {
        colors.setColor(QPalette::Base, invalidBackground(palette().color(QPalette::Base)));
    }
    m_search->setPalette(colors);
    if (m_filterWithRegex) {
        m_search->setToolTip(
            i18n("Regular expressions filter:\n"
                 ".   : any character\n"
                 "c+   : match character c one or more times\n"
                 "c*   : match character c zero or more times\n"
                 "^   : start of line\n"
                 "$   : end of line\n"
                 "(string){n} : match string n times\n"
                 "(abcd)   : subexpression\n"
                 "[aei0-9]   : match a,e,i and 0..9\n"
                 "[^aei0-9] : anything but a,e,i and 0..9\n\n"
                 "\\w   : matches a-z,A-Z,0-9 and _\n"
                 "\\W   : any non-alphanumeric character\n"
                 "\\d   : digits 0-9\n"
                 "\\s   : whitespaces"));
    } else {
        m_search->setToolTip({});
    }
}

void LogDlg::setWalkControlsEnabled(bool enabled)
{
    m_walkBehavior->setEnabled(enabled);
    m_view->setEnabled(enabled);
}

QList<GitStatusListEntry> LogDlg::applyHidePaths(QList<GitStatusListEntry> entries) const
{
    // CLogDlg::FillLogMessageCtrl
    if (m_hidePaths == HidePaths::None || m_matchPaths.isEmpty()) {
        return entries;
    }
    const auto isUnrelated = [this](const GitStatusListEntry& entry) {
        return std::ranges::none_of(m_matchPaths, [&entry](const QString& matchPath) {
            return isRelatedPath(entry, matchPath);
        });
    };
    if (m_hidePaths == HidePaths::Hide) {
        entries.removeIf(isUnrelated);
        return entries;
    }
    for (GitStatusListEntry& entry : entries) {
        entry.isGrayed = isUnrelated(entry);
    }
    return entries;
}

void LogDlg::onSelectedRevChanged()
{
    enableOkButton();
    // CLogDlg::FillLogMessageCtrl
    m_changedFileList->setCurrentVersion({}, {});
    m_changedFileList->clear();
    const int row = m_logList->selectedRow();
    if (row < 0) {
        m_msgView->clear();
        return;
    }
    const GitRevLoglist& rev = m_logList->logModel()->rev(row);
    fillLogMessage(rev);
    showFiles(rev);
    if (rev.filesState == GitRevLoglist::FilesState::NotLoaded) {
        m_logList->requestFiles(row);
    }
}

void LogDlg::fillLogMessage(const GitRevLoglist& rev)
{
    // "SHA-1: <hash>", a blank line, the subject in bold, then the body
    m_msgView->clear();
    QTextCursor cursor(m_msgView->document());
    QTextCharFormat normal;
    normal.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    QTextCharFormat bold = normal;
    bold.setFontWeight(QFont::Bold);
    cursor.insertText(i18n("SHA-1") + QStringLiteral(": ") + rev.hash + QStringLiteral("\n\n"), normal);
    cursor.insertText(rev.subject, bold);
    cursor.insertText(QStringLiteral("\n") + rev.body, normal);
    m_msgView->moveCursor(QTextCursor::Start);
}

void LogDlg::onFilesLoaded(int row)
{
    if (row == m_logList->selectedRow()) {
        showFiles(m_logList->logModel()->rev(row));
    }
}

void LogDlg::showFiles(const GitRevLoglist& rev)
{
    switch (rev.filesState) {
    case GitRevLoglist::FilesState::NotLoaded:
        m_changedFileList->setBusyString(i18n("Fetching changed files..."));
        m_changedFileList->setBusy(true);
        return;
    case GitRevLoglist::FilesState::Failed:
        // TortoiseGit leaves the list busy with this message (GitRevLoglist::m_IsDiffFiles == 2)
        m_changedFileList->setBusyString(i18n("Could not fetch commit or calculate diff."));
        m_changedFileList->setBusy(true);
        return;
    case GitRevLoglist::FilesState::Loaded:
        break;
    }
    m_changedFileList->setCurrentVersion(rev.hash, rev.parents);
    m_changedFileList->updateWithGitPathList(applyHidePaths(GitStatusListModel::makeRevisionEntries(rev.files)));
}

} // namespace kgt
