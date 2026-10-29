/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolUtil.h"
#include "base/Log.h"
#include "io/IStream.h"

#include <QTest>

#include <algorithm>
#include <cstring>
#include <deque>

namespace {

class MemoryStream : public deskflow::IStream
{
public:
  explicit MemoryStream(std::string bytes) : m_bytes(std::move(bytes))
  {
  }

  void close() override
  {
    m_closed = true;
  }
  uint32_t read(void *buffer, uint32_t count) override
  {
    if (m_closed || m_offset == m_bytes.size() || count == 0) {
      return 0;
    }
    const auto available = static_cast<uint32_t>(m_bytes.size() - m_offset);
    const auto copied = std::min(count, available);
    if (buffer != nullptr) {
      std::memcpy(buffer, m_bytes.data() + m_offset, copied);
    }
    m_offset += copied;
    return copied;
  }
  void write(const void *, uint32_t) override
  {
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
    close();
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<MemoryStream *>(this);
  }
  bool isReady() const override
  {
    return !m_closed && m_offset < m_bytes.size();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_bytes.size() - m_offset);
  }

private:
  std::string m_bytes;
  size_t m_offset = 0;
  bool m_closed = false;
};

std::string frame(std::string payload)
{
  std::string result(4, '\0');
  const auto size = static_cast<uint32_t>(payload.size());
  result[0] = static_cast<char>((size >> 24) & 0xff);
  result[1] = static_cast<char>((size >> 16) & 0xff);
  result[2] = static_cast<char>((size >> 8) & 0xff);
  result[3] = static_cast<char>(size & 0xff);
  result.append(payload);
  return result;
}

} // namespace

class ProtocolBoundedStringTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void readsAtLimit();
  void readsEmptyString();
  void rejectsLengthBeforePayloadAllocation();

private:
  Log m_log;
};

void ProtocolBoundedStringTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Debug);
}

void ProtocolBoundedStringTests::readsAtLimit()
{
  MemoryStream stream(frame(std::string(32 * 1024, 'a')));
  QCOMPARE(ProtocolUtil::readBoundedString(&stream, 32 * 1024).size(), 32 * 1024);
}

void ProtocolBoundedStringTests::readsEmptyString()
{
  MemoryStream stream(frame({}));
  QVERIFY(ProtocolUtil::readBoundedString(&stream, 32 * 1024).empty());
}

void ProtocolBoundedStringTests::rejectsLengthBeforePayloadAllocation()
{
  MemoryStream stream(frame(std::string(32 * 1024 + 1, 'a')));
  QVERIFY_THROWS_EXCEPTION(BadClientException, ProtocolUtil::readBoundedString(&stream, 32 * 1024));
  QCOMPARE(stream.getSize(), static_cast<uint32_t>(32 * 1024 + 1));
}

QTEST_APPLESS_MAIN(ProtocolBoundedStringTests)

#include "ProtocolBoundedStringTests.moc"
