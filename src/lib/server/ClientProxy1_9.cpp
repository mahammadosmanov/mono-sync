/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/IEventQueue.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ipc/CoreIpc.h"
#include "io/IStream.h"

#include <cstring>

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, stream, server, events),
      m_events(events),
      m_fileTransfer(stream, stream->isPeerAuthenticated(), QString::fromStdString(name), [](const QString &message) {
        ipcSendToClient(QStringLiteral("fileTransfer"), message);
      })
{
  m_events->addHandler(EventTypes::ClientProxyReady, this, [this](const auto &) { m_fileTransfer.setConnected(true); });
  m_events->addHandler(EventTypes::ClientProxyDisconnected, this, [this](const auto &) {
    m_fileTransfer.setConnected(false);
  });
}

ClientProxy1_9::~ClientProxy1_9()
{
  m_fileTransfer.setConnected(false);
  m_events->removeHandler(EventTypes::ClientProxyReady, this);
  m_events->removeHandler(EventTypes::ClientProxyDisconnected, this);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (m_fileTransfer.parseMessage(code) == FileTransferProtocol::ParseResult::Handled) {
    return true;
  }
  return ClientProxy1_8::parseMessage(code);
}

bool ClientProxy1_9::handleFileTransferMessage(const QString &message)
{
  return m_fileTransfer.handleLocalMessage(message);
}
