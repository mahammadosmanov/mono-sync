/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"
#include "deskflow/FileTransferProtocol.h"

class ServerProxy1_9 : public ServerProxy1_8
{
public:
  ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxy1_9() override = default;
  void connectionReady() override;
  bool handleFileTransferMessage(const QString &message) override;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;

private:
  FileTransferProtocol m_fileTransfer;
};
