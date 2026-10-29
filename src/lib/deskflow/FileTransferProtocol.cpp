/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferProtocol.h"

#include "DeskflowException.h"
#include "FileTransferReceiver.h"
#include "ProtocolTypes.h"
#include "ProtocolUtil.h"
#include "io/IStream.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint32_t kTransferIdMaximum = 36;
constexpr uint32_t kFileNameMaximum = 240;
constexpr uint32_t kSha256Length = 32;

QString actionName(const char *name)
{
  return QString::fromLatin1(name);
}

bool checkedRead(deskflow::IStream *stream, const char *format, uint32_t *first = nullptr, uint32_t *second = nullptr)
{
  bool success = false;
  if (first != nullptr && second != nullptr) {
    success = ProtocolUtil::readf(stream, format, first, second);
  } else if (first != nullptr) {
    success = ProtocolUtil::readf(stream, format, first);
  } else {
    success = ProtocolUtil::readf(stream, format);
  }
  if (!success) {
    throw BadClientException("Malformed file-transfer message");
  }
  return success;
}

uint64_t combineSize(uint32_t high, uint32_t low)
{
  return (static_cast<uint64_t>(high) << 32) | low;
}

} // namespace

FileTransferProtocol::FileTransferProtocol(
    deskflow::IStream *stream, bool peerAuthenticated, QString peerName, NotifyFunction notifyFunction
)
    : m_stream(stream),
      m_peerAuthenticated(peerAuthenticated),
      m_peerName(std::move(peerName)),
      m_notify(std::move(notifyFunction))
{
}

FileTransferProtocol::~FileTransferProtocol()
{
  setConnected(false);
}

