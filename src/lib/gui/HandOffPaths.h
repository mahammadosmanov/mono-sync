/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QStringList>

// Largest hand-off payload accepted from a second launch, in bytes.
inline constexpr qsizetype kHandOffMaxBytes = 64 * 1024;

// Parses newline-separated absolute paths of existing regular files sent by a second launch.
// Returns an empty list when data is oversized. Keeps at most 100 valid paths.
QStringList parseHandOffPaths(const QByteArray &data);
