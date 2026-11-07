/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "deskflow/ProtocolTypes.h"
#include "deskflow/ipc/CoreIpc.h"
#include "io/IStream.h"

#include <cstring>

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events),
      m_fileTransfer(stream, stream->isPeerAuthenticated(), QStringLiteral("server"), [](const QString &message) {
        ipcSendToClient(QStringLiteral("fileTransfer"), message);
      })
{
}

void ServerProxy1_9::connectionReady()
{
  m_fileTransfer.setConnected(true);
}

bool ServerProxy1_9::handleFileTransferMessage(const QString &message)
{
  return m_fileTransfer.handleLocalMessage(message);
}

ServerProxy::ConnectionResult ServerProxy1_9::parseMessage(const uint8_t *code)
{
  if (m_fileTransfer.parseMessage(code) == FileTransferProtocol::ParseResult::Handled) {
    return ConnectionResult::Okay;
  }
  return ServerProxy1_8::parseMessage(code);
}
