// SPDX-FileCopyrightText: 2003-2008, 2018 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/FileDiffDlg.cpp and its IDD_DIFFFILES resource: the dialog, its versions
 * and the loading of its list. The file list's context menu is in filediffdlg_menu.cpp.
 */

#include "filediffdlg.hpp"

#include "apputils.hpp"
#include "browserefsdlg.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "git/gitrevloglist.hpp"
#include "gitdiff.hpp"
#include "logdlg.hpp"
#include "reflogdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KWindowConfig>

#include <QAction>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace
{

using namespace kgt;

constexpr int initialWidth = 640;
constexpr int initialHeight = 560;
constexpr int filterDelay = 1000; // IDT_FILTER's timeout in ms
constexpr int inputDelay = 1000; // IDT_INPUT's timeout in ms
constexpr int patchDelay = 100; // The patch view's timer in ms (FillPatchView)
constexpr qsizetype shortHashLength = 8; // CGit::GetShortHASHLength
constexpr int hintMargin = 8;

/**
 * @returns Where EnableSaveRestore(L"FileDiffDlg") keeps the window size.
 */
KConfigGroup windowSettings()
{
    return AppUtils::resizableState(QStringLiteral("FileDiffDlg"));
}

/**
 * Takes a finished call out of its owner. It is deleted from the event loop, since it may still be emitting.
 * @param[in,out] call The owner; left empty.
 * @returns The call, valid until control returns to the event loop.
 */
GitCall *takeFinished(std::unique_ptr<GitCall>& call)
{
    GitCall *finished = call.release();
    Q_ASSERT(finished);
    finished->deleteLater();
    return finished;
}

/**
 * @param hash A full hash; empty for the working tree.
 * @returns It as CGitHash::ToString writes it: forty zeros for the working tree.
 */
QString hashText(const QString& hash)
{
    return hash.isEmpty() ? FileDiffDlg::workingCopyRef() : hash;
}

/**
 * @param options The Diff Options.
 * @returns The diff options for the whitespace choices, as CGit::GetCommitDiffList and FillPatchView add them.
 */
QStringList ignoreArguments(const FileDiffOptions& options)
{
    QStringList arguments;
    if (options.ignoreSpaceAtEol) {
        arguments.append(QStringLiteral("--ignore-space-at-eol"));
    }
    if (options.ignoreSpaceChange) {
        arguments.append(QStringLiteral("--ignore-space-change"));
    }
    if (options.ignoreAllSpace) {
        arguments.append(QStringLiteral("--ignore-all-space"));
    }
    if (options.ignoreBlankLines) {
        arguments.append(QStringLiteral("--ignore-blank-lines"));
    }
    return arguments;
}

/**
 * @param options The Diff Options.
 * @returns Whether any is chosen, which shows the button pressed.
 */
bool anyOption(const FileDiffOptions& options)
{
    return options != FileDiffOptions{};
}

/**
 * Parses what readRev's git log printed.
 * @param output %H, %s, %aN, %at and %ct, separated by NULs.
 * @returns The commit, or a Parse error.
 */
Result<FileDiffRev> parseRev(const QByteArray& output)
{
    const QList<QByteArray> fields = output.trimmed().split('\0');
    constexpr qsizetype fieldCount = 5;
    if (fields.size() != fieldCount) {
        const QString message = QStringLiteral("Unexpected output from git log: %1").arg(QString::fromUtf8(output));
        return makeError(ErrorCategory::Parse, message);
    }
    return FileDiffRev{
        .hash = QString::fromLatin1(fields.at(0)),
        .subject = QString::fromUtf8(fields.at(1)),
        .authorName = QString::fromUtf8(fields.at(2)),
        .authorDate = QDateTime::fromSecsSinceEpoch(fields.at(3).toLongLong()),
        .committerDate = QDateTime::fromSecsSinceEpoch(fields.at(4).toLongLong()),
        .isCommit = true,
    };
}

} // namespace

namespace kgt
{

FileDiffDlg::FileDiffDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
    , m_workingTree(workingTree)
    , m_model(new FileDiffListModel(this))
    , m_isBare(GitAdminDir::isBareRepo(workingTree))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Changed Files", QDir::toNativeSeparators(workingTree)));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    setWindowFlag(Qt::WindowMinimizeButtonHint);