FileTransferProtocol::ParseResult FileTransferProtocol::parseMessage(const uint8_t *code)
{
  const auto isFileMessage =
      std::memcmp(code, "MSOF", 4) == 0 || std::memcmp(code, "MSRP", 4) == 0 || std::memcmp(code, "MSCH", 4) == 0 ||
      std::memcmp(code, "MSAK", 4) == 0 || std::memcmp(code, "MSFN", 4) == 0 || std::memcmp(code, "MSOK", 4) == 0 ||
      std::memcmp(code, "MSER", 4) == 0 || std::memcmp(code, "MSCA", 4) == 0;
  if (!isFileMessage) {
    return ParseResult::NotFileMessage;
  }
  if (!isAuthenticatedFileMessage()) {
    throw BadClientException("File transfer requires an authenticated TLS peer");
  }
  if (!m_connected) {
    throw BadClientException("File transfer is not available before the peer handshake completes");
  }

  if (std::memcmp(code, "MSOF", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    uint32_t sizeHigh = 0;
    uint32_t sizeLow = 0;
    checkedRead(m_stream, "%4i%4i", &sizeHigh, &sizeLow);
    std::string hash = ProtocolUtil::readBoundedString(m_stream, kSha256Length);
    std::string fileName = ProtocolUtil::readBoundedString(m_stream, kFileNameMaximum);
    const uint64_t size = combineSize(sizeHigh, sizeLow);
    if (!isValidTransferId(id) || hash.size() != kSha256Length || fileName.empty() ||
        size > FileTransferReceiver::kMaximumFileSize) {
      throw BadClientException("Invalid file-transfer offer");
    }
    const QString decodedName = QString::fromUtf8(fileName.data(), static_cast<qsizetype>(fileName.size()));
    if (decodedName.toUtf8() != QByteArray::fromStdString(fileName) ||
        !FileTransferReceiver::isSafeFileName(decodedName)) {
      throw BadClientException("Invalid file-transfer name");
    }
    if (m_state != State::Idle || isTerminalTransferId(id)) {
      const uint32_t rejected = 0;
      ProtocolUtil::writef(m_stream, kMsgFileResponse, &id, rejected);
      return ParseResult::Handled;
    }
    m_state = State::AwaitingLocalOfferDecision;
    m_transferId = std::move(id);
    m_fileName = std::move(fileName);
    m_expectedHash = std::move(hash);
    m_expectedSize = size;
    m_receivedSize = 0;
    m_sending = false;
    notify({
        {QStringLiteral("action"), actionName("offer")},
        {QStringLiteral("id"), QString::fromStdString(m_transferId)},
        {QStringLiteral("name"), decodedName},
        {QStringLiteral("size"), QString::number(m_expectedSize)},
        {QStringLiteral("sha256"), QString::fromLatin1(QByteArray::fromStdString(m_expectedHash).toHex())},
    });
  } else if (std::memcmp(code, "MSRP", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    uint32_t result = 0;
    checkedRead(m_stream, "%1i", &result);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId || m_state != State::AwaitingOfferResponse || result > 1) {
      throw BadClientException("Unexpected file-transfer response");
    }
    if (result == 1) {
      m_state = State::SenderReadyForChunk;
      notify({{QStringLiteral("action"), actionName("accepted")}, {QStringLiteral("id"), QString::fromStdString(id)}});
    } else {
      notify({{QStringLiteral("action"), actionName("rejected")}, {QStringLiteral("id"), QString::fromStdString(id)}});
      resetTransfer();
    }
  } else if (std::memcmp(code, "MSCH", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    std::string chunk = ProtocolUtil::readBoundedString(m_stream, FileTransferReceiver::kMaximumChunkSize);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId || m_state != State::Receiving || chunk.empty() ||
        chunk.size() > m_expectedSize - m_receivedSize) {
      throw BadClientException("Unexpected file-transfer chunk");
    }
    m_pendingChunkSize = static_cast<uint32_t>(chunk.size());
    m_state = State::AwaitingLocalChunkCommit;
    notify({
        {QStringLiteral("action"), actionName("receiveChunk")},
        {QStringLiteral("id"), QString::fromStdString(id)},
        {QStringLiteral("data"), QString::fromLatin1(QByteArray::fromStdString(chunk).toBase64(
             QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))},
        {QStringLiteral("received"), QString::number(m_receivedSize)},
        {QStringLiteral("total"), QString::number(m_expectedSize)},
    });
  } else if (std::memcmp(code, "MSAK", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    uint32_t sizeHigh = 0;
    uint32_t sizeLow = 0;
    checkedRead(m_stream, "%4i%4i", &sizeHigh, &sizeLow);
    const uint64_t acknowledged = combineSize(sizeHigh, sizeLow);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId || m_state != State::AwaitingChunkAck ||
        acknowledged != m_sentSize + m_pendingChunkSize) {
      throw BadClientException("Unexpected file-transfer acknowledgement");
    }
    m_sentSize = acknowledged;
    m_pendingChunkSize = 0;
    if (m_sentSize == m_expectedSize) {
      notify({
          {QStringLiteral("action"), actionName("sendReadyToFinish")},
          {QStringLiteral("id"), QString::fromStdString(id)},
          {QStringLiteral("sent"), QString::number(m_sentSize)},
          {QStringLiteral("total"), QString::number(m_expectedSize)},
      });
      m_state = State::SenderReadyForChunk;
    } else {
      m_state = State::SenderReadyForChunk;
      notify({
          {QStringLiteral("action"), actionName("sendNextChunk")},
          {QStringLiteral("id"), QString::fromStdString(id)},
          {QStringLiteral("sent"), QString::number(m_sentSize)},
          {QStringLiteral("total"), QString::number(m_expectedSize)},
      });
    }
  } else if (std::memcmp(code, "MSFN", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId || m_state != State::Receiving ||
        m_receivedSize != m_expectedSize) {
      throw BadClientException("Unexpected file-transfer completion");
    }
    m_state = State::AwaitingLocalFinish;
    notify({
        {QStringLiteral("action"), actionName("finishReceive")},
        {QStringLiteral("id"), QString::fromStdString(id)},
        {QStringLiteral("total"), QString::number(m_expectedSize)},
    });
  } else if (std::memcmp(code, "MSOK", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId || m_state != State::AwaitingRemoteFinalResult) {
      throw BadClientException("Unexpected file-transfer result");
    }
    notify({{QStringLiteral("action"), actionName("completed")}, {QStringLiteral("id"), QString::fromStdString(id)}});
    resetTransfer();
  } else if (std::memcmp(code, "MSER", 4) == 0) {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    uint32_t errorCode = 0;
    checkedRead(m_stream, "%1i", &errorCode);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId) {
      throw BadClientException("Unexpected file-transfer error");
    }
    notify({
        {QStringLiteral("action"), actionName("failed")},
        {QStringLiteral("id"), QString::fromStdString(id)},
        {QStringLiteral("reason"), QString::number(errorCode)},
    });
    resetTransfer();
  } else {
    std::string id = ProtocolUtil::readBoundedString(m_stream, kTransferIdMaximum);
    if (isTerminalTransferId(id)) {
      return ParseResult::Handled;
    }
    if (!isValidTransferId(id) || id != m_transferId) {
      throw BadClientException("Unexpected file-transfer cancellation");
    }
    notify({{QStringLiteral("action"), actionName("cancelled")}, {QStringLiteral("id"), QString::fromStdString(id)}});
    resetTransfer();
  }

  return ParseResult::Handled;
}

