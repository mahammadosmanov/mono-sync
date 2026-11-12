/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/FileTransferProtocol.h"
#include "deskflow/FileTransferReceiver.h"
#include "io/IStream.h"

#include <QJsonObject>
#include <QCryptographicHash>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <string>

#include <memory>

namespace {

class MemoryStream : public deskflow::IStream
{
public:
  explicit MemoryStream(std::string input = {}) : m_input(std::move(input))
  {
  }
  void close() override { m_closed = true; }
  uint32_t read(void *buffer, uint32_t count) override
  {
    if (m_closed || count == 0 || m_offset == m_input.size()) {
      return 0;
    }
    const auto amount = std::min(count, static_cast<uint32_t>(m_input.size() - m_offset));
    std::memcpy(buffer, m_input.data() + m_offset, amount);
    m_offset += amount;
    return amount;
  }
  void write(const void *buffer, uint32_t count) override
  {
    m_output.append(static_cast<const char *>(buffer), count);
  }
  void flush() override {}
  void shutdownInput() override { close(); }
  void shutdownOutput() override {}
  void *getEventTarget() const override { return const_cast<MemoryStream *>(this); }
  bool isReady() const override { return !m_closed && m_offset < m_input.size(); }
  uint32_t getSize() const override { return static_cast<uint32_t>(m_input.size() - m_offset); }
  const std::string &output() const { return m_output; }

private:
  std::string m_input;
  size_t m_offset = 0;
  std::string m_output;
  bool m_closed = false;
};

struct DuplexState
{
  std::string input[2];
};

class DuplexStream : public deskflow::IStream
{
public:
  DuplexStream(std::shared_ptr<DuplexState> state, int side) : m_state(std::move(state)), m_side(side) {}
  void close() override { m_closed = true; }
  uint32_t read(void *buffer, uint32_t count) override
  {
    if (m_closed || count == 0 || m_state->input[m_side].empty()) {
      return 0;
    }
    const auto amount = std::min(count, static_cast<uint32_t>(m_state->input[m_side].size()));
    std::memcpy(buffer, m_state->input[m_side].data(), amount);
    m_state->input[m_side].erase(0, amount);
    return amount;
  }
  void write(const void *buffer, uint32_t count) override
  {
    m_state->input[1 - m_side].append(static_cast<const char *>(buffer), count);
  }
  void flush() override {}
  void shutdownInput() override { close(); }
  void shutdownOutput() override {}
  void *getEventTarget() const override { return const_cast<DuplexStream *>(this); }
  bool isReady() const override { return !m_closed && !m_state->input[m_side].empty(); }
  uint32_t getSize() const override { return static_cast<uint32_t>(m_state->input[m_side].size()); }

private:
  std::shared_ptr<DuplexState> m_state;
  int m_side;
  bool m_closed = false;
};

bool deliverMessage(FileTransferProtocol &protocol, deskflow::IStream &stream)
{
  uint8_t code[4];
  return stream.read(code, sizeof(code)) == sizeof(code) &&
         protocol.parseMessage(code) == FileTransferProtocol::ParseResult::Handled;
}

void append32(std::string &bytes, uint32_t value)
{
  bytes.push_back(static_cast<char>((value >> 24) & 0xff));
  bytes.push_back(static_cast<char>((value >> 16) & 0xff));
  bytes.push_back(static_cast<char>((value >> 8) & 0xff));
  bytes.push_back(static_cast<char>(value & 0xff));
}

void appendString(std::string &bytes, const std::string &value)
{
  append32(bytes, static_cast<uint32_t>(value.size()));
  bytes.append(value);
}

std::string offerPayload()
{
  std::string bytes;
  appendString(bytes, "12345678-1234-1234-1234-123456789abc");
  append32(bytes, 0);
  append32(bytes, 1);
  appendString(bytes, std::string(32, 'h'));
  appendString(bytes, "sample.txt");
  return bytes;
}

std::string offerPayloadFor(const std::string &id, uint32_t size, const std::string &hash = std::string(32, 'h'))
{
  std::string bytes;
  appendString(bytes, id);
  append32(bytes, 0);
  append32(bytes, size);
  appendString(bytes, hash);
  appendString(bytes, "sample.txt");
  return bytes;
}

// Payload of an MSRP accepted response (the code bytes are passed to parseMessage separately).
std::string acceptedPayload(const std::string &id)
{
  std::string bytes;
  appendString(bytes, id);
  bytes.push_back('\x01');
  return bytes;
}

std::string ackPayload(const std::string &id, uint32_t acknowledged)
{
  std::string bytes;
  appendString(bytes, id);
  append32(bytes, 0);
  append32(bytes, acknowledged);
  return bytes;
}

std::string digestPayload(const std::string &id, const std::string &digest)
{
  std::string bytes;
  appendString(bytes, id);
  appendString(bytes, digest);
  return bytes;
}

std::string idPayload(const std::string &id)
{
  std::string bytes;
  appendString(bytes, id);
  return bytes;
}

std::string chunkPayload(const std::string &id, const std::string &data)
{
  std::string bytes;
  appendString(bytes, id);
  appendString(bytes, data);
  return bytes;
}

FileTransferProtocol::NotifyFunction recordNotices(QList<QJsonObject> &notices)
{
  return [&notices](const QString &encoded) {
    QJsonObject message;
    if (FileTransferProtocol::decodeMessage(encoded, message)) {
      notices.append(message);
    }
  };
}

int countActions(const QList<QJsonObject> &notices, const QString &action)
{
  return static_cast<int>(std::count_if(notices.cbegin(), notices.cend(), [&](const QJsonObject &notice) {
    return notice.value(QStringLiteral("action")).toString() == action;
  }));
}

const char kTransferId[] = "12345678-1234-1234-1234-123456789abc";

QString offerMessage(uint32_t size)
{
  return FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendOffer")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("name"), QStringLiteral("sample.txt")},
      {QStringLiteral("size"), QString::number(size)},
      {QStringLiteral("sha256"), QString::fromLatin1(QByteArray(32, '\x01').toHex())},
  });
}

