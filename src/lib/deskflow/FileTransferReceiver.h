/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <cstdint>

/**
 * Receives one explicitly accepted file into a user-selected directory.
 * Data is staged beside the destination and only published after size and hash
 * verification. Existing destination files are never replaced.
 */
class FileTransferReceiver
{
public:
  // JSON and base64 wrap chunks across local IPC; 24 KiB fits its 48 KiB frame cap.
  static constexpr qsizetype kMaximumChunkSize = 24 * 1024;
  static constexpr quint64 kMaximumFileSize = 10ULL * 1024 * 1024 * 1024;

  FileTransferReceiver(QString destinationDirectory, QString fileName, quint64 expectedSize,
                       QByteArray expectedSha256);
  ~FileTransferReceiver();

  FileTransferReceiver(const FileTransferReceiver &) = delete;
  FileTransferReceiver &operator=(const FileTransferReceiver &) = delete;

  bool begin(QString *error = nullptr);
  bool appendChunk(const QByteArray &chunk, QString *error = nullptr);
  bool finish(QString *error = nullptr);
  void cancel();

  quint64 receivedBytes() const;
  quint64 expectedBytes() const;
  QString destinationPath() const;
  bool isActive() const;
  bool isComplete() const;
  static bool isSafeFileName(const QString &fileName);

private:
  bool fail(const QString &message, QString *error);

  QString m_destinationDirectory;
  QString m_fileName;
  QString m_destinationPath;
  QString m_temporaryPath;
  quint64 m_expectedSize;
  quint64 m_receivedSize = 0;
  QByteArray m_expectedSha256;
  QCryptographicHash m_hash{QCryptographicHash::Sha256};
  QFile m_file;
  bool m_active = false;
  bool m_complete = false;
};