    createWidgets();

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(filterDelay);
    connect(&m_filterTimer, &QTimer::timeout, this, [this] {
        // OnTimer(IDT_FILTER). A list still loading takes the filter when it is done
        if (!isLoading()) {
            filter();
        }
    });
    m_inputTimer.setSingleShot(true);
    m_inputTimer.setInterval(inputDelay);
    connect(&m_inputTimer, &QTimer::timeout, this, &FileDiffDlg::onInputTimer);
    m_patchTimer.setSingleShot(true);
    m_patchTimer.setInterval(patchDelay);
    connect(&m_patchTimer, &QTimer::timeout, this, [this] {
        fillPatchView();
    });

    // PreTranslateMessage's VK_F5
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &FileDiffDlg::onInputTimer);

    // EnableSaveRestore. KWindowConfig needs the native window, which show() would otherwise create
    resize(initialWidth, initialHeight);
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());

    loadRefs();
    if (Git::isConfigTrue(m_git.configValue(QStringLiteral("tgit.diffshowpatch")))) {
        togglePatchView();
    }
}

FileDiffDlg::~FileDiffDlg() = default;

QString FileDiffDlg::workingCopyRef()
{
    return QString(40, QLatin1Char('0'));
}

bool FileDiffDlg::isWorkingCopyRef(const QString& text)
{
    constexpr qsizetype minLength = 8;
    return text.size() >= minLength && workingCopyRef().startsWith(text);
}

QStringList FileDiffDlg::diffListArguments(const FileDiffQuery& query)
{
    // CGit::GetCommitDiffList: a diff against the working tree is turned round when the base is the working tree
    const bool withWorkingTree = query.rev1.isEmpty() || query.rev2.isEmpty();
    const QString command = withWorkingTree ? QStringLiteral("diff") : QStringLiteral("diff-tree");
    QStringList arguments{command, QStringLiteral("-r")};
    if (query.rev1.isEmpty()) {
        arguments.append(QStringLiteral("-R"));
    }
    // TortoiseGit's default similarity index (ms_iSimilarityIndexThreshold, 50%) for renames and copies
    arguments << QStringLiteral("--raw") << QStringLiteral("-C50%") << QStringLiteral("-M50%");
    arguments << QStringLiteral("--numstat") << QStringLiteral("-z");
    arguments << ignoreArguments(query.options) << QStringLiteral("--end-of-options");
    for (const QString& rev : {query.rev1, query.rev2}) {
        if (!rev.isEmpty()) {
            arguments.append(rev);
        }
    }
    arguments.append(QStringLiteral("--"));
    return arguments;
}

QStringList FileDiffDlg::diffArguments(const FileDiffEntry& file, const FileDiffQuery& query, bool unborn)
{
    const QString& path = file.path;
    const QString oldPath = file.oldPath.isEmpty() ? path : file.oldPath;
    const QString& rev1 = query.rev1;
    const QString& rev2 = query.rev2;
    if (rev2.isEmpty() && (unborn || file.actions.testFlag(TGitPath::Action::Added))) {
        return GitDiff::diffNullArguments(path);
    }
    if (rev2.isEmpty()) {
        return GitDiff::diffArguments(path, oldPath, {}, rev1);
    }
    if (rev1.isEmpty()) {
        if (oldPath != path) {
            return GitDiff::diffArguments(oldPath, path, {}, rev2);
        }
        // The working tree is the base: git difftool -R puts it on the left
        QStringList arguments = GitDiff::diffArguments(path, path, {}, rev2);
        arguments.insert(arguments.indexOf(QStringLiteral("--no-prompt")) + 1, QStringLiteral("-R"));
        return arguments;
    }
    return GitDiff::diffArguments(path, oldPath, rev2, rev1);
}

QStringList FileDiffDlg::patchArguments(const FileDiffQuery& query, const QList<FileDiffEntry>& selected)
{
    QStringList arguments = QStringList{QStringLiteral("diff")} + ignoreArguments(query.options);
    if (selected.isEmpty()) {
        arguments << QStringLiteral("--stat");
    }
    if (query.rev2.isEmpty()) {
        arguments << QStringLiteral("-p") << query.rev1;
    } else if (query.rev1.isEmpty()) {
        arguments << QStringLiteral("-R") << QStringLiteral("-p") << query.rev2;
    } else {
        arguments << QStringLiteral("-p") << query.rev1 + QStringLiteral("..") + query.rev2;
    }
    arguments.append(QStringLiteral("--"));
    // TortoiseGit runs one diff per file; one git gives the same diffs
    for (const FileDiffEntry& entry : selected) {
        if (!entry.oldPath.isEmpty()) {
            arguments.append(entry.oldPath);
        }
        arguments.append(entry.path);
    }
    return arguments;
}

