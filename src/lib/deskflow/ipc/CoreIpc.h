/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "base/Event.h"

#include <QString>

class CoreIpcCommand : public EventData
{
public:
  CoreIpcCommand(QString command, QString payload) : m_command(std::move(command)), m_payload(std::move(payload))
  {
  }

  const QString &command() const
  {
    return m_command;
  }
  const QString &payload() const
  {
    return m_payload;
  }

private:
  QString m_command;
  QString m_payload;
};

void ipcSendToClient(const QString &command, const QString &args = "");
void ipcSendConnectionState(deskflow::core::ConnectionState state);
