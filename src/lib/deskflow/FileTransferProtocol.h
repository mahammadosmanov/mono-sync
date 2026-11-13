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

  // fast: protocol 1.10 peer. Allows a window of chunks in flight instead of one.
  FileTransferProtocol(deskflow::IStream *stream, bool peerAuthenticated, QString peerName, NotifyFunction notify,
                       bool fast = false);
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
  uint32_t window() const;
  uint64_t inFlightBytes(const std::deque<uint32_t> &chunks) const;
  void requestNextIfRoom();
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
  std::deque<uint32_t> m_inFlightChunks; // sender: chunks sent, not yet acknowledged
  std::deque<uint32_t> m_pendingChunks;  // receiver: chunks received, not yet saved
  bool m_nextRequested = false;          // sender: a sendNextChunk request is outstanding
  bool m_fast = false;
  bool m_sending = false;
};
