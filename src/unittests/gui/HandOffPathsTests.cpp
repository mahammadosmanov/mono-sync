/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/HandOffPaths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class HandOffPathsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void acceptsExistingAbsoluteFile();
  void rejectsRelativePath();
  void rejectsDirectory();
  void rejectsMissingFile();
  void capsAtOneHundredPaths();
  void oversizedInputIsRejected();
};

void HandOffPathsTests::acceptsExistingAbsoluteFile()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = QDir(temporary.path()).filePath(QStringLiteral("a.txt"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  QCOMPARE(parseHandOffPaths((path + QLatin1Char('\n')).toUtf8()), QStringList{path});
}

void HandOffPathsTests::rejectsRelativePath()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QFile file(QDir(temporary.path()).filePath(QStringLiteral("a.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  QVERIFY(parseHandOffPaths(QByteArrayLiteral("a.txt")).isEmpty());
}

void HandOffPathsTests::rejectsDirectory()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());

  QVERIFY(parseHandOffPaths(temporary.path().toUtf8()).isEmpty());
}

void HandOffPathsTests::rejectsMissingFile()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = QDir(temporary.path()).filePath(QStringLiteral("missing.txt"));

  QVERIFY(parseHandOffPaths(path.toUtf8()).isEmpty());
}

void HandOffPathsTests::capsAtOneHundredPaths()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = QDir(temporary.path()).filePath(QStringLiteral("a.txt"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  QByteArray data;
  for (int i = 0; i < 150; ++i)
    data.append(path.toUtf8()).append('\n');

  QCOMPARE(parseHandOffPaths(data).size(), 100);
}

void HandOffPathsTests::oversizedInputIsRejected()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = QDir(temporary.path()).filePath(QStringLiteral("a.txt"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  const auto data = path.toUtf8() + '\n' + QByteArray(64 * 1024, ' ');
  QVERIFY(parseHandOffPaths(data).isEmpty());
}

QTEST_GUILESS_MAIN(HandOffPathsTests)
#include "HandOffPathsTests.moc"
