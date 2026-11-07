/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "../deskflow/MockEventQueue.h"
#include "base/Log.h"
#include "common/CoreIpcPath.h"
#include "common/VersionInfo.h"
#include "deskflow/FileTransferProtocol.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "gui/ipc/CoreIpcClient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTest>
#include <QUuid>

namespace {

class RecordingEventQueue : public MockEventQueue
{
public:
  QString command;
  QString payload;

  void addEvent(Event &&event) override
  {
    if (const auto *ipcCommand = dynamic_cast<CoreIpcCommand *>(event.getDataObject())) {
      command = ipcCommand->command();
      payload = ipcCommand->payload();
    }
    Event::deleteData(event);
  }
};

QString uniqueIpcServerName()
{
  const auto id = QUuid::createUuid().toString(QUuid::Id128).left(8);
#ifdef Q_OS_WIN
  return QStringLiteral("MonoSyncCoreTest-%1").arg(id);
#else
  return QFileInfo(coreIpcServerName()).dir().filePath(QStringLiteral("t-%1.sock").arg(id));
#endif
}

} // namespace

class CoreIpcDeliveryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void authenticatedPeerAvailabilitySurvivesLateGuiAttachment();

private:
  Log m_log;
};

void CoreIpcDeliveryTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Debug);
}

void CoreIpcDeliveryTests::authenticatedPeerAvailabilitySurvivesLateGuiAttachment()
{
#ifndef Q_OS_WIN
  QVERIFY(preparePrivateCoreIpcDirectory());
#endif
  RecordingEventQueue events;
  const auto serverName = uniqueIpcServerName();
  deskflow::core::ipc::CoreIpcServer server(nullptr, &events, serverName);
  server.listen();

  // Model the network peer becoming available before this GUI process attaches.
  const auto availability = FileTransferProtocol::encodeMessage(
      {{QStringLiteral("action"), QStringLiteral("availability")},
       {QStringLiteral("available"), true},
       {QStringLiteral("peer"), QStringLiteral("remote-peer")}}
  );
  server.broadcastCommand(QStringLiteral("fileTransfer"), availability);

  deskflow::gui::ipc::CoreIpcClient client(nullptr, serverName);
  client.setExpectedServerProcessId(QCoreApplication::applicationPid());
  QStringList deliveredAvailability;
  connect(&client, &deskflow::gui::ipc::CoreIpcClient::connected, &client, [&] {
    if (client.isServerPeerVerified()) {
      client.sendFileTransferMessage(FileTransferProtocol::availabilityRefreshRequest());
    }
  });
  connect(&client, &deskflow::gui::ipc::CoreIpcClient::commandReceived, &client,
          [&](const QString &command, const QString &args) {
            if (command == QStringLiteral("fileTransfer") && client.isServerPeerVerified()) {
              deliveredAvailability.append(args);
            }
          });

  client.connectToServer();
  QTRY_VERIFY(client.isConnected());
  QVERIFY(client.isServerPeerVerified());
  QVERIFY(client.serverPeerVerificationFailureReason().isEmpty());
  QTRY_COMPARE(deliveredAvailability, QStringList{availability});
  QTRY_COMPARE(events.command, QStringLiteral("fileTransfer"));
  QVERIFY(FileTransferProtocol::isPeerlessAvailabilityRefresh(events.payload));

  // Exercise the production notification entry point after the late GUI has attached.
  ipcSendToClient(QStringLiteral("fileTransfer"), availability);
  QTRY_COMPARE(deliveredAvailability, QStringList({availability, availability}));

#ifdef Q_OS_WIN
  client.disconnectFromServer();
  QTRY_VERIFY(!client.isConnected());

  deskflow::gui::ipc::CoreIpcClient wrongPidClient(nullptr, serverName);
  wrongPidClient.setExpectedServerProcessId(QCoreApplication::applicationPid() + 1);
  bool acceptedUnverifiedAvailability = false;
  connect(&wrongPidClient, &deskflow::gui::ipc::CoreIpcClient::connected, &wrongPidClient, [&] {
    if (wrongPidClient.isServerPeerVerified()) {
      wrongPidClient.sendFileTransferMessage(FileTransferProtocol::availabilityRefreshRequest());
    }
  });
  connect(&wrongPidClient, &deskflow::gui::ipc::CoreIpcClient::commandReceived, &wrongPidClient,
          [&](const QString &command, const QString &args) {
            Q_UNUSED(args)
            acceptedUnverifiedAvailability = command == QStringLiteral("fileTransfer") &&
                                             wrongPidClient.isServerPeerVerified();
          });
  wrongPidClient.connectToServer();
  QTRY_VERIFY(wrongPidClient.isConnected());
  QVERIFY(!wrongPidClient.isServerPeerVerified());
  QVERIFY(!wrongPidClient.serverPeerVerificationFailureReason().isEmpty());
  ipcSendToClient(QStringLiteral("fileTransfer"), availability);
  QTest::qWait(100);
  QVERIFY(!acceptedUnverifiedAvailability);
#endif
}

QTEST_GUILESS_MAIN(CoreIpcDeliveryTests)

#include "CoreIpcDeliveryTests.moc"
