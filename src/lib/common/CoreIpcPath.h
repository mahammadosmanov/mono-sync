/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

QString coreIpcServerName();
bool preparePrivateCoreIpcDirectory();
bool isPrivateCoreIpcDirectoryTrusted();
