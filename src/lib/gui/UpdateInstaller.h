/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

/**
 * Installs a verified release package after this process exits. Nothing from
 * the downloaded package runs before it is installed.
 */
namespace UpdateInstaller {

/**
 * \brief Quotes text as one POSIX shell word
 */
QString shellQuote(const QString &text);

/**
 * \brief Starts a detached helper that waits for this process to exit, installs \a package and
 * relaunches Mono Sync. The caller must quit right after a true return.
 * \return false with a user-facing \a error when nothing was started
 */
bool launch(const QString &package, QString &error);

/**
 * \brief Shows \a path in Finder or Explorer
 */
void reveal(const QString &path);

} // namespace UpdateInstaller
