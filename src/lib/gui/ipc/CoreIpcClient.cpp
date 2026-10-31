/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/Constants.h"
#include "common/CoreIpcPath.h"

#include <QLocalSocket>
#include <QString>

#include <cerrno>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace deskflow::gui::ipc {

CoreIpcClient::CoreIpcClient(QObject *parent) : IpcClient(parent, coreIpcServerName(), QStringLiteral("core"))
{
  // do nothing
}

CoreIpcClient::CoreIpcClient(QObject *parent, const QString &socketName)
    : IpcClient(parent, socketName, QStringLiteral("core"))
{
  // Injectable endpoint keeps IPC identity tests isolated from a running Mono Sync core.
}

void CoreIpcClient::setExpectedServerProcessId(qint64 processId)
{
  m_expectedServerProcessId = processId;
}

void CoreIpcClient::connectToServer()
{
  if (!isPrivateCoreIpcDirectoryTrusted()) {
    Q_EMIT connectionFailed();
    return;
  }
  IpcClient::connectToServer();
}

bool CoreIpcClient::verifyServerPeer() const
{
  m_serverPeerVerificationFailureReason.clear();
#ifdef Q_OS_WIN
  if (m_expectedServerProcessId <= 0) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the launched core process ID is unavailable");
    return false;
  }
  if (socket() == nullptr) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the local IPC socket is unavailable");
    return false;
  }
  ULONG serverProcessId = 0;
  const auto handle = reinterpret_cast<HANDLE>(static_cast<quintptr>(socket()->socketDescriptor()));
  if (GetNamedPipeServerProcessId(handle, &serverProcessId) == 0) {
    m_serverPeerVerificationFailureReason = QStringLiteral("Windows could not read the named-pipe server PID (error %1)")
                                               .arg(GetLastError());
    return false;
  }
  if (static_cast<qint64>(serverProcessId) != m_expectedServerProcessId) {
    m_serverPeerVerificationFailureReason =
        QStringLiteral("the named-pipe server PID did not match the launched core process (expected %1, got %2)")
            .arg(m_expectedServerProcessId)
            .arg(serverProcessId);
    return false;
  }
  return true;
#elif defined(Q_OS_MACOS)
  uid_t peerUid = static_cast<uid_t>(-1);
  gid_t peerGid = static_cast<gid_t>(-1);
  if (socket() == nullptr || !isPrivateCoreIpcDirectoryTrusted()) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the local IPC directory or socket is not trusted");
    return false;
  }
  if (getpeereid(static_cast<int>(socket()->socketDescriptor()), &peerUid, &peerGid) != 0) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the operating system could not read local IPC peer credentials (error %1)")
                                               .arg(errno);
    return false;
  }
  if (peerUid != geteuid()) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the local IPC server belongs to a different user");
    return false;
  }
  return true;
#else
  struct ucred credentials
  {
  };
  socklen_t length = sizeof(credentials);
  if (socket() == nullptr || !isPrivateCoreIpcDirectoryTrusted()) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the local IPC directory or socket is not trusted");
    return false;
  }
  const int descriptor = static_cast<int>(socket()->socketDescriptor());
  if (getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the operating system could not read local IPC peer credentials (error %1)")
                                               .arg(errno);
    return false;
  }
  if (credentials.uid != geteuid()) {
    m_serverPeerVerificationFailureReason = QStringLiteral("the local IPC server belongs to a different user");
    return false;
  }
  return true;
#endif
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

void CoreIpcClient::sendFileTransferMessage(const QString &encodedMessage)
{
  sendMessage(QStringLiteral("fileTransfer=%1").arg(encodedMessage));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.at(1) : QString();
  Q_EMIT commandReceived(command, args);
}

} // namespace deskflow::gui::ipc
