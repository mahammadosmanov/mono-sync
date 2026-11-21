/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/HandOffPaths.h"

#include <QFileInfo>

namespace {
constexpr qsizetype kHandOffMaxPaths = 100;
}

QStringList parseHandOffPaths(const QByteArray &data)
{
  QStringList paths;
  if (data.size() > kHandOffMaxBytes)
    return paths;

  for (const auto &line : data.split('\n')) {
    if (paths.size() >= kHandOffMaxPaths)
      break;
    const auto path = QString::fromUtf8(line.endsWith('\r') ? line.chopped(1) : line);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isAbsolute() || !info.isFile())
      continue;
    paths.append(path);
  }
  return paths;
}