// One byte, base64url of "a".
QString oneByteChunkMessage()
{
  return FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendChunk")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("data"), QStringLiteral("YQ")},
  });
}

QString acceptMessage()
{
  return FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("accept")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
  });
}

QString savedMessage(uint32_t bytes)
{
  return FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("chunkSaved")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("bytes"), QString::number(bytes)},
  });
}

const uint8_t kOfferCode[] = {'M', 'S', 'O', 'F'};
const uint8_t kResponseCode[] = {'M', 'S', 'R', 'P'};
const uint8_t kChunkCode[] = {'M', 'S', 'C', 'H'};
const uint8_t kAckCode[] = {'M', 'S', 'A', 'K'};

} // namespace

class FileTransferProtocolTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void inboundOfferRequiresExplicitAcceptanceBeforeChunks();
  void unauthenticatedPeersCannotOfferFiles();
  void simultaneousOfferIsRejectedWithoutDroppingPeer();
  void maximumChunkFitsLocalIpcLimit();
  void lateCancelAfterRejectIsIgnored();
  void fileMessagesAreRejectedBeforeHandshakeCompletion();
  void refreshReplaysAuthenticatedAvailability();
  void refreshReportsUnauthenticatedPeerAsUnavailable();
  void peerlessRefreshSelectsServerFanoutOnly();
  void singleFileMessageRoundTripUsesExistingPeerStream();
  void fastSenderKeepsThirtyTwoChunksInFlight();
  void fastSenderCumulativeAckFreesWindowSlots();
  void fastSenderAckMustLandOnChunkBoundary();
  void fastReceiverAcknowledgesSavedChunksCumulatively();
  void fastReceiverRejectsChunkBeyondWindow();
  void legacySenderRejectsSecondChunkBeforeAck();
  void legacyReceiverRejectsSecondChunkBeforeSave();
  void fastSenderFinishesWithStreamedDigest();
  void fastReceiverSurfacesDigestFromFinishHash();
  void fastReceiverRejectsPlainFinishAfterZeroDigest();
  void legacyReceiverRejectsDigestMessage();

private:
  Log m_log;
};

void FileTransferProtocolTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Debug);
}

