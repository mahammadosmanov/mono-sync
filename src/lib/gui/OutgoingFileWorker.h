/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QFile>
#include <QObject>

#include "deskflow/FileTransferReceiver.h"

#include <atomic>
#include <memory>

class OutgoingFileWorker : public QObject
{
  Q_OBJECT

public:
  explicit OutgoingFileWorker(QObject *parent = nullptr);
  void requestCancel();

public Q_SLOTS:
  void prepare(const QString &path);
  void readNextChunk();
  void beginReceive(const QString &directory, const QString &fileName, quint64 size, const QByteArray &sha256);
  void appendReceivedChunk(const QByteArray &chunk);
  void finishReceive();

Q_SIGNALS:
  void hashingProgress(quint64 processed, quint64 total);
  void prepared(const QString &fileName, quint64 size, const QByteArray &sha256);
  void chunkReady(const QByteArray &chunk, quint64 position);
  void receiverReady(bool success, const QString &error);
  void receiveChunkSaved(bool success, quint64 bytes, quint64 received, quint64 total, const QString &error);
  void receiveFinished(bool success, const QString &path, const QString &error);
  void failed(const QString &message);

private:
  QFile m_file;
  std::atomic_bool m_cancelled{false};
  quint64 m_size = 0;
  std::unique_ptr<FileTransferReceiver> m_receiver;
};
