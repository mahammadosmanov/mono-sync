/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/TransferPanel.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using State = TransferPanel::State;

class TransferPanelTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void statusTextCoversEveryState();
  void rowsAddRenameRemove();
  void finishedRowsAreTrimmed();
  void saveButtonEmitsAcceptForOfferRow();
  void cancelButtonEmitsCancelForActiveRow();
};

void TransferPanelTests::statusTextCoversEveryState()
{
  using Panel = TransferPanel;
  QCOMPARE(Panel::statusText(State::Queued, false, "Office-PC", 0, 0, 0, {}),
           QStringLiteral("Queued for Office-PC"));
  QCOMPARE(Panel::statusText(State::Preparing, false, "Office-PC", 0, 0, 0, {}), QStringLiteral("Preparing…"));
  QCOMPARE(Panel::statusText(State::WaitingForPeer, false, "Office-PC", 0, 0, 0, {}),
           QStringLiteral("Waiting for Office-PC to accept…"));
  QVERIFY(Panel::statusText(State::Offer, true, "Office-PC", 0, 100, 0, {})
              .startsWith(QStringLiteral("Office-PC wants to send this")));

  const QString plain = Panel::statusText(State::Transferring, false, "Office-PC", 10, 100, 0, {});
  QVERIFY(plain.contains(QStringLiteral(" of ")));
  QVERIFY(!plain.contains(QStringLiteral("/s")));

  const QString fast = Panel::statusText(State::Transferring, false, "Office-PC", 1000, 2000, 500, {});
  QVERIFY(fast.contains(QStringLiteral("/s")));
  QVERIFY(fast.endsWith(QStringLiteral("2 s left")));

  QCOMPARE(Panel::statusText(State::Done, true, "Office-PC", 0, 0, 0, QStringLiteral("/tmp/a.pdf")),
           QStringLiteral("Saved to /tmp/a.pdf"));
  QVERIFY(Panel::statusText(State::Done, false, "Office-PC", 0, 100, 0, {})
              .startsWith(QStringLiteral("Sent to Office-PC")));
  QCOMPARE(Panel::statusText(State::Failed, false, "Office-PC", 0, 0, 0, QStringLiteral("boom")),
           QStringLiteral("Failed: boom"));
  QCOMPARE(Panel::statusText(State::Failed, false, "Office-PC", 0, 0, 0, {}), QStringLiteral("Failed"));
}

void TransferPanelTests::rowsAddRenameRemove()
{
  TransferPanel panel;
  QVERIFY(panel.isHidden());

  panel.addRow(QStringLiteral("a"), QStringLiteral("a.pdf"), QStringLiteral("Office-PC"), 10, false, State::Queued);
  QVERIFY(panel.hasRow(QStringLiteral("a")));
  QVERIFY(!panel.isHidden());

  panel.renameRow(QStringLiteral("a"), QStringLiteral("id-1"));
  QVERIFY(!panel.hasRow(QStringLiteral("a")));
  QVERIFY(panel.hasRow(QStringLiteral("id-1")));

  panel.addRow(QStringLiteral("id-1"), QStringLiteral("a.pdf"), QStringLiteral("Office-PC"), 10, false, State::Preparing);
  QVERIFY(panel.hasRow(QStringLiteral("id-1")));

  panel.removeRow(QStringLiteral("id-1"));
  QVERIFY(!panel.hasRow(QStringLiteral("id-1")));
  QVERIFY(panel.isHidden());
}

void TransferPanelTests::finishedRowsAreTrimmed()
{
  TransferPanel panel;
  for (int i = 0; i < 6; ++i) {
    const auto id = QStringLiteral("f%1").arg(i);
    panel.addRow(id, QStringLiteral("f.bin"), QStringLiteral("Office-PC"), 1, true, State::Failed);
  }
  panel.addRow(QStringLiteral("q"), QStringLiteral("q.bin"), QStringLiteral("Office-PC"), 1, false, State::Queued);
  QVERIFY(!panel.hasRow(QStringLiteral("f0")));
  QVERIFY(panel.hasRow(QStringLiteral("f1")));
  QVERIFY(panel.hasRow(QStringLiteral("q")));
}

void TransferPanelTests::saveButtonEmitsAcceptForOfferRow()
{
  TransferPanel panel;
  QSignalSpy accepted(&panel, &TransferPanel::acceptRequested);
  panel.addRow(QStringLiteral("offer-1"), QStringLiteral("a.pdf"), QStringLiteral("Office-PC"), 10, true, State::Offer);
  panel.findChild<QPushButton *>(QStringLiteral("transferSave"))->click();
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(accepted.first().first().toString(), QStringLiteral("offer-1"));
}

void TransferPanelTests::cancelButtonEmitsCancelForActiveRow()
{
  TransferPanel panel;
  QSignalSpy cancelled(&panel, &TransferPanel::cancelRequested);
  panel.addRow(QStringLiteral("run-1"), QStringLiteral("b.pdf"), QStringLiteral("Office-PC"), 10, false,
               State::Transferring);
  panel.findChild<QPushButton *>(QStringLiteral("transferCancel"))->click();
  QCOMPARE(cancelled.count(), 1);
  QCOMPARE(cancelled.first().first().toString(), QStringLiteral("run-1"));
}

QTEST_MAIN(TransferPanelTests)

#include "TransferPanelTests.moc"
