/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"
#include "common/CoreIpcPath.h"
#include "base/Event.h"
#include "base/IEventQueue.h"
#include "deskflow/ipc/CoreIpc.h"

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent, IEventQueue *events)
    : IpcServer(parent, coreIpcServerName(), QStringLiteral("core"), true),
      m_events(events)
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  Q_UNUSED(parts)
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  if (command == QStringLiteral("fileTransfer")) {
    if (parts.size() != 2 || parts.at(1).size() > 48 * 1024 || m_events == nullptr) {
      writeToClientSocket(clientSocket, QStringLiteral("error=invalid file-transfer command"));
      return;
    }
    m_events->addEvent(Event(
        EventTypes::CoreIpcCommand, m_events->getSystemTarget(),
        new CoreIpcCommand(command, parts.at(1))
    ));
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
