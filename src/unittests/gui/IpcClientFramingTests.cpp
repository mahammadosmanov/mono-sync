/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/VersionInfo.h"
#include "deskflow/FileTransferProtocol.h"
#include "gui/ipc/IpcClient.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>
#include <QUuid>

namespace {

class TestIpcClient : public deskflow::gui::ipc::IpcClient
{
public:
  explicit TestIpcClient(const QString &name) : IpcClient(nullptr, name, QStringLiteral("test")) {}
  QStringList commands;
  QStringList arguments;
  void sendFileTransferMessage(const QString &message)
  {
    sendMessage(QStringLiteral("fileTransfer=%1").arg(message));
  }

protected:
  void processCommand(const QString &command, const QStringList &parts) override
  {
    commands.append(command);
    arguments.append(parts.size() >= 2 ? parts.at(1) : QString());
  }
};

} // namespace

class IpcClientFramingTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void keepsLargeLineUntilNewlineArrives();
  void sendsFileTransferMessageAsBoundedLocalIpcFrame();
};

void IpcClientFramingTests::keepsLargeLineUntilNewlineArrives()
{
  const QString socketName = QStringLiteral("msc-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
  QLocalServer server;
  QVERIFY(server.listen(socketName));

  TestIpcClient client(socketName);
  client.connectToServer();
  QTRY_VERIFY(server.hasPendingConnections());
  auto *peer = server.nextPendingConnection();
  QVERIFY(peer != nullptr);
  const QString versionId = QStringLiteral("%1+%2").arg(kVersion, kVersionGitSha);
  peer->write(QStringLiteral("hello=%1\n").arg(versionId).toUtf8());
  peer->flush();
  QTRY_VERIFY(client.isConnected());

  const QByteArray payload = "fileTransfer=" + QByteArray(44 * 1024, 'b');
  for (qsizetype offset = 0; offset < payload.size(); offset += 4096) {
    const QByteArray fragment = payload.mid(offset, 4096);
    QCOMPARE(peer->write(fragment), fragment.size());
    peer->flush();
    QTest::qWait(2);
  }
  QVERIFY(client.commands.isEmpty());
  QCOMPARE(peer->write("\n"), 1);
  peer->flush();
  QTRY_COMPARE(client.commands.size(), 1);
  QCOMPARE(client.commands.first(), QStringLiteral("fileTransfer"));
  QCOMPARE(client.arguments.first(), QString(44 * 1024, QLatin1Char('b')));

  QCOMPARE(peer->write("first=1\nsecond=2\n"), 17);
  peer->flush();
  QTRY_COMPARE(client.commands.size(), 3);
  QCOMPARE(client.commands.mid(1), QStringList({QStringLiteral("first"), QStringLiteral("second")}));

  peer->deleteLater();
  server.close();
}

void IpcClientFramingTests::sendsFileTransferMessageAsBoundedLocalIpcFrame()
{
  const QString socketName = QStringLiteral("msc-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
  QLocalServer server;
  QVERIFY(server.listen(socketName));

  TestIpcClient client(socketName);
  client.connectToServer();
  QTRY_VERIFY(server.hasPendingConnections());
  auto *peer = server.nextPendingConnection();
  QVERIFY(peer != nullptr);
  QTRY_VERIFY(peer->canReadLine());
  const QByteArray hello = peer->readLine();
  QVERIFY(hello.startsWith("hello="));
  const QString versionId = QStringLiteral("%1+%2").arg(kVersion, kVersionGitSha);
  QCOMPARE(peer->write(QStringLiteral("hello=%1\n").arg(versionId).toUtf8()),
           static_cast<qint64>(QStringLiteral("hello=%1\n").arg(versionId).toUtf8().size()));
  peer->flush();
  QTRY_VERIFY(client.isConnected());

  const QJsonObject offer{{QStringLiteral("action"), QStringLiteral("sendOffer")},
                          {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
                          {QStringLiteral("peer"), QStringLiteral("remote")},
                          {QStringLiteral("name"), QStringLiteral("sample.txt")},
                          {QStringLiteral("size"), QStringLiteral("3")}};
  const QString encoded = FileTransferProtocol::encodeMessage(offer);
  client.sendFileTransferMessage(encoded);
  QTRY_VERIFY(peer->canReadLine());
  const auto parts = QString::fromUtf8(peer->readLine()).trimmed().split('=');
  QCOMPARE(parts.size(), 2);
  QCOMPARE(parts.first(), QStringLiteral("fileTransfer"));
  QJsonObject decoded;
  QVERIFY(FileTransferProtocol::decodeMessage(parts.at(1), decoded));
  QCOMPARE(decoded.value(QStringLiteral("action")).toString(), QStringLiteral("sendOffer"));
  QCOMPARE(decoded.value(QStringLiteral("id")).toString(), offer.value(QStringLiteral("id")).toString());

  peer->deleteLater();
  server.close();
}

QTEST_GUILESS_MAIN(IpcClientFramingTests)

#include "IpcClientFramingTests.moc"