void FileDiffDlg::setDiff(const QString& path, const QString& baseRev1, const QString& rev2)
{
    // SetDiff and the rest of OnInitDialog
    if (!path.isEmpty()) {
        m_filter->setText(path);
    }
    m_sides.at(0).edit->setText(baseRev1);
    m_sides.at(1).edit->setText(rev2);
    bool gotError = !fillRevFromString(m_sides.at(0).rev, baseRev1);
    if (!gotError) {
        gotError = !fillRevFromString(m_sides.at(1).rev, rev2);
    }
    setUrlLabels();
    if (!gotError) {
        startDiff();
    }
    // Start with focus on file list
    m_fileList->setFocus();
    if (m_sides.at(1).rev.hash.isEmpty()) {
        m_switch->setEnabled(false);
    }
    m_filterTimer.stop();
}

const FileDiffRev& FileDiffDlg::rev1() const
{
    return m_sides.at(0).rev;
}

const FileDiffRev& FileDiffDlg::rev2() const
{
    return m_sides.at(1).rev;
}

bool FileDiffDlg::isLoading() const
{
    return m_diffCall != nullptr;
}

const QString& FileDiffDlg::hintText() const
{
    return m_hint;
}

const QList<FileDiffEntry>& FileDiffDlg::allEntries() const
{
    return m_entries;
}

QTreeView *FileDiffDlg::fileList() const
{
    return m_fileList;
}

FileDiffListModel *FileDiffDlg::listModel() const
{
    return m_model;
}

QLineEdit *FileDiffDlg::revEdit(int index) const
{
    return m_sides.at(index).edit;
}

QLineEdit *FileDiffDlg::filterEdit() const
{
    return m_filter;
}

QToolButton *FileDiffDlg::switchButton() const
{
    return m_switch;
}

QMenu *FileDiffDlg::diffOptionsMenu() const
{
    return m_diffOptionsMenu;
}

QPlainTextEdit *FileDiffDlg::patchView() const
{
    return m_patchView;
}

void FileDiffDlg::switchLeftRight()
{
    // OnBnClickedSwitchleftright
    if (isLoading()) {
        return;
    }
    std::swap(m_sides.at(0).rev, m_sides.at(1).rev);
    const QString text1 = m_sides.at(0).edit->text();
    m_sides.at(0).edit->setText(m_sides.at(1).edit->text());
    m_sides.at(1).edit->setText(text1);
    setUrlLabels();
    // Setting the edit boxes' text starts TortoiseGit's input timer, which reads both versions and reloads
    onInputTimer();
}

