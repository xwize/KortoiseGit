// SPDX-FileCopyrightText: 2008-2017, 2019-2021, 2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CloneDlg.cpp and its IDD_CLONE resource. The URL and option history
 * kept in the registry are left out for now.
 */

#include "clonedlg.hpp"

#include "apputils.hpp"
#include "git/git.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>


namespace
{

using namespace kgt;

// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();
constexpr int maxDepth = 0x7fffffff;

/**
 * @param text Text that may be quoted.
 * @returns It without one pair of surrounding double or single quotes.
 */
QString unquoted(const QString& text)
{
    if (text.size() > 2
        && ((text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
            || (text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\''))))) {
        return text.mid(1, text.size() - 2);
    }
    return text;
}

} // namespace

namespace kgt
{

CloneDlg::CloneDlg(const QString& directory, QWidget *parent)
    : QDialog(parent)
    , m_url(new QComboBox(this))
    , m_directory(new QLineEdit(directory, this))
    , m_depth(new QCheckBox(i18n("Depth"), this))
    , m_depthValue(new QSpinBox(this))
    , m_recursive(new QCheckBox(i18n("Recursive"), this))
    , m_bare(new QCheckBox(i18n("Clone into Bare Repo"), this))
    , m_noCheckout(new QCheckBox(i18n("No Checkout"), this))
    , m_branch(new QCheckBox(i18n("Branch"), this))
    , m_branchName(new QLineEdit(this))
    , m_origin(new QCheckBox(i18n("Origin Name"), this))
    , m_originName(new QLineEdit(this))
{
    setWindowTitle(i18nc("@title:window", "Git clone - KortoiseGit"));
    // Names for tests and accessibility tools
    m_url->setObjectName(QStringLiteral("url"));
    m_directory->setObjectName(QStringLiteral("directory"));
    m_depthValue->setObjectName(QStringLiteral("depthValue"));
    m_branchName->setObjectName(QStringLiteral("branchName"));
    m_originName->setObjectName(QStringLiteral("originName"));

    auto *layout = new QVBoxLayout(this);
    auto *group = new QGroupBox(i18n("Clone Existing Repository"), this);
    auto *grid = new QGridLayout(group);

    m_url->setEditable(true);
    m_url->setInsertPolicy(QComboBox::NoInsert);
    m_url->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *urlLabel = new QLabel(i18n("&URL:"), group);
    urlLabel->setBuddy(m_url);
    auto *browseUrl = new QPushButton(i18n("&Browse..."), group);
    grid->addWidget(urlLabel, 0, 0);
    grid->addWidget(m_url, 0, 1);
    grid->addWidget(browseUrl, 0, 2);

    auto *directoryLabel = new QLabel(i18n("&Directory:"), group);
    directoryLabel->setBuddy(m_directory);
    auto *browseDirectory = new QPushButton(i18n("Bro&wse..."), group);
    // TortoiseGit's SHAutoComplete(SHACF_FILESYSTEM)
    auto *folders = new QFileSystemModel(this);
    folders->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    folders->setRootPath(QDir::rootPath());
    m_directory->setCompleter(new QCompleter(folders, this));
    grid->addWidget(directoryLabel, 1, 0);
    grid->addWidget(m_directory, 1, 1);
    grid->addWidget(browseDirectory, 1, 2);

    const QString depthToolTip = i18n(
        "Create a shallow clone with a history truncated to the specified number of revisions. A shallow repository has a number of limitations (you "
        "cannot clone or fetch from it, nor push from nor into it), but is adequate if you are only interested in the recent history of a large project "
        "with a long history, and would want to send in fixes as patches.");
    m_depth->setToolTip(depthToolTip);
    m_depthValue->setToolTip(depthToolTip);
    m_depthValue->setRange(1, maxDepth);
    m_depthValue->setValue(1);
    m_recursive->setToolTip(i18n("Submodules are initialized and cloned using their default settings."));
    m_bare->setToolTip(i18n("Clone into a git repository without the working tree."));
    m_noCheckout->setToolTip(i18n("No checkout of HEAD is performed after the clone is complete.\nThis makes the working tree empty."));
    auto *optionRow = new QHBoxLayout;
    optionRow->addWidget(m_depth);
    optionRow->addWidget(m_depthValue);
    optionRow->addWidget(m_recursive);
    optionRow->addWidget(m_bare);
    optionRow->addWidget(m_noCheckout);
    optionRow->addStretch();
    grid->addLayout(optionRow, 2, 0, 1, 3);

    const QString branchToolTip =
        i18n("Instead of pointing the newly created HEAD to the branch pointed to by the cloned repository's HEAD, point to specific branch instead.");
    m_branch->setToolTip(branchToolTip);
    m_branchName->setToolTip(branchToolTip);
    const QString originToolTip = i18n("Instead of using the remote name \"origin\" to keep track of the upstream repository, use the specific name.");
    m_origin->setToolTip(originToolTip);
    m_originName->setToolTip(originToolTip);
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(m_branch);
    nameRow->addWidget(m_branchName);
    nameRow->addWidget(m_origin);
    nameRow->addWidget(m_originName);
    nameRow->addStretch();
    grid->addLayout(nameRow, 3, 0, 1, 3);
    layout->addWidget(group);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CloneDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_url, &QComboBox::editTextChanged, this, &CloneDlg::onUrlChanged);
    connect(browseUrl, &QPushButton::clicked, this, &CloneDlg::browseUrl);
    connect(browseDirectory, &QPushButton::clicked, this, &CloneDlg::browseDirectory);
    for (const QCheckBox *box : {m_depth, m_recursive, m_bare, m_noCheckout, m_branch, m_origin}) {
        connect(box, &QCheckBox::toggled, this, &CloneDlg::updateStates);
    }

