/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferReceiver.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

std::filesystem::path toFilesystemPath(const QString &path)
{
  const QByteArray utf8 = path.toUtf8();
  std::u8string encoded;
  encoded.resize(static_cast<size_t>(utf8.size()));
  std::transform(utf8.cbegin(), utf8.cend(), encoded.begin(), [](char byte) {
    return static_cast<char8_t>(static_cast<unsigned char>(byte));
  });
  return std::filesystem::path(encoded);
}

} // namespace

FileTransferReceiver::FileTransferReceiver(
    QString destinationDirectory, QString fileName, quint64 expectedSize, QByteArray expectedSha256
)
    : m_destinationDirectory(std::move(destinationDirectory)),
      m_fileName(std::move(fileName)),
      m_expectedSize(expectedSize),
      m_expectedSha256(std::move(expectedSha256))
{
}

FileTransferReceiver::~FileTransferReceiver()
{
  cancel();
}

bool FileTransferReceiver::begin(QString *error)
{
  if (m_active || m_complete) {
    return fail(QStringLiteral("transfer has already started"), error);
  }
  if (!isSafeFileName(m_fileName)) {
    return fail(QStringLiteral("the offered file name is not a safe file name"), error);
  }
  if (m_expectedSha256.size() != 32) {
    return fail(QStringLiteral("the offered SHA-256 digest is invalid"), error);
  }
  if (m_expectedSize > kMaximumFileSize) {
    return fail(QStringLiteral("the offered file exceeds the 10 GiB limit"), error);
  }

  const QFileInfo selectedDirectory(m_destinationDirectory);
  const QString canonicalDirectory = selectedDirectory.canonicalFilePath();
  if (canonicalDirectory.isEmpty() || !selectedDirectory.isDir() || !selectedDirectory.isWritable()) {
    return fail(QStringLiteral("the selected destination is not a writable directory"), error);
  }

  QStorageInfo storage(canonicalDirectory);
  storage.refresh();
  if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < 0 ||
      static_cast<quint64>(storage.bytesAvailable()) < m_expectedSize) {
    return fail(QStringLiteral("the selected destination does not have enough available space"), error);
  }

  m_destinationDirectory = canonicalDirectory;
  m_destinationPath = QDir(canonicalDirectory).filePath(m_fileName);
  if (QFileInfo::exists(m_destinationPath)) {
    return fail(QStringLiteral("a file with this name already exists; choose another name"), error);
  }

  const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_temporaryPath = QDir(canonicalDirectory).filePath(QStringLiteral(".monosync-%1.part").arg(suffix));
  m_file.setFileName(m_temporaryPath);
  if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    m_temporaryPath.clear();
    return fail(QStringLiteral("could not create a private temporary file in the selected destination"), error);
  }
  if (!m_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    cancel();
    return fail(QStringLiteral("could not restrict access to the temporary file"), error);
  }

  m_hash.reset();
  m_receivedSize = 0;
  m_active = true;
  return true;
}

bool FileTransferReceiver::appendChunk(const QByteArray &chunk, QString *error)
{
  if (!m_active) {
    return fail(QStringLiteral("there is no active file transfer"), error);
  }
  if (chunk.isEmpty() || chunk.size() > kMaximumChunkSize) {
    cancel();
    return fail(QStringLiteral("the received file chunk has an invalid size"), error);
  }
  const auto chunkSize = static_cast<quint64>(chunk.size());
  if (chunkSize > m_expectedSize - m_receivedSize) {
    cancel();
    return fail(QStringLiteral("the received data exceeds the offered file size"), error);
  }

  if (m_file.write(chunk) != chunk.size()) {
    cancel();
    return fail(QStringLiteral("could not write the received file data"), error);
  }
  m_hash.addData(QByteArrayView(chunk));
  m_receivedSize += chunkSize;
  return true;
}

bool FileTransferReceiver::finish(QString *error)
{
  if (!m_active) {
    return fail(QStringLiteral("there is no active file transfer"), error);
  }
  if (m_receivedSize != m_expectedSize) {
    cancel();
    return fail(QStringLiteral("the received file size does not match the offer"), error);
  }
  if (m_hash.result() != m_expectedSha256) {
    cancel();
    return fail(QStringLiteral("the received file failed its SHA-256 check"), error);
  }
  if (!m_file.flush()) {
    cancel();
    return fail(QStringLiteral("could not flush the received file to disk"), error);
  }
  m_file.close();

  // Hard-link creation is atomic and fails if the destination appeared after
  // the initial check. Both paths share a directory, so cross-device links are
  // not involved. Unsupported destination filesystems fail closed.
  const auto temporaryPath = toFilesystemPath(m_temporaryPath);
  const auto destinationPath = toFilesystemPath(m_destinationPath);
  std::error_code linkError;
  std::filesystem::create_hard_link(temporaryPath, destinationPath, linkError);
  if (linkError) {
    cancel();
    if (linkError == std::errc::file_exists) {
      return fail(QStringLiteral("a file with this name appeared during transfer; the existing file was kept"), error);
    }
    return fail(QStringLiteral("the selected destination does not support safe atomic file finalization"), error);
  }

  QFile::remove(m_temporaryPath);
  m_temporaryPath.clear();
  m_active = false;
  m_complete = true;
  return true;
}

void FileTransferReceiver::cancel()
{
  if (m_file.isOpen()) {
    m_file.close();
  }
  if (!m_temporaryPath.isEmpty()) {
    QFile::remove(m_temporaryPath);
    m_temporaryPath.clear();
  }
  m_active = false;
}

quint64 FileTransferReceiver::receivedBytes() const
{
  return m_receivedSize;
}

quint64 FileTransferReceiver::expectedBytes() const
{
  return m_expectedSize;
}

QString FileTransferReceiver::destinationPath() const
{
  return m_destinationPath;
}

bool FileTransferReceiver::isActive() const
{
  return m_active;
}

bool FileTransferReceiver::isComplete() const
{
  return m_complete;
}

bool FileTransferReceiver::fail(const QString &message, QString *error)
{
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool FileTransferReceiver::isSafeFileName(const QString &fileName)
{
  if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..") ||
      fileName.toUtf8().size() > 240 || fileName.endsWith(QLatin1Char('.')) || fileName.endsWith(QLatin1Char(' '))) {
    return false;
  }

  for (const QChar character : fileName) {
    if (character.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(character)) {
      return false;
    }
  }

  const QString stem = fileName.section(QLatin1Char('.'), 0, 0).toUpper();
  if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN") || stem == QStringLiteral("AUX") ||
      stem == QStringLiteral("NUL")) {
    return false;
  }
  for (int deviceNumber = 1; deviceNumber <= 9; ++deviceNumber) {
    if (stem == QStringLiteral("COM%1").arg(deviceNumber) || stem == QStringLiteral("LPT%1").arg(deviceNumber)) {
      return false;
    }
  }
  return true;
}
