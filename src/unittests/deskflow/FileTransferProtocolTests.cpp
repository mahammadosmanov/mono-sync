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
  void singleFileMessageRoundTripUsesExistingPeerStream();

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

QTEST_APPLESS_MAIN(FileTransferProtocolTests)

#include "FileTransferProtocolTests.moc"
