/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcClient.h"

#include <QObject>

namespace deskflow::gui::ipc {

class CoreIpcClient : public IpcClient
{
  Q_OBJECT

public:
  explicit CoreIpcClient(QObject *parent = nullptr);
  void setExpectedServerProcessId(qint64 processId);
  void connectToServer();

  void sendStop();
  void sendFileTransferMessage(const QString &encodedMessage);

Q_SIGNALS:
  void commandReceived(const QString &command, const QString &args);

protected:
  bool verifyServerPeer() const override;
  void processCommand(const QString &command, const QStringList &parts) override;

private:
  qint64 m_expectedServerProcessId = 0;
};

} // namespace deskflow::gui::ipc
