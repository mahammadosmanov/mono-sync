/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/UpdateInstaller.h"

#include <QTest>

class UpdateInstallerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void shellQuoteKeepsPathsAsOneWord();
};

void UpdateInstallerTests::shellQuoteKeepsPathsAsOneWord()
{
  QCOMPARE(UpdateInstaller::shellQuote(QStringLiteral("/Applications/Mono Sync.app")),
           QStringLiteral("'/Applications/Mono Sync.app'"));
  QCOMPARE(UpdateInstaller::shellQuote(QStringLiteral("O'Brien $(rm)")), QStringLiteral("'O'\\''Brien $(rm)'"));
}

QTEST_GUILESS_MAIN(UpdateInstallerTests)

#include "UpdateInstallerTests.moc"
