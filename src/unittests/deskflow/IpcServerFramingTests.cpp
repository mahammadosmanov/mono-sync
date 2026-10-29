/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "deskflow/ipc/IpcServer.h"

#include <QLocalSocket>
#include <QTest>
#include <QUuid>

namespace {

class TestIpcServer : public deskflow::core::ipc::IpcServer
{
public:
  explicit TestIpcServer(const QString &name) : IpcServer(nullptr, name, QStringLiteral("test")) {}
  QString received;
  QStringList commands;

protected:
  void processCommand(QLocalSocket *, const QString &command, const QStringList &parts) override
  {
    commands.append(command);
    if (command == QStringLiteral("fileTransfer") && parts.size() == 2) {
      received = parts.at(1);
    }
  }
};

} // namespace

class IpcServerFramingTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void keepsLargeLineUntilNewlineArrives();

private:
  Log m_log;
};

void IpcServerFramingTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Debug);
}

void IpcServerFramingTests::keepsLargeLineUntilNewlineArrives()
{
  const QString socketName = QStringLiteral("mono-sync-ipc-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
  TestIpcServer server(socketName);
  server.listen();

  QLocalSocket client;
  client.connectToServer(socketName);
  QVERIFY(client.waitForConnected(2000));

  const QByteArray payload = "fileTransfer=" + QByteArray(44 * 1024, 'a');
  for (qsizetype offset = 0; offset < payload.size(); offset += 4096) {
    const QByteArray fragment = payload.mid(offset, 4096);
    QCOMPARE(client.write(fragment), fragment.size());
    client.flush();
    QTest::qWait(2);
  }
  QVERIFY(server.received.isEmpty());

  QCOMPARE(client.write("\n"), 1);
  client.flush();
  QTRY_COMPARE(server.received, QString(44 * 1024, QLatin1Char('a')));

  QCOMPARE(client.write("first=1\nsecond=2\n"), 17);
  client.flush();
  QTRY_COMPARE(server.commands.size(), 3);
  QCOMPARE(server.commands.at(1), QStringLiteral("first"));
  QCOMPARE(server.commands.at(2), QStringLiteral("second"));

  QLocalSocket partialClient;
  partialClient.connectToServer(socketName);
  QVERIFY(partialClient.waitForConnected(2000));
  QCOMPARE(partialClient.write("fileTransfer=partial"), 20);
  partialClient.flush();
  partialClient.disconnectFromServer();
  QTRY_VERIFY(partialClient.state() == QLocalSocket::UnconnectedState);

  QLocalSocket nextClient;
  nextClient.connectToServer(socketName);
  QVERIFY(nextClient.waitForConnected(2000));
  QCOMPARE(nextClient.write("fileTransfer=complete\n"), 22);
  nextClient.flush();
  QTRY_COMPARE(server.received, QStringLiteral("complete"));
}

QTEST_GUILESS_MAIN(IpcServerFramingTests)

#include "IpcServerFramingTests.moc"
