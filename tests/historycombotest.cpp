// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "historycombo.hpp"

#include <QComboBox>
#include <QTest>

using kgt::HistoryCombo;

/**
 * HistoryCombo::addString, in the two ways the dialogs call it.
 */
class HistoryComboTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_combo.clear();
        m_combo.addItems({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")});
        m_combo.setCurrentIndex(-1);
    }

    void emptyTextDoesNothing()
    {
        HistoryCombo::addString(&m_combo, QString());
        QCOMPARE(m_combo.count(), 3);
        QCOMPARE(m_combo.currentIndex(), -1);
    }

    void appendsAtTheEndByDefault()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("four"));
        QCOMPARE(m_combo.count(), 4);
        QCOMPARE(m_combo.itemText(3), QStringLiteral("four"));
        QCOMPARE(m_combo.currentIndex(), 3);
    }

    void defaultSelectsAnExistingEntryWhereItIs()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("two"));
        QCOMPARE(m_combo.count(), 3);
        QCOMPARE(m_combo.itemText(1), QStringLiteral("two"));
        QCOMPARE(m_combo.currentIndex(), 1);
    }

    void matchIsCaseSensitive()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("TWO"));
        QCOMPARE(m_combo.count(), 4);
    }

    void insertsAtTheGivenPosition()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("zero"), 0);
        QCOMPARE(m_combo.count(), 4);
        QCOMPARE(m_combo.itemText(0), QStringLiteral("zero"));
        QCOMPARE(m_combo.currentIndex(), 0);
    }

    void movesALaterEntryToTheGivenPosition()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("three"), 0);
        QCOMPARE(m_combo.count(), 3);
        QCOMPARE(m_combo.itemText(0), QStringLiteral("three"));
        QCOMPARE(m_combo.itemText(1), QStringLiteral("one"));
        QCOMPARE(m_combo.currentIndex(), 0);
    }

    void entryAlreadyAtThePositionIsSelected()
    {
        HistoryCombo::addString(&m_combo, QStringLiteral("one"), 0);
        QCOMPARE(m_combo.count(), 3);
        QCOMPARE(m_combo.itemText(0), QStringLiteral("one"));
        QCOMPARE(m_combo.currentIndex(), 0);
    }

private:
    QComboBox m_combo;
};

QTEST_MAIN(HistoryComboTest)

#include "historycombotest.moc"
