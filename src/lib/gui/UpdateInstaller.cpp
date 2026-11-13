/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QXmlStreamReader>

namespace UpdateInstaller {

namespace {

QString helperPath(qint64 pid, const QString &suffix)
{
  return QDir::temp().filePath(QStringLiteral("monosync-update-%1%2").arg(pid).arg(suffix));
}

bool writeHelper(const QString &path, const QString &text)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(text.toUtf8());
  file.close();
  return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

#if defined(Q_OS_MACOS)
QString posixHelper(qint64 pid, const QString &target, const QString &mount)
{
  const auto quotedTarget = shellQuote(target);
  const auto quotedMount = shellQuote(mount);
  // Copies the new bundle beside the old one, strips quarantine, swaps, detaches and relaunches.
  // If anything before the swap fails, the old bundle is kept and relaunched.
  return QStringLiteral(R"(#!/bin/sh
while kill -0 {{PID}} 2>/dev/null; do sleep 0.5; done
target={{TARGET}}
new="$target.new"
old="$target.old"
mount={{MOUNT}}
rm -rf "$new"
if ditto "$mount/MonoSync.app" "$new" && xattr -dr com.apple.quarantine "$new" && mv "$target" "$old"; then
  if mv "$new" "$target"; then
    rm -rf "$old"
  else
    mv "$old" "$target"
  fi
fi
rm -rf "$new"
hdiutil detach "$mount" -quiet
open "$target"
rm -f "$0"
)")
      .replace(QStringLiteral("{{PID}}"), QString::number(pid))
      .replace(QStringLiteral("{{TARGET}}"), quotedTarget)
      .replace(QStringLiteral("{{MOUNT}}"), quotedMount);
}

QString attachImage(const QString &package, QString &error)
{
  QProcess hdiutil;
  hdiutil.start(QStringLiteral("hdiutil"), {"attach", "-nobrowse", "-readonly", "-plist", package});
  if (!hdiutil.waitForFinished(300000) || hdiutil.exitCode() != 0) {
    error = QObject::tr("Could not open the update disk image.");
    return {};
  }

  QXmlStreamReader xml(hdiutil.readAllStandardOutput());
  QString key;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) {
      continue;
    }
    if (xml.name() == QLatin1String("key")) {
      key = xml.readElementText();
    } else if (xml.name() == QLatin1String("string") && key == QLatin1String("mount-point")) {
      const auto mount = xml.readElementText();
      if (!mount.isEmpty()) {
        return mount;
      }
    }
  }
  error = QObject::tr("The update disk image has no volume.");
  return {};
}
#endif

#if defined(Q_OS_WIN)
QString windowsHelper(qint64 pid, const QString &installer, const QString &exe)
{
  // Silent setup refuses to touch locked files, so it runs only after this process has exited.
  // On failure the old version is started again and a message keeps the installer path.
  const auto message = QStringLiteral("Mono Sync could not install the update. The current version is still installed. "
                                      "The downloaded installer is kept at: ")
                           .append(installer)
                           .replace(QLatin1Char('\''), QStringLiteral("''"));
  return QStringLiteral(R"(@echo off
:wait
tasklist /FI "PID eq {{PID}}" 2>nul | find "{{PID}}" >nul
if not errorlevel 1 (ping -n 2 127.0.0.1 >nul & goto wait)
"{{INSTALLER}}" /S
if errorlevel 1 goto failed
start "" "{{EXE}}"
exit /b 0
:failed
powershell -NoProfile -Command "Add-Type -AssemblyName PresentationFramework; [System.Windows.MessageBox]::Show('{{MESSAGE}}', 'Mono Sync')"
start "" "{{EXE}}"
exit /b 1
)")
      .replace(QStringLiteral("{{PID}}"), QString::number(pid))
      .replace(QStringLiteral("{{INSTALLER}}"), installer)
      .replace(QStringLiteral("{{EXE}}"), exe)
      .replace(QStringLiteral("{{MESSAGE}}"), message);
}
#endif

} // namespace

QString shellQuote(const QString &text)
{
  QString quoted = text;
  quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

bool launch(const QString &package, QString &error)
{
  const auto pid = QCoreApplication::applicationPid();

#if defined(Q_OS_MACOS)
  // The running bundle is Contents/MacOS/.. up to the .app folder.
  const auto target = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
  if (!target.endsWith(QStringLiteral(".app"))) {
    error = QObject::tr("Mono Sync is not running from an app bundle, so it cannot replace itself.");
    return false;
  }
  if (!QFileInfo(QFileInfo(target).absolutePath()).isWritable()) {
    error = QObject::tr("The folder containing %1 is not writable, so the update cannot replace it.").arg(target);
    return false;
  }

  const auto mount = attachImage(package, error);
  if (mount.isEmpty()) {
    return false;
  }
  if (!QFileInfo::exists(QDir(mount).filePath(QStringLiteral("MonoSync.app")))) {
    QProcess::execute(QStringLiteral("hdiutil"), {"detach", "-quiet", mount});
    error = QObject::tr("The update disk image does not contain MonoSync.app.");
    return false;
  }

  const auto script = helperPath(pid, QStringLiteral(".sh"));
  if (!writeHelper(script, posixHelper(pid, target, mount)) ||
      !QProcess::startDetached(QStringLiteral("/bin/sh"), {script})) {
    QProcess::execute(QStringLiteral("hdiutil"), {"detach", "-quiet", mount});
    error = QObject::tr("Could not start the update helper.");
    return false;
  }
  return true;

#elif defined(Q_OS_WIN)
  const auto installer = QDir::toNativeSeparators(package);
  const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  const auto script = helperPath(pid, QStringLiteral(".cmd"));
  if (!writeHelper(script, windowsHelper(pid, installer, exe)) ||
      !QProcess::startDetached(QStringLiteral("cmd.exe"), {"/d", "/c", QDir::toNativeSeparators(script)})) {
    error = QObject::tr("Could not start the update helper.");
    return false;
  }
  return true;

#else
  Q_UNUSED(package)
  error = QObject::tr("Installing updates is not supported on this platform.");
  return false;
#endif
}

void reveal(const QString &path)
{
#if defined(Q_OS_MACOS)
  QProcess::startDetached(QStringLiteral("/usr/bin/open"), {"-R", path});
#elif defined(Q_OS_WIN)
  QProcess::startDetached(QStringLiteral("explorer.exe"), {"/select," + QDir::toNativeSeparators(path)});
#else
  Q_UNUSED(path)
#endif
}

} // namespace UpdateInstaller
