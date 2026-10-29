/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitor.h"

#include <QAbstractSocket>
#include <QList>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace deskflow::gui {

bool NetworkMonitor::isVirtualInterface(const QString &interfaceName)
{
  // Common virtual network interface patterns
  static const auto virtualRegEx = QRegularExpression(
      QStringLiteral("^vboxnet|vmnet|docker|virbr|veth|br\\-|tun|utun|awdl|p2p|llw|anpi|tap|vEth"),
      QRegularExpression::CaseInsensitiveOption
  );
  return virtualRegEx.match(interfaceName).hasMatch();
}

QList<NetworkMonitor::AddressCandidate> NetworkMonitor::rankAddressCandidates(QList<AddressCandidate> candidates)
{
  std::ranges::stable_sort(candidates, [](const AddressCandidate &a, const AddressCandidate &b) {
    const bool aIpv4 = a.address.protocol() == QHostAddress::IPv4Protocol;
    const bool bIpv4 = b.address.protocol() == QHostAddress::IPv4Protocol;
    if (aIpv4 != bIpv4)
      return aIpv4;
    if (a.virtualInterface != b.virtualInterface)
      return !a.virtualInterface;

    const int addressBits = aIpv4 ? 32 : 128;
    const bool aOnLink = a.prefixLength >= 0 && a.prefixLength < addressBits;
    const bool bOnLink = b.prefixLength >= 0 && b.prefixLength < addressBits;
    if (aOnLink != bOnLink)
      return aOnLink;

    if (a.address.isPrivateUse() != b.address.isPrivateUse())
      return a.address.isPrivateUse();
    if (a.address.isLinkLocal() != b.address.isLinkLocal())
      return !a.address.isLinkLocal();
    return aIpv4 ? a.address.toIPv4Address() < b.address.toIPv4Address()
                 : a.address.toString() < b.address.toString();
  });

  return candidates;
}

NetworkMonitor::NetworkMonitor(QObject *parent) : QObject(parent), m_checkTimer(new QTimer(this))
{
  connect(m_checkTimer, &QTimer::timeout, this, &NetworkMonitor::updateNetworkState);
}

void NetworkMonitor::startMonitoring(int intervalMs)
{
  if (m_isMonitoring) {
    return;
  }

  updateNetworkState();

  m_checkTimer->start(intervalMs);
  m_isMonitoring = true;
}

void NetworkMonitor::stopMonitoring()
{
  if (!m_isMonitoring) {
    return;
  }

  m_checkTimer->stop();
  m_isMonitoring = false;
}

QStringList NetworkMonitor::validAddresses()
{
  QStringList result;
  for (const auto &candidate : rankedCandidates())
    result.append(candidate.address.toString());
  return result;
}

QList<NetworkMonitor::AddressCandidate> NetworkMonitor::rankedCandidates()
{
  QList<AddressCandidate> candidates;
  QSet<QHostAddress> uniqueAddresses;

  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : allInterfaces) {
    if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning) ||
        (interface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }

    const bool isP2P = (interface.flags() & QNetworkInterface::IsPointToPoint);
    const bool isVirtualType = interface.type() == QNetworkInterface::Virtual;
    const bool isVirtual = isVirtualInterface(interface.humanReadableName()) || isP2P || isVirtualType;
    const auto addressEntries = interface.addressEntries();
    // link-local is only worth offering when it's a physical interface's only address, e.g. a direct ethernet cable
    const bool hasNonLinkLocal = std::ranges::any_of(addressEntries, [](const QNetworkAddressEntry &entry) {
      return !entry.ip().isLoopback() && !entry.ip().isLinkLocal();
    });
    const bool skipLinkLocal = isVirtual || hasNonLinkLocal;

    for (const auto &entry : addressEntries) {
      const QHostAddress address = entry.ip();

      if (address.isLoopback() || (skipLinkLocal && address.isLinkLocal()) || uniqueAddresses.contains(address)) {
        continue;
      }

      uniqueAddresses.insert(address);
      candidates.append({address, entry.prefixLength(), isVirtual, interface.humanReadableName()});
    }
  }
  return rankAddressCandidates(std::move(candidates));
}

void NetworkMonitor::setIpAddresses(const QStringList &newAddresses)
{
  if (newAddresses == m_lastAddresses)
    return;
  m_lastAddresses = newAddresses;
  Q_EMIT ipAddressesChanged(m_lastAddresses);
}

void NetworkMonitor::updateNetworkState()
{
  setIpAddresses(validAddresses());
}

} // namespace deskflow::gui
