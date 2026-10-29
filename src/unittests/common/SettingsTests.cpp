/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsTests.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>

void SettingsTests::initTestCase()
{
  QFile oldSettings(m_settingsFile);
  if (oldSettings.exists())
    oldSettings.remove();
}

void SettingsTests::setSettingsFile()
{
  Settings::setSettingsFile(m_settingsFile);
}

void SettingsTests::setStateFile()
{
  Settings::setStateFile(m_stateFile);
}

void SettingsTests::settingsFile()
{
  QVERIFY(Settings::settingsFile().endsWith(m_settingsFile));
}

void SettingsTests::settingsPath()
{
  QVERIFY(Settings::settingsPath().endsWith(m_settingsPath));
}

void SettingsTests::tlsDir()
{
  QVERIFY(Settings::tlsDir().endsWith(m_expectedTlsDir));
}

void SettingsTests::tlsTrustedServersDb()
{
  QVERIFY(Settings::tlsTrustedServersDb().endsWith(m_expectedTlsServerDB));
}

void SettingsTests::tlsTrustedClientsDb()
{
  QVERIFY(Settings::tlsTrustedClientsDb().endsWith(m_expectedTlsClientDB));
}

void SettingsTests::checkValidSettings()
{
  QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
  QVERIFY(spy.isValid());

  const auto keysToCheck = QRegularExpression(QLatin1String("[^%1]").arg(Settings::Core::ComputerName));
  const auto validKeys = Settings::validKeys().filter(keysToCheck);
  for (const auto &setting : validKeys) {
    const auto value = Settings::value(setting).toString();
    QCOMPARE(Settings::defaultValue(setting).toString(), value);

    Settings::setValue(setting, "NEW_VALUE");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<QString>(spy.first().at(0)), setting);
    auto expectedAfterWrite = QStringLiteral("NEW_VALUE");
#ifdef Q_OS_WIN
    if (setting == Settings::Core::ProcessMode || setting == Settings::Daemon::Elevate ||
        setting == Settings::Gui::AutoUpdateCheck)
      expectedAfterWrite = Settings::defaultValue(setting).toString();
#endif
    QCOMPARE(Settings::value(setting).toString(), expectedAfterWrite);

    Settings::setValue(setting);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(Settings::value(setting).toString(), value);

    // Reset the spy for the next loop
    spy.clear();
    QCOMPARE(spy.count(), 0);
  }
}

void SettingsTests::checkCleanComputerName()
{
  const auto input = QStringLiteral("--!_ _-C@o#mpu$t%e^&*(r)= +Name\n[1]2|3?4--5>6<,7`~/8*90\\.lan--..    ..");
  const auto expected = QStringLiteral("Computer_Name_1234--567890.lan");

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkCleanComputerName_LongName()
{
  QString input;
  input.fill('f', 300);
  input.prepend('.');

  QString expected;
  expected.fill('f', 255);

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkClearState()
{
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4));
  QVERIFY(Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());

  Settings::clearState();

  QVERIFY(!Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());
}

void SettingsTests::windowsModeIgnoresPrivilegedSettings()
{
#ifndef Q_OS_WIN
  QSKIP("Windows standard-user policy is only compiled for Windows.");
#else
  QVERIFY(!Settings::isPortableMode());
  QCOMPARE(
      Settings::defaultValue(Settings::Core::ProcessMode).value<Settings::ProcessMode>(), Settings::ProcessMode::Desktop
  );
  QVERIFY(!Settings::defaultValue(Settings::Daemon::Elevate).toBool());
  QVERIFY(!Settings::defaultValue(Settings::Gui::AutoUpdateCheck).toBool());

  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Service);
  Settings::setValue(Settings::Daemon::Elevate, true);
  Settings::setValue(Settings::Gui::AutoUpdateCheck, true);

  const bool usesDesktopModeWithoutPortableMarker =
      Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>() == Settings::ProcessMode::Desktop;
  const bool doesNotElevateDaemonWithoutPortableMarker = !Settings::value(Settings::Daemon::Elevate).toBool();
  const bool doesNotCheckInstallerUpdatesWithoutPortableMarker =
      !Settings::value(Settings::Gui::AutoUpdateCheck).toBool();

  const auto portableFileName = Settings::portableSettingsFile();
  const auto portableDirectory = QFileInfo(portableFileName).absolutePath();
  QVERIFY(QDir().mkpath(portableDirectory));

  QFile portableSettingsFile(portableFileName);
  QVERIFY(portableSettingsFile.open(QIODevice::WriteOnly));
  portableSettingsFile.close();

  const bool usesDesktopModeWithPortableMarker =
      Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>() == Settings::ProcessMode::Desktop;
  const bool doesNotElevateDaemonWithPortableMarker = !Settings::value(Settings::Daemon::Elevate).toBool();
  const bool doesNotCheckInstallerUpdatesWithPortableMarker = !Settings::value(Settings::Gui::AutoUpdateCheck).toBool();

  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
  Settings::setValue(Settings::Daemon::Elevate, false);
  Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  const bool removedPortableMarker = portableSettingsFile.remove();
  QDir().rmdir(portableDirectory);

  QVERIFY(usesDesktopModeWithoutPortableMarker);
  QVERIFY(doesNotElevateDaemonWithoutPortableMarker);
  QVERIFY(doesNotCheckInstallerUpdatesWithoutPortableMarker);
  QVERIFY(usesDesktopModeWithPortableMarker);
  QVERIFY(doesNotElevateDaemonWithPortableMarker);
  QVERIFY(doesNotCheckInstallerUpdatesWithPortableMarker);
  QVERIFY(removedPortableMarker);
#endif
}

QTEST_MAIN(SettingsTests)
