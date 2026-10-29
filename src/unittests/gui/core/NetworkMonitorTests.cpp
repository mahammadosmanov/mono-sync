/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitorTests.h"

#include "gui/core/NetworkMonitor.h"

#include <QSignalSpy>

using namespace deskflow::gui;
void NetworkMonitorTests::testVirtualInterface()
{
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vboxnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vboXnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vmnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("docker-bridge")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("virbr0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("veth0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("br-0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("tun0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("utun0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("awdl0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("p2p0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("llw0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("anpi5")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("tap1")));

  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("eth0")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("enp0s0f")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("wifi")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("wlan0")));
}

void NetworkMonitorTests::testAddressRankingPrefersOnLinkAddresses()
{
  const auto ordered = NetworkMonitor::rankAddressCandidates({
      {QHostAddress(QStringLiteral("10.20.30.40")), 32, false, QStringLiteral("host-route")},
      {QHostAddress(QStringLiteral("192.168.20.4")), 24, false, QStringLiteral("Ethernet 2")},
      {QHostAddress(QStringLiteral("10.10.0.4")), 24, false, QStringLiteral("Ethernet")},
      {QHostAddress(QStringLiteral("172.20.0.1")), 24, true, QStringLiteral("Tunnel")},
      {QHostAddress(QStringLiteral("192.0.2.10")), 31, false, QStringLiteral("Point-to-point")},
      {QHostAddress(QStringLiteral("198.51.100.9")), -1, false, QStringLiteral("Unknown mask")},
      {QHostAddress(QStringLiteral("2001:db8::2")), 64, false, QStringLiteral("IPv6 LAN")},
      {QHostAddress(QStringLiteral("2001:db8::10")), 128, false, QStringLiteral("IPv6 host route")},
  });

  QCOMPARE(ordered.size(), 8);
  QCOMPARE(ordered.at(0).address.toString(), QStringLiteral("10.10.0.4"));
  QCOMPARE(ordered.at(0).interfaceName, QStringLiteral("Ethernet"));
  QCOMPARE(ordered.at(1).address.toString(), QStringLiteral("192.168.20.4"));
  QCOMPARE(ordered.at(1).interfaceName, QStringLiteral("Ethernet 2"));
  QCOMPARE(ordered.at(2).address.toString(), QStringLiteral("192.0.2.10"));
  QCOMPARE(ordered.at(3).address.toString(), QStringLiteral("10.20.30.40"));
  QCOMPARE(ordered.at(4).address.toString(), QStringLiteral("198.51.100.9"));
  QCOMPARE(ordered.at(5).address.toString(), QStringLiteral("172.20.0.1"));
  QCOMPARE(ordered.at(6).address.toString(), QStringLiteral("2001:db8::2"));
  QCOMPARE(ordered.at(7).address.toString(), QStringLiteral("2001:db8::10"));
}
QTEST_MAIN(NetworkMonitorTests)