void FileTransferProtocolTests::inboundOfferRequiresExplicitAcceptanceBeforeChunks()
{
  MemoryStream stream(offerPayload());
  QString notification;
  FileTransferProtocol protocol(&stream, true, QStringLiteral("peer"), [&](const QString &message) {
    notification = message;
  });
  protocol.setConnected(true);
  const uint8_t offerCode[] = {'M', 'S', 'O', 'F'};
  QCOMPARE(protocol.parseMessage(offerCode), FileTransferProtocol::ParseResult::Handled);

  QJsonObject offer;
  QVERIFY(FileTransferProtocol::decodeMessage(notification, offer));
  QCOMPARE(offer.value(QStringLiteral("action")).toString(), QStringLiteral("offer"));
  const QString id = offer.value(QStringLiteral("id")).toString();

  QJsonObject chunk{{QStringLiteral("action"), QStringLiteral("sendChunk")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("data"), QStringLiteral("YQ")}};
  QVERIFY(!protocol.handleLocalMessage(FileTransferProtocol::encodeMessage(chunk)));
  QVERIFY(stream.output().empty());

  QJsonObject accept{{QStringLiteral("action"), QStringLiteral("accept")}, {QStringLiteral("id"), id}};
  QVERIFY(protocol.handleLocalMessage(FileTransferProtocol::encodeMessage(accept)));
  QVERIFY(!stream.output().empty());
}

void FileTransferProtocolTests::unauthenticatedPeersCannotOfferFiles()
{
  MemoryStream stream(offerPayload());
  FileTransferProtocol protocol(&stream, false, QStringLiteral("peer"), {});
  const uint8_t offerCode[] = {'M', 'S', 'O', 'F'};
  QVERIFY_THROWS_EXCEPTION(BadClientException, protocol.parseMessage(offerCode));
}

void FileTransferProtocolTests::simultaneousOfferIsRejectedWithoutDroppingPeer()
{
  auto offers = offerPayload();
  offers += offerPayload();
  MemoryStream stream(std::move(offers));
  FileTransferProtocol protocol(&stream, true, QStringLiteral("peer"), {});
  protocol.setConnected(true);
  const uint8_t offerCode[] = {'M', 'S', 'O', 'F'};
  QCOMPARE(protocol.parseMessage(offerCode), FileTransferProtocol::ParseResult::Handled);
  QCOMPARE(protocol.parseMessage(offerCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(!stream.output().empty());
}

void FileTransferProtocolTests::maximumChunkFitsLocalIpcLimit()
{
  const QByteArray data(static_cast<qsizetype>(FileTransferReceiver::kMaximumChunkSize), 'x');
  const auto message = FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendChunk")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("data"), QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding |
                                                                  QByteArray::OmitTrailingEquals))},
  });
  QVERIFY(message.size() < 48 * 1024);
  QJsonObject decoded;
  QVERIFY(FileTransferProtocol::decodeMessage(message, decoded));
}

