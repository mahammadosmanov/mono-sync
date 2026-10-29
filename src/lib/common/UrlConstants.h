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
const auto kUrlDownload = QStringLiteral("%1/actions").arg(kUrlApp);
const auto kUrlWiki = kUrlApp;
const auto kUrlUpdateCheck = QString();

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
