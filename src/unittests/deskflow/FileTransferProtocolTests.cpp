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
#include <QTest>

#include <algorithm>
#include <cstring>

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

QTEST_APPLESS_MAIN(FileTransferProtocolTests)

#include "FileTransferProtocolTests.moc"