void FileDiffDlg::done(int result)
{
    // OnCancel waits for the threads; here closing just stops git
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

bool FileDiffDlg::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_fileList->viewport() && event->type() == QEvent::Paint && !m_hint.isEmpty()) {
        // CHintCtrl draws its text in place of the items
        QPainter painter(m_fileList->viewport());
        painter.setPen(palette().color(QPalette::PlaceholderText));
        const QRect rect = m_fileList->viewport()->rect().adjusted(hintMargin, hintMargin, -hintMargin, -hintMargin);
        painter.drawText(rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_hint);
        return true;
    }
    if (event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }
    const auto *keyEvent = dynamic_cast<const QKeyEvent *>(event);
    if (!keyEvent) {
        return QDialog::eventFilter(watched, event);
    }
    const bool isReturn = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
    if (watched == m_fileList && isReturn) {
        // Return pressed in file list. Show diff, as for double click
        const QModelIndex current = m_fileList->currentIndex();
        if (current.isValid() && m_hint.isEmpty()) {
            doDiff(m_model->entries().at(current.row()));
        }
        return true;
    }
    const bool isInsert = keyEvent->key() == Qt::Key_Insert && keyEvent->modifiers().testFlag(Qt::ControlModifier);
    const bool isCopy = keyEvent->matches(QKeySequence::Copy) || isInsert;
    if (watched == m_fileList && isCopy) {
        copySelectionToClipboard(false);
        return true;
    }
    if (watched == m_filter && keyEvent->key() == Qt::Key_Escape && !m_filter->text().isEmpty()) {
        m_filter->clear();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void FileDiffDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(createTopRow());
    createRevSide(0, i18n("Version 1 (Base)"));
    createRevSide(1, i18n("Version 2"));
    for (const RevSide& side : m_sides) {
        layout->addWidget(side.group);
    }

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(i18n("Filter paths"));
    m_filter->setClearButtonEnabled(true);
    m_filter->installEventFilter(this);
    layout->addWidget(m_filter);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        // The cancel button filters at once (OnClickedCancelFilter); typing waits (OnEnChangeFilter)
        if (text.isEmpty() && !isLoading()) {
            m_filterTimer.stop();
            filter();
            return;
        }
        m_filterTimer.start();
    });

    // TortoiseGit's patch view is a window docked beside the dialog; here it is a pane beside the list
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_fileList = new QTreeView(m_splitter);
    m_fileList->setModel(m_model);
    m_fileList->setRootIsDecorated(false);
    m_fileList->setUniformRowHeights(true);
    m_fileList->setAllColumnsShowFocus(true);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->header()->setSectionsClickable(true);
    m_fileList->header()->setSortIndicatorShown(true);
    m_fileList->header()->setSortIndicator(-1, Qt::AscendingOrder);
    m_fileList->installEventFilter(this);
    m_fileList->viewport()->installEventFilter(this);
    m_patchView = new QPlainTextEdit(m_splitter);
    m_patchView->setReadOnly(true);
    m_patchView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_patchView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_patchView->hide();
    layout->addWidget(m_splitter, 1);

    connect(m_fileList->header(), &QHeaderView::sectionClicked, this, &FileDiffDlg::onHeaderClicked);
    connect(m_fileList, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        // OnNMDblclkFilelist
        if (index.isValid() && m_hint.isEmpty()) {
            doDiff(m_model->entries().at(index.row()));
        }
    });
    connect(m_fileList, &QWidget::customContextMenuRequested, this, &FileDiffDlg::showContextMenu);
    connect(m_fileList->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        // OnFileListItemChanged
        fillPatchView(true);
    });

    m_showPatch = new QLabel(this);
    m_showPatch->setTextFormat(Qt::RichText);
    m_showPatch->setAlignment(Qt::AlignRight);
    connect(m_showPatch, &QLabel::linkActivated, this, &FileDiffDlg::togglePatchView);
    layout->addWidget(m_showPatch);
    m_showPatch->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(i18n("View Patch>>").toHtmlEscaped()));
}

QLayout *FileDiffDlg::createTopRow()
{
    auto *row = new QHBoxLayout;
    m_differenceLabel = new QLabel(i18n("Difference between"), this);
    row->addWidget(m_differenceLabel, 1);

    m_diffOptions = new QPushButton(i18n("Diff Options"), this);
    m_diffOptions->setAutoDefault(false);
    m_diffOptions->setCheckable(true);
    createDiffOptionsMenu();
    m_diffOptions->setMenu(m_diffOptionsMenu);
    row->addWidget(m_diffOptions);

    m_log = new QPushButton(i18n("Show log"), this);
    m_log->setAutoDefault(false);
    connect(m_log, &QPushButton::clicked, this, &FileDiffDlg::onShowLog);
    row->addWidget(m_log);

    m_switch = new QToolButton(this);
    m_switch->setIcon(QIcon::fromTheme(QStringLiteral("document-swap")));
    m_switch->setToolTip(i18n("Switches the comparison left<->right"));
    connect(m_switch, &QToolButton::clicked, this, &FileDiffDlg::switchLeftRight);
    row->addWidget(m_switch);
    return row;
}