void FileTransferProtocolTests::lateCancelAfterRejectIsIgnored()
{
  auto input = offerPayload();
  appendString(input, "12345678-1234-1234-1234-123456789abc");
  MemoryStream stream(std::move(input));
  FileTransferProtocol protocol(&stream, true, QStringLiteral("peer"), {});
  protocol.setConnected(true);
  const uint8_t offerCode[] = {'M', 'S', 'O', 'F'};
  const uint8_t cancelCode[] = {'M', 'S', 'C', 'A'};
  QCOMPARE(protocol.parseMessage(offerCode), FileTransferProtocol::ParseResult::Handled);

  QJsonObject reject{{QStringLiteral("action"), QStringLiteral("reject")},
                     {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")}};
  QVERIFY(protocol.handleLocalMessage(FileTransferProtocol::encodeMessage(reject)));
  QCOMPARE(protocol.parseMessage(cancelCode), FileTransferProtocol::ParseResult::Handled);
}

void FileTransferProtocolTests::fileMessagesAreRejectedBeforeHandshakeCompletion()
{
  MemoryStream stream(offerPayload());
  FileTransferProtocol protocol(&stream, true, QStringLiteral("peer"), {});
  const uint8_t offerCode[] = {'M', 'S', 'O', 'F'};
  QVERIFY_THROWS_EXCEPTION(BadClientException, protocol.parseMessage(offerCode));
}

void FileTransferProtocolTests::refreshReplaysAuthenticatedAvailability()
{
  MemoryStream stream;
  QList<QJsonObject> notifications;
  FileTransferProtocol protocol(&stream, true, QStringLiteral("peer"), [&](const QString &encoded) {
    QJsonObject message;
    if (FileTransferProtocol::decodeMessage(encoded, message)) {
      notifications.append(message);
    }
  });
  protocol.setConnected(true);
  notifications.clear(); // Simulate GUI IPC attaching after the initial peer-ready event.

  const auto refresh = FileTransferProtocol::availabilityRefreshRequest();
  QVERIFY(protocol.handleLocalMessage(refresh));
  QCOMPARE(notifications.size(), 1);
  QCOMPARE(notifications.first().value(QStringLiteral("action")).toString(), QStringLiteral("availability"));
  QVERIFY(notifications.first().value(QStringLiteral("available")).toBool());
  QCOMPARE(notifications.first().value(QStringLiteral("peer")).toString(), QStringLiteral("peer"));
}

void FileTransferProtocolTests::refreshReportsUnauthenticatedPeerAsUnavailable()
{
  MemoryStream stream;
  QList<QJsonObject> notifications;
  FileTransferProtocol protocol(&stream, false, QStringLiteral("peer"), [&](const QString &encoded) {
    QJsonObject message;
    if (FileTransferProtocol::decodeMessage(encoded, message)) {
      notifications.append(message);
    }
  });
  protocol.setConnected(true);
  QVERIFY(notifications.isEmpty());

  const auto refresh = FileTransferProtocol::availabilityRefreshRequest();
  QVERIFY(protocol.handleLocalMessage(refresh));
  QCOMPARE(notifications.size(), 1);
  QCOMPARE(notifications.first().value(QStringLiteral("action")).toString(), QStringLiteral("availability"));
  QVERIFY(!notifications.first().value(QStringLiteral("available")).toBool());
}

void FileTransferProtocolTests::peerlessRefreshSelectsServerFanoutOnly()
{
  const auto refresh = FileTransferProtocol::availabilityRefreshRequest();
  QVERIFY(FileTransferProtocol::isPeerlessAvailabilityRefresh(refresh));

  const auto peerRefresh = FileTransferProtocol::encodeMessage(
      {{QStringLiteral("action"), QStringLiteral("refresh")}, {QStringLiteral("peer"), QStringLiteral("client")}}
  );
  QVERIFY(!FileTransferProtocol::isPeerlessAvailabilityRefresh(peerRefresh));

  const auto offer = FileTransferProtocol::encodeMessage(
      {{QStringLiteral("action"), QStringLiteral("sendOffer")}, {QStringLiteral("peer"), QStringLiteral("client")}}
  );
  QVERIFY(!FileTransferProtocol::isPeerlessAvailabilityRefresh(offer));
  QVERIFY(!FileTransferProtocol::isPeerlessAvailabilityRefresh(QStringLiteral("not-json")));
}

void FileTransferProtocolTests::singleFileMessageRoundTripUsesExistingPeerStream()
{
  auto state = std::make_shared<DuplexState>();
  DuplexStream senderStream(state, 0);
  DuplexStream receiverStream(state, 1);
  QString senderNotice;
  QString receiverNotice;
  FileTransferProtocol sender(&senderStream, true, QStringLiteral("peer"), [&](const QString &message) {
    senderNotice = message;
  });
  FileTransferProtocol receiver(&receiverStream, true, QStringLiteral("peer"), [&](const QString &message) {
    receiverNotice = message;
  });
  sender.setConnected(true);
  receiver.setConnected(true);

  const QByteArray content("abc");
  const auto digest = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
  const QString id = QStringLiteral("12345678-1234-1234-1234-123456789abc");
  QJsonObject offer{{QStringLiteral("action"), QStringLiteral("sendOffer")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), QStringLiteral("sample.txt")},
                    {QStringLiteral("size"), QStringLiteral("3")},
                    {QStringLiteral("sha256"), QString::fromLatin1(digest)}};
  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage(offer)));
  QVERIFY(deliverMessage(receiver, receiverStream));

  QJsonObject remoteOffer;
  QVERIFY(FileTransferProtocol::decodeMessage(receiverNotice, remoteOffer));
  QCOMPARE(remoteOffer.value(QStringLiteral("action")).toString(), QStringLiteral("offer"));
  QJsonObject accept{{QStringLiteral("action"), QStringLiteral("accept")}, {QStringLiteral("id"), id}};
  QVERIFY(receiver.handleLocalMessage(FileTransferProtocol::encodeMessage(accept)));
  QVERIFY(deliverMessage(sender, senderStream));

  QJsonObject chunk{{QStringLiteral("action"), QStringLiteral("sendChunk")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("data"), QStringLiteral("YWJj")}};
  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage(chunk)));
  QVERIFY(deliverMessage(receiver, receiverStream));
  QJsonObject receivedChunk;
  QVERIFY(FileTransferProtocol::decodeMessage(receiverNotice, receivedChunk));
  QCOMPARE(receivedChunk.value(QStringLiteral("action")).toString(), QStringLiteral("receiveChunk"));
  QCOMPARE(QByteArray::fromBase64(receivedChunk.value(QStringLiteral("data")).toString().toLatin1(),
                                  QByteArray::Base64UrlEncoding),
           content);

  QJsonObject saved{{QStringLiteral("action"), QStringLiteral("chunkSaved")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("bytes"), QStringLiteral("3")}};
  QVERIFY(receiver.handleLocalMessage(FileTransferProtocol::encodeMessage(saved)));
  QVERIFY(deliverMessage(sender, senderStream));
  QJsonObject ready;
  QVERIFY(FileTransferProtocol::decodeMessage(senderNotice, ready));
  QCOMPARE(ready.value(QStringLiteral("action")).toString(), QStringLiteral("sendReadyToFinish"));

  QJsonObject finish{{QStringLiteral("action"), QStringLiteral("sendFinish")}, {QStringLiteral("id"), id}};
  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage(finish)));
  QVERIFY(deliverMessage(receiver, receiverStream));
  QJsonObject finishNotice;
  QVERIFY(FileTransferProtocol::decodeMessage(receiverNotice, finishNotice));
  QCOMPARE(finishNotice.value(QStringLiteral("action")).toString(), QStringLiteral("finishReceive"));

  QJsonObject result{{QStringLiteral("action"), QStringLiteral("receiveResult")},
                     {QStringLiteral("id"), id},
                     {QStringLiteral("success"), true}};
  QVERIFY(receiver.handleLocalMessage(FileTransferProtocol::encodeMessage(result)));
  QVERIFY(deliverMessage(sender, senderStream));
  QJsonObject complete;
  QVERIFY(FileTransferProtocol::decodeMessage(senderNotice, complete));
  QCOMPARE(complete.value(QStringLiteral("action")).toString(), QStringLiteral("completed"));
}

