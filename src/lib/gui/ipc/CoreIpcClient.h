/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcClient.h"

#include <QObject>
#include <QString>

namespace deskflow::gui::ipc {

class CoreIpcClient : public IpcClient
{
  Q_OBJECT

public:
  explicit CoreIpcClient(QObject *parent = nullptr);
  CoreIpcClient(QObject *parent, const QString &socketName);
  void setExpectedServerProcessId(qint64 processId);
  void connectToServer();
  QString serverPeerVerificationFailureReason() const
  {
    return m_serverPeerVerificationFailureReason;
  }

  void sendStop();
  void sendFileTransferMessage(const QString &encodedMessage);

Q_SIGNALS:
  void commandReceived(const QString &command, const QString &args);

protected:
  bool verifyServerPeer() const override;
  void processCommand(const QString &command, const QStringList &parts) override;

private:
  qint64 m_expectedServerProcessId = 0;
  mutable QString m_serverPeerVerificationFailureReason;
};

} // namespace deskflow::gui::ipc