void FileDiffDlg::createRevSide(int index, const QString& title)
{
    RevSide& side = m_sides.at(index);
    side.group = new QGroupBox(title, this);
    auto *groupLayout = new QVBoxLayout(side.group);
    auto *editRow = new QHBoxLayout;
    side.edit = new QLineEdit(side.group);
    editRow->addWidget(side.edit, 1);
    connect(side.edit, &QLineEdit::textEdited, this, [this, index] {
        onRevEdited(index);
    });
    connect(side.edit, &QLineEdit::returnPressed, this, &FileDiffDlg::onInputTimer);

    // A CMenuButton: the button runs its current entry, and the menu picks another and runs it
    side.button = new QToolButton(side.group);
    side.button->setPopupMode(QToolButton::MenuButtonPopup);
    side.button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *menu = new QMenu(side.button);
    const QStringList entries{i18n("Reference Browser"), i18n("Log"), i18n("RefLog")};
    for (int entry = 0; entry < entries.size(); ++entry) {
        connect(menu->addAction(entries.at(entry)), &QAction::triggered, this, [this, index, entry, entries] {
            m_sides.at(index).buttonEntry = entry;
            m_sides.at(index).button->setText(entries.at(entry));
            clickRevButton(index);
        });
    }
    side.button->setMenu(menu);
    side.button->setText(entries.constFirst());
    connect(side.button, &QToolButton::clicked, this, [this, index] {
        clickRevButton(index);
    });
    editRow->addWidget(side.button);
    groupLayout->addLayout(editRow);

    side.label = new QLineEdit(side.group);
    side.label->setReadOnly(true);
    side.label->setFrame(false);
    side.label->setFocusPolicy(Qt::NoFocus);
    // A borderless read-only edit on the dialog's background, as IDC_FIRSTURL and IDC_SECONDURL are
    QPalette labelPalette = side.label->palette();
    labelPalette.setColor(QPalette::Base, labelPalette.color(QPalette::Window));
    side.label->setPalette(labelPalette);
    groupLayout->addWidget(side.label);
}

void FileDiffDlg::createDiffOptionsMenu()
{
    m_diffOptionsMenu = new QMenu(this);
    const std::array<QString, 5> texts{
        i18n("Ignore space at EOL"),
        i18n("Ignore space change"),
        i18n("Ignore all space"),
        i18n("Ignore blank lines"),
        i18n("Use common ancestor (A...B diff)"),
    };
    for (std::size_t i = 0; i < texts.size(); ++i) {
        // DIFFOPTION_COMMONANCESTOR follows a separator
        if (i == texts.size() - 1) {
            m_diffOptionsMenu->addSeparator();
        }
        QAction *action = m_diffOptionsMenu->addAction(texts.at(i));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, &FileDiffDlg::onDiffOptionTriggered);
        m_diffOptionActions.at(i) = action;
    }
    // Opening the menu doesn't press the button: it shows whether any option is on
    connect(m_diffOptionsMenu, &QMenu::aboutToHide, this, [this] {
        m_diffOptions->setChecked(anyOption(m_options));
    });
}