void FileTransferProtocolTests::fastSenderKeepsThirtyTwoChunksInFlight()
{
  MemoryStream stream(acceptedPayload(kTransferId));
  QList<QJsonObject> notices;
  FileTransferProtocol sender(&stream, true, QStringLiteral("peer"), recordNotices(notices), true);
  sender.setConnected(true);
  QVERIFY(sender.handleLocalMessage(offerMessage(100)));
  QCOMPARE(sender.parseMessage(kResponseCode), FileTransferProtocol::ParseResult::Handled);

  const auto chunk = oneByteChunkMessage();
  for (int i = 0; i < 32; ++i) {
    QVERIFY(sender.handleLocalMessage(chunk));
  }
  QVERIFY(!sender.handleLocalMessage(chunk));
  // one request per send that still leaves room in the window
  QCOMPARE(countActions(notices, QStringLiteral("sendNextChunk")), 31);
}

void FileTransferProtocolTests::fastSenderCumulativeAckFreesWindowSlots()
{
  MemoryStream stream(acceptedPayload(kTransferId) + ackPayload(kTransferId, 3));
  QList<QJsonObject> notices;
  FileTransferProtocol sender(&stream, true, QStringLiteral("peer"), recordNotices(notices), true);
  sender.setConnected(true);
  QVERIFY(sender.handleLocalMessage(offerMessage(100)));
  QCOMPARE(sender.parseMessage(kResponseCode), FileTransferProtocol::ParseResult::Handled);

  const auto chunk = oneByteChunkMessage();
  for (int i = 0; i < 32; ++i) {
    QVERIFY(sender.handleLocalMessage(chunk));
  }
  QCOMPARE(sender.parseMessage(kAckCode), FileTransferProtocol::ParseResult::Handled);
  for (int i = 0; i < 3; ++i) {
    QVERIFY(sender.handleLocalMessage(chunk));
  }
  QVERIFY(!sender.handleLocalMessage(chunk));
}

