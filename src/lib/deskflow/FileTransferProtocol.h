/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace deskflow {
class IStream;
}

class FileTransferProtocol
{
public:
  enum class ParseResult
  {
    NotFileMessage,
    Handled
  };

  using NotifyFunction = std::function<void(const QString &encodedMessage)>;

  FileTransferProtocol(deskflow::IStream *stream, bool peerAuthenticated, QString peerName, NotifyFunction notify);
  FileTransferProtocol(const FileTransferProtocol &) = delete;
  FileTransferProtocol &operator=(const FileTransferProtocol &) = delete;
  ~FileTransferProtocol();

  ParseResult parseMessage(const uint8_t *code);
  bool handleLocalMessage(const QString &encodedMessage);
  void setConnected(bool connected);
  bool isPeerAuthenticated() const;

  static QString encodeMessage(const QJsonObject &message);
  static bool decodeMessage(const QString &encoded, QJsonObject &message);
  static QString availabilityRefreshRequest();
  static bool isPeerlessAvailabilityRefresh(const QString &encodedMessage);

private:
  enum class State
  {
    Idle,
    AwaitingOfferResponse,
    AwaitingLocalOfferDecision,
    SenderReadyForChunk,
    AwaitingChunkAck,
    Receiving,
    AwaitingLocalChunkCommit,
    AwaitingLocalFinish,
    AwaitingLocalFinalResult,
    AwaitingRemoteFinalResult
  };

  void notify(QJsonObject message) const;
  void resetTransfer();
  bool isValidTransferId(const std::string &id) const;
  bool isTerminalTransferId(const std::string &id) const;
  bool isAuthenticatedFileMessage() const;

  deskflow::IStream *m_stream = nullptr;
  bool m_peerAuthenticated = false;
  bool m_connected = false;
  QString m_peerName;
  NotifyFunction m_notify;
  State m_state = State::Idle;
  std::string m_transferId;
  std::string m_fileName;
  std::string m_expectedHash;
  std::deque<std::string> m_terminalTransferIds;
  uint64_t m_expectedSize = 0;
  uint64_t m_sentSize = 0;
  uint64_t m_receivedSize = 0;
  uint32_t m_pendingChunkSize = 0;
  bool m_sending = false;
};
