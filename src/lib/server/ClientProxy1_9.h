/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransferProtocol.h"
#include "server/ClientProxy1_8.h"

class IEventQueue;

class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events);
  ~ClientProxy1_9() override;
  bool parseMessage(const uint8_t *code) override;
  bool handleFileTransferMessage(const QString &message);

private:
  IEventQueue *m_events;
  FileTransferProtocol m_fileTransfer;
};