bool FileTransferProtocol::handleLocalMessage(const QString &encodedMessage)
{
  QJsonObject message;
  if (!decodeMessage(encodedMessage, message)) {
    return false;
  }
  const QString action = message.value(QStringLiteral("action")).toString();
  if (action == QStringLiteral("refresh")) {
    if (m_connected) {
      notify({
          {QStringLiteral("action"), actionName("availability")},
          {QStringLiteral("available"), m_peerAuthenticated},
          {QStringLiteral("peer"), m_peerName},
      });
    }
    return true;
  }
  if (!isAuthenticatedFileMessage() || !m_connected) {
    notify({{QStringLiteral("action"), actionName("failed")}, {QStringLiteral("reason"), actionName("untrustedPeer")}});
    return false;
  }

  const QString idText = message.value(QStringLiteral("id")).toString();
  const std::string id = idText.toStdString();
  if (action == QStringLiteral("sendOffer")) {
    const auto fileName = message.value(QStringLiteral("name")).toString();
    bool sizeOk = false;
    const uint64_t size = message.value(QStringLiteral("size")).toString().toULongLong(&sizeOk);
    const QByteArray hash = QByteArray::fromHex(message.value(QStringLiteral("sha256")).toString().toLatin1());
    if (m_state != State::Idle || !isValidTransferId(id) || !sizeOk || size > FileTransferReceiver::kMaximumFileSize ||
        hash.size() != static_cast<int>(kSha256Length) || !FileTransferReceiver::isSafeFileName(fileName)) {
      return false;
    }
    m_transferId = id;
    m_expectedSize = size;
    m_expectedHash = hash.toStdString();
    m_fileName = fileName.toUtf8().toStdString();
    m_sentSize = 0;
    m_pendingChunkSize = 0;
    m_sending = true;
    m_state = State::AwaitingOfferResponse;
    const uint32_t sizeHigh = static_cast<uint32_t>(size >> 32);
    const uint32_t sizeLow = static_cast<uint32_t>(size & 0xffffffffU);
    const std::string idCopy = m_transferId;
    const std::string hashCopy = m_expectedHash;
    const std::string nameCopy = m_fileName;
    ProtocolUtil::writef(m_stream, kMsgFileOffer, &idCopy, sizeHigh, sizeLow, &hashCopy, &nameCopy);
    return true;
  }

  if (!isValidTransferId(id) || id != m_transferId) {
    return false;
  }
  if (action == QStringLiteral("accept")) {
    if (m_state != State::AwaitingLocalOfferDecision) {
      return false;
    }
    const std::string idCopy = m_transferId;
    const uint32_t accepted = 1;
    ProtocolUtil::writef(m_stream, kMsgFileResponse, &idCopy, accepted);
    m_state = State::Receiving;
    return true;
  }
  if (action == QStringLiteral("reject")) {
    if (m_state != State::AwaitingLocalOfferDecision) {
      return false;
    }
    const std::string idCopy = m_transferId;
    const uint32_t rejected = 0;
    ProtocolUtil::writef(m_stream, kMsgFileResponse, &idCopy, rejected);
    resetTransfer();
    return true;
  }
  if (action == QStringLiteral("sendChunk")) {
    if (!m_sending || m_state != State::SenderReadyForChunk) {
      return false;
    }
    const QByteArray chunk = QByteArray::fromBase64(
        message.value(QStringLiteral("data")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors
    );
    if (chunk.isEmpty() || chunk.size() > FileTransferReceiver::kMaximumChunkSize ||
        static_cast<uint64_t>(chunk.size()) > m_expectedSize - m_sentSize) {
      return false;
    }
    const std::string idCopy = m_transferId;
    const std::string data = chunk.toStdString();
    m_pendingChunkSize = static_cast<uint32_t>(chunk.size());
    m_state = State::AwaitingChunkAck;
    ProtocolUtil::writef(m_stream, kMsgFileChunk, &idCopy, &data);
    return true;
  }
  if (action == QStringLiteral("chunkSaved")) {
    if (m_sending || m_state != State::AwaitingLocalChunkCommit) {
      return false;
    }
    const auto bytes = message.value(QStringLiteral("bytes")).toString().toULongLong();
    if (bytes != m_pendingChunkSize) {
      return false;
    }
    m_receivedSize += m_pendingChunkSize;
    m_pendingChunkSize = 0;
    const uint32_t sizeHigh = static_cast<uint32_t>(m_receivedSize >> 32);
    const uint32_t sizeLow = static_cast<uint32_t>(m_receivedSize & 0xffffffffU);
    const std::string idCopy = m_transferId;
    ProtocolUtil::writef(m_stream, kMsgFileAck, &idCopy, sizeHigh, sizeLow);
    m_state = State::Receiving;
    return true;
  }
  if (action == QStringLiteral("sendFinish")) {
    if (!m_sending || m_state != State::SenderReadyForChunk || m_sentSize != m_expectedSize) {
      return false;
    }
    const std::string idCopy = m_transferId;
    ProtocolUtil::writef(m_stream, kMsgFileFinish, &idCopy);
    m_state = State::AwaitingRemoteFinalResult;
    return true;
  }
  if (action == QStringLiteral("receiveResult")) {
    if (m_sending || m_state != State::AwaitingLocalFinish) {
      return false;
    }
    const bool success = message.value(QStringLiteral("success")).toBool();
    const std::string idCopy = m_transferId;
    if (success) {
      ProtocolUtil::writef(m_stream, kMsgFileDone, &idCopy);
    } else {
      const uint32_t errorCode = 1;
      ProtocolUtil::writef(m_stream, kMsgFileError, &idCopy, errorCode);
    }
    notify({
        {QStringLiteral("action"), success ? actionName("completed") : actionName("failed")},
        {QStringLiteral("id"), QString::fromStdString(idCopy)},
    });
    resetTransfer();
    return true;
  }
  if (action == QStringLiteral("cancel")) {
    if (m_state == State::Idle) {
      return false;
    }
    const std::string idCopy = m_transferId;
    ProtocolUtil::writef(m_stream, kMsgFileCancel, &idCopy);
    notify({{QStringLiteral("action"), actionName("cancelled")}, {QStringLiteral("id"), QString::fromStdString(idCopy)}});
    resetTransfer();
    return true;
  }
  return false;
}

void FileTransferProtocol::setConnected(bool connected)
{
  if (m_connected == connected) {
    return;
  }
  m_connected = connected;
  if (m_peerAuthenticated) {
    notify({
        {QStringLiteral("action"), actionName("availability")},
        {QStringLiteral("available"), connected},
        {QStringLiteral("peer"), m_peerName},
    });
  }
  if (!connected && m_state != State::Idle) {
    notify({{QStringLiteral("action"), actionName("failed")}, {QStringLiteral("reason"), actionName("disconnected")}});
    resetTransfer();
  }
}

bool FileTransferProtocol::isPeerAuthenticated() const
{
  return m_peerAuthenticated;
}

QString FileTransferProtocol::encodeMessage(const QJsonObject &message)
{
  const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool FileTransferProtocol::decodeMessage(const QString &encoded, QJsonObject &message)
{
  if (encoded.isEmpty() || encoded.size() > 48 * 1024) {
    return false;
  }
  const auto json = QByteArray::fromBase64(
      encoded.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors
  );
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  message = document.object();
  return true;
}

void FileTransferProtocol::notify(QJsonObject message) const
{
  if (!m_notify) {
    return;
  }
  message.insert(QStringLiteral("peer"), m_peerName);
  m_notify(encodeMessage(message));
}

void FileTransferProtocol::resetTransfer()
{
  if (!m_transferId.empty()) {
    if (!isTerminalTransferId(m_transferId)) {
      m_terminalTransferIds.push_back(m_transferId);
      if (m_terminalTransferIds.size() > 8) {
        m_terminalTransferIds.pop_front();
      }
    }
  }
  m_state = State::Idle;
  m_transferId.clear();
  m_fileName.clear();
  m_expectedHash.clear();
  m_expectedSize = 0;
  m_sentSize = 0;
  m_receivedSize = 0;
  m_pendingChunkSize = 0;
  m_sending = false;
}

bool FileTransferProtocol::isTerminalTransferId(const std::string &id) const
{
  return std::find(m_terminalTransferIds.begin(), m_terminalTransferIds.end(), id) != m_terminalTransferIds.end();
}

bool FileTransferProtocol::isValidTransferId(const std::string &id) const
{
  if (id.size() != kTransferIdMaximum) {
    return false;
  }
  for (size_t i = 0; i < id.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (id[i] != '-') {
        return false;
      }
    } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool FileTransferProtocol::isAuthenticatedFileMessage() const
{
  return m_peerAuthenticated;
}