void FileTransferProtocolTests::fastSenderAckMustLandOnChunkBoundary()
{
  // chunks of 1 and 2 bytes: boundaries are at 1 and 3, so an ack of 2 is invalid
  MemoryStream stream(acceptedPayload(kTransferId) + ackPayload(kTransferId, 2) + ackPayload(kTransferId, 3));
  FileTransferProtocol sender(&stream, true, QStringLiteral("peer"), {}, true);
  sender.setConnected(true);
  QVERIFY(sender.handleLocalMessage(offerMessage(100)));
  QCOMPARE(sender.parseMessage(kResponseCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(sender.handleLocalMessage(oneByteChunkMessage()));
  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendChunk")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("data"), QStringLiteral("YWI")},
  })));
  QVERIFY_THROWS_EXCEPTION(BadClientException, sender.parseMessage(kAckCode));
  // a valid cumulative ack after the rejected one still lands
  QCOMPARE(sender.parseMessage(kAckCode), FileTransferProtocol::ParseResult::Handled);
}

void FileTransferProtocolTests::fastReceiverAcknowledgesSavedChunksCumulatively()
{
  MemoryStream stream(offerPayloadFor(kTransferId, 100) + chunkPayload(kTransferId, "a") +
                      chunkPayload(kTransferId, "b") + chunkPayload(kTransferId, "c"));
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), {}, true);
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  for (int i = 0; i < 3; ++i) {
    QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  }
  for (uint32_t i = 1; i <= 3; ++i) {
    QVERIFY(receiver.handleLocalMessage(savedMessage(1)));
  }

  std::string expected;
  expected += "MSRP";
  expected += acceptedPayload(kTransferId);
  for (uint32_t total = 1; total <= 3; ++total) {
    expected += "MSAK";
    expected += ackPayload(kTransferId, total);
  }
  QVERIFY(stream.output() == expected);
}

void FileTransferProtocolTests::fastReceiverRejectsChunkBeyondWindow()
{
  std::string input = offerPayloadFor(kTransferId, 100);
  for (int i = 0; i < 33; ++i) {
    input += chunkPayload(kTransferId, "a");
  }
  MemoryStream stream(std::move(input));
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), {}, true);
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  for (int i = 0; i < 32; ++i) {
    QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  }
  QVERIFY_THROWS_EXCEPTION(BadClientException, receiver.parseMessage(kChunkCode));
}

void FileTransferProtocolTests::legacySenderRejectsSecondChunkBeforeAck()
{
  MemoryStream stream(acceptedPayload(kTransferId));
  QList<QJsonObject> notices;
  FileTransferProtocol sender(&stream, true, QStringLiteral("peer"), recordNotices(notices));
  sender.setConnected(true);
  QVERIFY(sender.handleLocalMessage(offerMessage(100)));
  QCOMPARE(sender.parseMessage(kResponseCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(sender.handleLocalMessage(oneByteChunkMessage()));
  QVERIFY(!sender.handleLocalMessage(oneByteChunkMessage()));
  QCOMPARE(countActions(notices, QStringLiteral("sendNextChunk")), 0);
}

void FileTransferProtocolTests::legacyReceiverRejectsSecondChunkBeforeSave()
{
  MemoryStream stream(offerPayloadFor(kTransferId, 100) + chunkPayload(kTransferId, "a") +
                      chunkPayload(kTransferId, "b"));
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), {});
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY_THROWS_EXCEPTION(BadClientException, receiver.parseMessage(kChunkCode));
}

