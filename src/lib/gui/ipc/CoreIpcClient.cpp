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
#ifdef Q_OS_WIN
  if (m_expectedServerProcessId <= 0 || socket() == nullptr) {
    return false;
  }
  ULONG serverProcessId = 0;
  const auto handle = reinterpret_cast<HANDLE>(static_cast<quintptr>(socket()->socketDescriptor()));
  return GetNamedPipeServerProcessId(handle, &serverProcessId) != 0 &&
         static_cast<qint64>(serverProcessId) == m_expectedServerProcessId;
#elif defined(Q_OS_MACOS)
  uid_t peerUid = static_cast<uid_t>(-1);
  gid_t peerGid = static_cast<gid_t>(-1);
  return socket() != nullptr && isPrivateCoreIpcDirectoryTrusted() &&
         getpeereid(static_cast<int>(socket()->socketDescriptor()), &peerUid, &peerGid) == 0 && peerUid == geteuid();
#else
  struct ucred credentials
  {
  };
  socklen_t length = sizeof(credentials);
  if (socket() == nullptr || !isPrivateCoreIpcDirectoryTrusted()) {
    return false;
  }
  const int descriptor = static_cast<int>(socket()->socketDescriptor());
  return getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 && credentials.uid == geteuid();
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
