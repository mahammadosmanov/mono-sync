/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OutgoingFileWorker.h"

#include "deskflow/FileTransferReceiver.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QThread>

#include <algorithm>

OutgoingFileWorker::OutgoingFileWorker(QObject *parent) : QObject(parent), m_file(this)
{
}

void OutgoingFileWorker::requestCancel()
{
  m_cancelled.store(true, std::memory_order_release);
}

void OutgoingFileWorker::prepare(const QString &path)
{
  Q_ASSERT(QThread::currentThread() == thread());
  const QFileInfo info(path);
  if (!info.isFile() || info.size() < 0 ||
      static_cast<quint64>(info.size()) > FileTransferReceiver::kMaximumFileSize ||
      !FileTransferReceiver::isSafeFileName(info.fileName())) {
    Q_EMIT failed(QStringLiteral("The selected file is unavailable, has an unsafe name, or exceeds 10 GiB."));
    return;
  }

  m_size = static_cast<quint64>(info.size());
  m_file.setFileName(path);
  if (!m_file.open(QIODevice::ReadOnly)) {
    Q_EMIT failed(QStringLiteral("The selected file could not be opened."));
    return;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  quint64 processed = 0;
  quint64 reported = 0;
  constexpr qint64 kReadBlockSize = 256 * 1024;
  constexpr quint64 kProgressInterval = 8 * 1024 * 1024;
  while (!m_cancelled.load(std::memory_order_acquire)) {
    const QByteArray block = m_file.read(kReadBlockSize);
    if (block.isEmpty()) {
      if (!m_file.atEnd()) {
        Q_EMIT failed(QStringLiteral("The selected file could not be read."));
        return;
      }
      break;
    }
    const auto blockSize = static_cast<quint64>(block.size());
    if (processed > m_size || blockSize > m_size - processed) {
      Q_EMIT failed(QStringLiteral("The selected file changed while it was being prepared."));
      return;
    }
    hash.addData(block);
    processed += blockSize;
    if (processed - reported >= kProgressInterval || processed == m_size) {
      Q_EMIT hashingProgress(processed, m_size);
      reported = processed;
    }
  }
  if (m_cancelled.load(std::memory_order_acquire)) {
    return;
  }
  if (processed != m_size || m_file.size() != static_cast<qint64>(m_size) || !m_file.seek(0)) {
    Q_EMIT failed(QStringLiteral("The selected file changed while it was being prepared."));
    return;
  }
  Q_EMIT prepared(info.fileName(), m_size, hash.result());
}

void OutgoingFileWorker::readNextChunk()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_cancelled.load(std::memory_order_acquire)) {
    return;
  }
  const auto position = static_cast<quint64>(m_file.pos());
  if (position >= m_size || m_file.size() != static_cast<qint64>(m_size)) {
    Q_EMIT failed(QStringLiteral("The selected file changed during transfer."));
    return;
  }
  const auto remaining = m_size - position;
  const auto requested = static_cast<qint64>(std::min<quint64>(remaining, FileTransferReceiver::kMaximumChunkSize));
  const QByteArray chunk = m_file.read(requested);
  if (chunk.isEmpty()) {
    Q_EMIT failed(QStringLiteral("The selected file changed or could not be read during transfer."));
    return;
  }
  if (static_cast<quint64>(chunk.size()) > remaining ||
      m_file.size() != static_cast<qint64>(m_size)) {
    Q_EMIT failed(QStringLiteral("The selected file changed during transfer."));
    return;
  }
  Q_EMIT chunkReady(chunk, static_cast<quint64>(m_file.pos()));
}

void OutgoingFileWorker::beginReceive(const QString &directory, const QString &fileName, quint64 size,
                                      const QByteArray &sha256)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_cancelled.load(std::memory_order_acquire) || m_receiver != nullptr) {
    return;
  }
  m_receiver = std::make_unique<FileTransferReceiver>(directory, fileName, size, sha256);
  QString error;
  const bool success = m_receiver->begin(&error);
  if (!success) {
    m_receiver.reset();
  }
  Q_EMIT receiverReady(success, error);
}

void OutgoingFileWorker::appendReceivedChunk(const QByteArray &chunk)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_cancelled.load(std::memory_order_acquire) || m_receiver == nullptr) {
    return;
  }
  QString error;
  const bool success = m_receiver->appendChunk(chunk, &error);
  const quint64 received = success ? m_receiver->receivedBytes() : 0;
  const quint64 total = m_receiver->expectedBytes();
  Q_EMIT receiveChunkSaved(success, static_cast<quint64>(chunk.size()), received, total, error);
  if (!success) {
    m_receiver.reset();
  }
}

void OutgoingFileWorker::finishReceive()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_cancelled.load(std::memory_order_acquire) || m_receiver == nullptr) {
    return;
  }
  QString error;
  const QString path = m_receiver->destinationPath();
  const bool success = m_receiver->finish(&error);
  m_receiver.reset();
  Q_EMIT receiveFinished(success, success ? path : QString(), error);
}