void FileTransferProtocolTests::fastSenderFinishesWithStreamedDigest()
{
  const std::string digest = QCryptographicHash::hash("abc", QCryptographicHash::Sha256).toStdString();
  MemoryStream stream(acceptedPayload(kTransferId) + ackPayload(kTransferId, 3));
  QList<QJsonObject> notices;
  FileTransferProtocol sender(&stream, true, QStringLiteral("peer"), recordNotices(notices), true);
  sender.setConnected(true);
  QVERIFY(sender.handleLocalMessage(offerMessage(3)));
  QCOMPARE(sender.parseMessage(kResponseCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendChunk")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("data"), QStringLiteral("YWJj")},
  })));
  QCOMPARE(sender.parseMessage(kAckCode), FileTransferProtocol::ParseResult::Handled);
  QCOMPARE(countActions(notices, QStringLiteral("sendReadyToFinish")), 1);

  QVERIFY(sender.handleLocalMessage(FileTransferProtocol::encodeMessage({
      {QStringLiteral("action"), QStringLiteral("sendFinish")},
      {QStringLiteral("id"), QStringLiteral("12345678-1234-1234-1234-123456789abc")},
      {QStringLiteral("sha256"), QString::fromLatin1(QByteArray::fromStdString(digest).toHex())},
  })));
  // the offer carried a zero digest; the real one follows in MSFH
  QVERIFY(stream.output().find(std::string(32, '\0')) != std::string::npos);
  const std::string finish = "MSFH" + digestPayload(kTransferId, digest);
  QVERIFY(stream.output().size() >= finish.size());
  QVERIFY(stream.output().compare(stream.output().size() - finish.size(), finish.size(), finish) == 0);
}

void FileTransferProtocolTests::fastReceiverSurfacesDigestFromFinishHash()
{
  const std::string digest = QCryptographicHash::hash("abc", QCryptographicHash::Sha256).toStdString();
  MemoryStream stream(offerPayloadFor(kTransferId, 3, std::string(32, '\0')) + chunkPayload(kTransferId, "abc") +
                      digestPayload(kTransferId, digest));
  QList<QJsonObject> notices;
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), recordNotices(notices), true);
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(savedMessage(3)));
  const uint8_t finishCode[] = {'M', 'S', 'F', 'H'};
  QCOMPARE(receiver.parseMessage(finishCode), FileTransferProtocol::ParseResult::Handled);

  QCOMPARE(countActions(notices, QStringLiteral("finishReceive")), 1);
  const auto finish = notices.last();
  QCOMPARE(finish.value(QStringLiteral("action")).toString(), QStringLiteral("finishReceive"));
  QCOMPARE(finish.value(QStringLiteral("sha256")).toString(),
           QString::fromLatin1(QByteArray::fromStdString(digest).toHex()));
}

void FileTransferProtocolTests::fastReceiverRejectsPlainFinishAfterZeroDigest()
{
  MemoryStream stream(offerPayloadFor(kTransferId, 3, std::string(32, '\0')) + chunkPayload(kTransferId, "abc") +
                      idPayload(kTransferId));
  QList<QJsonObject> notices;
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), recordNotices(notices), true);
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(savedMessage(3)));
  const uint8_t finishCode[] = {'M', 'S', 'F', 'N'};
  QCOMPARE(receiver.parseMessage(finishCode), FileTransferProtocol::ParseResult::Handled);

  QCOMPARE(countActions(notices, QStringLiteral("finishReceive")), 0);
  QCOMPARE(countActions(notices, QStringLiteral("failed")), 1);
  std::string error = "MSER";
  appendString(error, kTransferId);
  error.push_back('\x01');
  QVERIFY(stream.output().size() >= error.size());
  QVERIFY(stream.output().compare(stream.output().size() - error.size(), error.size(), error) == 0);
}

void FileTransferProtocolTests::legacyReceiverRejectsDigestMessage()
{
  const std::string digest(32, '\x02');
  MemoryStream stream(offerPayloadFor(kTransferId, 3) + chunkPayload(kTransferId, "abc") +
                      digestPayload(kTransferId, digest));
  FileTransferProtocol receiver(&stream, true, QStringLiteral("peer"), {});
  receiver.setConnected(true);
  QCOMPARE(receiver.parseMessage(kOfferCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(acceptMessage()));
  QCOMPARE(receiver.parseMessage(kChunkCode), FileTransferProtocol::ParseResult::Handled);
  QVERIFY(receiver.handleLocalMessage(savedMessage(3)));
  const uint8_t finishCode[] = {'M', 'S', 'F', 'H'};
  QVERIFY_THROWS_EXCEPTION(BadClientException, receiver.parseMessage(finishCode));
}

QTEST_APPLESS_MAIN(FileTransferProtocolTests)

#include "FileTransferProtocolTests.moc"