Result<FileDiffRev> FileDiffDlg::readRev(const QString& text) const
{
    // GitRev::GetCommit(text, allowTree = true), with the mailmap applied (ApplyMailmap)
    if (isWorkingCopyRef(text)) {
        return FileDiffRev{
            .hash = {},
            .subject = i18n("Working Tree"),
            .authorName = {},
            .authorDate = {},
            .committerDate = {},
            .isCommit = true,
        };
    }
    // Dereferences signed tags
    const QString name = m_git.fixBranchName(text);
    try {
        const QStringList arguments{
            QStringLiteral("log"),
            QStringLiteral("-1"),
            QStringLiteral("--no-walk"),
            QStringLiteral("--no-show-signature"),
            QStringLiteral("--encoding=UTF-8"),
            QStringLiteral("--format=%H%x00%s%x00%aN%x00%at%x00%ct"),
            QStringLiteral("--end-of-options"),
            name + QStringLiteral("^{commit}"),
            QStringLiteral("--"),
        };
        return parseRev(m_git.run(arguments, RunMode::ReadOnly));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    try {
        const QStringList arguments{
            QStringLiteral("rev-parse"),
            QStringLiteral("--verify"),
            QStringLiteral("--end-of-options"),
            name + QStringLiteral("^{tree}"),
        };
        const QString hash = QString::fromLatin1(m_git.run(arguments, RunMode::ReadOnly).trimmed());
        return FileDiffRev{
            .hash = hash,
            .subject = {},
            .authorName = {},
            .authorDate = {},
            .committerDate = {},
            .isCommit = false,
        };
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return makeError(ErrorCategory::Git, QStringLiteral("Ref \"%1\" is neither a commit nor a tree.").arg(name));
    }
}

bool FileDiffDlg::fillRevFromString(FileDiffRev& rev, const QString& text)
{
    // FillRevFromString
    Result<FileDiffRev> read = readRev(text);
    if (!read) {
        m_diffCall.reset();
        showHint(i18n("\"%1\" is invalid.", text) + QLatin1Char('\n') + read.error().message);
        m_model->setEntries({});
        return false;
    }
    rev = std::move(*read);
    return true;
}

void FileDiffDlg::startDiff()
{
    // DiffThread. A newer request replaces one still running, where TortoiseGit waits for it
    m_diffCall.reset();
    // OnDisableButtons
    m_fileList->setCursor(Qt::BusyCursor);
    showHint(i18n("Please wait while differences are obtained..."));
    m_model->setEntries({});
    m_entries.clear();
    enableInputControl(false);

    const FileDiffRev& rev1 = m_sides.at(0).rev;
    const FileDiffRev& rev2 = m_sides.at(1).rev;
    QString base = rev1.hash;
    /**
     * TortoiseGit asks CGit::IsFastForward for the common ancestor but only uses it when rev1 already is that
     * ancestor, which makes the option do nothing; the merge base is what "A...B" means.
     */
    if (m_options.commonAncestor && isBothCommits()) {
        if (const QString ancestor = m_git.commonAncestor(rev1.hash, rev2.hash); !ancestor.isEmpty()) {
            base = ancestor;
        }
    }
    // git diff refreshes the index's stat information itself, which CGit::RefreshGitIndex does first
    const FileDiffQuery query{.rev1 = base, .rev2 = rev2.hash, .options = m_options};
    m_diffCall = m_git.runAsync(diffListArguments(query), RunMode::ReadOnly);
    connect(m_diffCall.get(), &GitCall::finished, this, &FileDiffDlg::onDiffFinished);
}

void FileDiffDlg::onDiffFinished()
{
    const GitCall *call = takeFinished(m_diffCall);
    QString error;
    QList<FileDiffEntry> entries;
    try {
        Result<GitRevFiles> files = GitRevLoglist::parseFiles(call->output());
        if (files) {
            entries = FileDiffListModel::makeEntries(*files);
        } else {
            error = files.error().message;
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        error = e.error().message;
    }
    // CTGitPath::IsDirectory: git's raw output has the mode, but the parsed entries don't; the working tree tells,
    // as it does for the status list
    for (FileDiffEntry& entry : entries) {
        entry.isDirectory = !m_isBare && QFileInfo(absolutePath(entry.path)).isDir();
    }
    FileDiffListModel::sort(entries, m_sortedColumn, m_ascending);
    m_entries = entries;

    // OnDiffFinished
    showHint({});
    filter();
    for (int column = 0; column < FileDiffListModel::ColumnCount; ++column) {
        m_fileList->resizeColumnToContents(column);
    }
    if (m_entries.isEmpty()) {
        showHint(i18n("No differences found!"));
    }
    if (!error.isEmpty()) {
        showHint(error);
    }
    m_fileList->unsetCursor();
    enableInputControl(true);
    fillPatchView(true);
    m_fileList->setFocus();
    Q_EMIT diffFinished();
}

void FileDiffDlg::showHint(const QString& text)
{
    m_hint = text;
    m_fileList->viewport()->update();
}

void FileDiffDlg::setUrlLabels(unsigned int mask)
{
    // SetURLLabels
    for (int index = 0; index < 2; ++index) {
        if (!(mask & (1U << static_cast<unsigned int>(index)))) {
            continue;
        }
        const RevSide& side = m_sides.at(index);
        side.label->setText(hashText(side.rev.hash).left(shortHashLength) + QStringLiteral(": ") + side.rev.subject);
        side.label->setCursorPosition(0);
        if (!side.rev.hash.isEmpty() && side.rev.isCommit) {
            const QString date = QLocale().toString(side.rev.authorDate, QLocale::ShortFormat);
            side.label->setToolTip(date + QStringLiteral("  ") + side.rev.authorName);
        }
    }
    m_sides.at(0).group->setTitle(i18n("Version 1 (Base)"));
    m_sides.at(1).group->setTitle(i18n("Version 2"));
    if ((mask & bothSides) != bothSides || !isBothCommits()) {
        return;
    }
    const FileDiffRev& rev1 = m_sides.at(0).rev;
    const FileDiffRev& rev2 = m_sides.at(1).rev;
    if (rev1.committerDate > rev2.committerDate) {
        m_sides.at(0).group->setTitle(i18n("Version 1 (Base, Newer commit time)"));
    } else if (rev1.committerDate < rev2.committerDate) {
        m_sides.at(1).group->setTitle(i18n("Version 2 (Newer commit time)"));
    }
}

void FileDiffDlg::clearUrlLabels(unsigned int mask)
{
    // ClearURLabels
    fillPatchView(true);
    for (int index = 0; index < 2; ++index) {
        if (mask & (1U << static_cast<unsigned int>(index))) {
            m_sides.at(index).label->clear();
            m_sides.at(index).label->setToolTip({});
        }
    }
}

void FileDiffDlg::filter()
{
    // Filter
    m_model->setEntries(FileDiffListModel::filtered(m_entries, m_filter->text()));
    fillPatchView(true);
}

void FileDiffDlg::enableInputControl(bool enable)
{
    // EnableInputControl
    for (const RevSide& side : m_sides) {
        side.edit->setEnabled(enable);
        side.button->setEnabled(enable);
    }
    m_filter->setEnabled(enable);
    m_switch->setEnabled(enable);
    m_log->setEnabled(enable && !m_sides.at(0).rev.hash.isEmpty() && !m_sides.at(1).rev.hash.isEmpty());
}

void FileDiffDlg::onInputTimer()
{
    // OnTimer(IDT_INPUT): reads both versions and reloads if both are valid
    m_inputTimer.stop();
    unsigned int mask = 0;
    if (fillRevFromString(m_sides.at(0).rev, m_sides.at(0).edit->text())) {
        mask |= 0x1U;
    }
    if (mask && fillRevFromString(m_sides.at(1).rev, m_sides.at(1).edit->text())) {
        mask |= 0x2U;
    }
    setUrlLabels(mask);
    if (mask == bothSides) {
        startDiff();
    }
}

void FileDiffDlg::onRevEdited(int index)
{
    // OnEnUpdate and OnTextUpdate
    clearUrlLabels(1U << static_cast<unsigned int>(index));
    m_inputTimer.start();
    showHint(i18n("Wait For input validate version"));
}

void FileDiffDlg::clickRevButton(int index)
{
    // ClickRevButton
    RevSide& side = m_sides.at(index);
    QString chosen;
    switch (side.buttonEntry) {
    case 0: // Browse Reference
        chosen = BrowseRefsDlg::pickRef(m_workingTree, {}, this);
        break;
    case 1: { // Log
        const QString revision = isWorkingCopyRef(side.edit->text()) ? QString() : side.edit->text();
        LogDlg dlg({m_workingTree}, revision, this);
        if (!revision.isEmpty()) {
            dlg.setHighlightRevision(revision);
        }
        dlg.setSelect(true);
        if (dlg.exec() == QDialog::Accepted) {
            chosen = dlg.selectedHash();
        }
        // Cf. TortoiseGit issue #3493
        raise();
        activateWindow();
        break;
    }
    default: { // RefLog
        RefLogDlg dlg(m_workingTree, this);
        dlg.refresh();
        if (dlg.exec() == QDialog::Accepted) {
            chosen = dlg.selectedHash();
        }
        break;
    }
    }
    if (chosen.isEmpty() || !fillRevFromString(side.rev, chosen)) {
        return;
    }
    side.edit->setText(chosen);
    setUrlLabels();
    startDiff();
    m_inputTimer.stop();
}

void FileDiffDlg::onHeaderClicked(int section)
{
    // OnHdnItemclickFilelist
    if (isLoading()) {
        m_fileList->header()->setSortIndicator(m_sortedColumn, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        return;
    }
    m_ascending = m_sortedColumn == section ? !m_ascending : true;
    m_sortedColumn = section;
    FileDiffListModel::sort(m_entries, m_sortedColumn, m_ascending);
    filter();
    m_fileList->header()->setSortIndicator(m_sortedColumn, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void FileDiffDlg::onDiffOptionTriggered()
{
    // OnBnClickedDiffoption
    m_options = FileDiffOptions{
        .ignoreSpaceAtEol = m_diffOptionActions.at(0)->isChecked(),
        .ignoreSpaceChange = m_diffOptionActions.at(1)->isChecked(),
        .ignoreAllSpace = m_diffOptionActions.at(2)->isChecked(),
        .ignoreBlankLines = m_diffOptionActions.at(3)->isChecked(),
        .commonAncestor = m_diffOptionActions.at(4)->isChecked(),
    };
    m_diffOptions->setChecked(anyOption(m_options));
    onInputTimer();
}

void FileDiffDlg::onShowLog()
{
    // OnBnClickedLog
    const QString range = m_sides.at(0).rev.hash + QStringLiteral("..") + m_sides.at(1).rev.hash;
    const QStringList arguments{
        QStringLiteral("--command"),
        QStringLiteral("log"),
        QStringLiteral("--path"),
        m_workingTree,
        QStringLiteral("--range"),
        range,
    };
    std::ignore = AppUtils::runTortoiseGitProc(arguments);
}

void FileDiffDlg::loadRefs()
{
    // LoadRefThread, with two git calls in place of the thread
    m_branchCall = m_git.runAsync(Git::branchListArguments(BranchAllF), RunMode::ReadOnly);
    m_tagCall = m_git.runAsync({QStringLiteral("tag"), QStringLiteral("-l")}, RunMode::ReadOnly);
    connect(m_branchCall.get(), &GitCall::finished, this, &FileDiffDlg::onRefsLoaded);
    connect(m_tagCall.get(), &GitCall::finished, this, &FileDiffDlg::onRefsLoaded);
}

void FileDiffDlg::onRefsLoaded()
{
    // OnRefLoad: the branches and tags complete what is typed into either version
    if (!m_branchCall || !m_tagCall || !m_branchCall->isFinished() || !m_tagCall->isFinished()) {
        return;
    }
    const GitCall *branchCall = takeFinished(m_branchCall);
    const GitCall *tagCall = takeFinished(m_tagCall);
    const bool hasFetchHead = !m_git.derefFetchHead().isEmpty();
    const QByteArray branchOutput = AppUtils::outputOrEmpty(*branchCall);
    QStringList refs = Git::parseBranchList(branchOutput, BranchAllF, false, hasFetchHead).branches;
    refs.append(Git::parseTagList(AppUtils::outputOrEmpty(*tagCall)));
    const QString remotesPrefix = QStringLiteral("remotes/");
    for (QString& ref : refs) {
        if (ref.startsWith(remotesPrefix)) {
            ref.remove(0, remotesPrefix.size());
        }
    }
    for (const RevSide& side : m_sides) {
        auto *completer = new QCompleter(refs, side.edit);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        side.edit->setCompleter(completer);
    }
}

void FileDiffDlg::togglePatchView()
{
    // OnStnClickedViewPatch
    if (m_patchView->isHidden()) {
        if (!Git::isConfigTrue(m_git.configValue(QStringLiteral("tgit.diffshowpatch")))) {
            m_git.setConfigValue(QStringLiteral("tgit.diffshowpatch"), QStringLiteral("true"));
        }
        m_patchView->show();
        fillPatchView();
        m_showPatch->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(i18n("Hide Patch<<").toHtmlEscaped()));
    } else {
        m_git.setConfigValue(QStringLiteral("tgit.diffshowpatch"), QStringLiteral("false"));
        m_patchView->hide();
        m_patchCall.reset();
        m_patchView->clear();
        m_showPatch->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(i18n("View Patch>>").toHtmlEscaped()));
    }
}

void FileDiffDlg::fillPatchView(bool onlySetTimer)
{
    // FillPatchView
    if (m_patchView->isHidden()) {
        return;
    }
    m_patchTimer.stop();
    if (onlySetTimer) {
        m_patchTimer.start();
        return;
    }
    m_patchCall.reset();
    if (!m_hint.isEmpty() || m_model->rowCount() == 0) {
        m_patchView->clear();
        return;
    }
    const FileDiffQuery query{.rev1 = m_sides.at(0).rev.hash, .rev2 = m_sides.at(1).rev.hash, .options = m_options};
    m_patchCall = m_git.runAsync(patchArguments(query, selectedEntries()), RunMode::ReadOnly);
    connect(m_patchCall.get(), &GitCall::finished, this, &FileDiffDlg::onPatchFinished);
}

void FileDiffDlg::onPatchFinished()
{
    const GitCall *call = takeFinished(m_patchCall);
    try {
        m_patchView->setPlainText(QString::fromUtf8(call->output()));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        m_patchView->setPlainText(e.error().message);
    }
}

bool FileDiffDlg::isBothCommits() const
{
    const FileDiffRev& rev1 = m_sides.at(0).rev;
    const FileDiffRev& rev2 = m_sides.at(1).rev;
    return !rev1.hash.isEmpty() && !rev2.hash.isEmpty() && rev1.isCommit && rev2.isCommit;
}

} // namespace kgt