    // A URL on the clipboard, possibly as a whole "git clone <url>" command, is what the user most likely wants
    setUrl(unquoted(AppUtils::getClipboardLink(QStringLiteral("git clone ")).trimmed()));
    updateStates();
    resize(560, sizeHint().height());
}

CloneOptions CloneDlg::options() const
{
    return {
        .url = m_url->currentText().trimmed(),
        .directory = m_directory->text().trimmed(),
        .depth = m_depth->isChecked(),
        .depthValue = m_depthValue->value(),
        .recursive = m_recursive->isChecked(),
        .bare = m_bare->isChecked(),
        .noCheckout = m_noCheckout->isChecked(),
        .branch = m_branch->isChecked(),
        .branchName = m_branchName->text().trimmed(),
        .origin = m_origin->isChecked(),
        .originName = m_originName->text().trimmed(),
    };
}

void CloneDlg::setUrl(const QString& url)
{
    m_url->setEditText(url);
    // setEditText() only signals a change; make sure the directory follows even for the same text
    onUrlChanged(url);
}

QString CloneDlg::moduleName(const QString& url)
{
    QString path = url.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    qsizetype start = path.lastIndexOf(QLatin1Char('/'));
    if (start < 0) {
        start = path.lastIndexOf(QLatin1Char(':'));
    }
    if (start < 0) {
        start = path.lastIndexOf(QLatin1Char('@'));
    }
    // TortoiseGit starts at 0 here and so drops the first character of a bare name; keep it whole
    QString name = path.mid(start + 1);
    if (name.endsWith(QLatin1String(".git"), Qt::CaseInsensitive)) {
        name.chop(4);
    }
    return name;
}

void CloneDlg::accept()
{
    const CloneOptions chosen = options();
    if (chosen.url.isEmpty() || chosen.directory.isEmpty()) {
        KMessageBox::error(this, i18n("URL and directory must not be empty."), QString(), noNotify);
        return;
    }
    // TortoiseGit shows these as balloons on the field
    QLineEdit *invalid = nullptr;
    if (chosen.branch && !Git::isBranchNameValid(chosen.branchName)) {
        invalid = m_branchName;
    } else if (chosen.origin && !chosen.bare && chosen.originName.isEmpty()) {
        invalid = m_originName;
    }
    if (invalid) {
        invalid->setFocus();
        KMessageBox::error(this, i18n("Branch/Tag name must not be empty or is invalid."), i18n("Error"), noNotify);
        return;
    }
    QDialog::accept();
}

void CloneDlg::onUrlChanged(const QString& text)
{
    const QString url = text.trimmed();
    if (url == m_oldUrl) {
        return;
    }
    m_oldUrl = url;
    const QString oldModule = m_moduleName;
    m_moduleName = moduleName(url);

    QString directory = m_directory->text();
    // Drop the name the previous URL gave the directory, if it is still there
    if (!oldModule.isEmpty() && directory.endsWith(oldModule)) {
        directory.chop(oldModule.size());
    }
    while (directory.endsWith(QLatin1Char('/'))) {
        directory.chop(1);
    }
    m_directory->setText(directory + QLatin1Char('/') + m_moduleName);
}

void CloneDlg::updateStates()
{
    m_depthValue->setEnabled(m_depth->isChecked());
    m_branchName->setEnabled(m_branch->isChecked());
    // A bare repository has no working tree to check out or recurse into, and keeps the remote's own names
    const bool bare = m_bare->isChecked();
    m_recursive->setEnabled(!bare);
    m_noCheckout->setEnabled(!bare);
    m_origin->setEnabled(!bare);
    m_originName->setEnabled(!bare && m_origin->isChecked());
    m_bare->setEnabled(!m_recursive->isChecked() && !m_noCheckout->isChecked() && !m_origin->isChecked());
}

void CloneDlg::browseUrl()
{
    const QString folder = QFileDialog::getExistingDirectory(this, QString(), m_url->currentText().trimmed());
    if (!folder.isEmpty()) {
        setUrl(folder);
    }
}

void CloneDlg::browseDirectory()
{
    const QString folder = QFileDialog::getExistingDirectory(this, QString(), m_directory->text());
    if (!folder.isEmpty()) {
        m_directory->setText(folder);
    }
}

} // namespace kgt
