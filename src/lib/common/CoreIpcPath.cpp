/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcPath.h"

#include "common/Constants.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN

QString coreIpcServerName()
{
  return QString::fromLatin1(kCoreIpcName);
}

bool preparePrivateCoreIpcDirectory()
{
  return true;
}

bool isPrivateCoreIpcDirectoryTrusted()
{
  return true;
}

#else

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

namespace {

QString privateDirectory()
{
  QString runtimeDirectory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtimeDirectory.isEmpty()) {
    runtimeDirectory = QDir::tempPath();
  }
  return QDir(runtimeDirectory).filePath(QStringLiteral("mono-sync-ipc-%1").arg(static_cast<qulonglong>(getuid())));
}

bool isPrivateOwnedDirectory(const QString &path)
{
  const QByteArray encodedPath = QFile::encodeName(path);
  struct stat info
  {
  };
  return lstat(encodedPath.constData(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
         (info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

} // namespace

QString coreIpcServerName()
{
  return QDir(privateDirectory()).filePath(QStringLiteral("core.sock"));
}

bool preparePrivateCoreIpcDirectory()
{
  const QString path = privateDirectory();
  const QByteArray encodedPath = QFile::encodeName(path);
  if (mkdir(encodedPath.constData(), S_IRWXU) != 0) {
    struct stat existing
    {
    };
    if (errno != EEXIST || lstat(encodedPath.constData(), &existing) != 0 || !S_ISDIR(existing.st_mode) ||
        existing.st_uid != geteuid()) {
      return false;
    }
    if (chmod(encodedPath.constData(), S_IRWXU) != 0) {
      return false;
    }
  }
  return isPrivateOwnedDirectory(path);
}

bool isPrivateCoreIpcDirectoryTrusted()
{
  return isPrivateOwnedDirectory(privateDirectory());
}

#endif
