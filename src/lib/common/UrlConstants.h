/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. org.deskflow
const auto kOrgDomain = QStringLiteral("monosync.app");

const auto kUrlSourceQuery = QStringLiteral("source=gui");
const auto kUrlApp = QStringLiteral("https://github.com/mahammadosmanov/mono-sync");
const auto kUrlHelp = QStringLiteral("%1/tree/codex/deskflow-rebuild/docs").arg(kUrlApp);
const auto kUrlDownload = QStringLiteral("%1/releases").arg(kUrlApp);
const auto kUrlWiki = kUrlApp;
// Newest release, including pre-releases. GitHub's /releases/latest skips pre-releases.
const auto kUrlUpdateCheck = QStringLiteral("https://api.github.com/repos/mahammadosmanov/mono-sync/releases?per_page=1");

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
