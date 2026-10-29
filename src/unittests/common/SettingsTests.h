/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Settings.h"

#include <QTest>

class SettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void setSettingsFile();
  void setStateFile();
  void migrateLegacyProfilePreservesSettingsAndTls();
  void migrateLegacyProfileResolvesDefaultCertificatePath();
  void settingsFile();
  void settingsPath();
  void tlsDir();
  void tlsTrustedServersDb();
  void tlsTrustedClientsDb();
  void checkValidSettings();
  void checkCleanComputerName();
  void checkCleanComputerName_LongName();
  void checkClearState();
  void windowsModeIgnoresPrivilegedSettings();

private:
  inline static const QString m_settingsPathTemp = QStringLiteral("tmp/test");
  inline static const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPathTemp);
  inline static const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPathTemp);

  inline static const QString m_settingsPath = m_settingsPathTemp;

  inline static const QString m_expectedTlsDir = QStringLiteral("%1/tls").arg(m_settingsPath);
  inline static const QString m_expectedTlsServerDB = QStringLiteral("%1/trusted-servers").arg(m_expectedTlsDir);
  inline static const QString m_expectedTlsClientDB = QStringLiteral("%1/trusted-clients").arg(m_expectedTlsDir);
};
