/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferReceiver.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class FileTransferReceiverTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void receivesAndFinalizesVerifiedFile();
  void receivesEmptyFile();
  void receivesMultipleBoundedChunks();
  void rejectsUnsafeFileNames();
  void rejectsExistingDestination();
  void rejectsOversizeOffer();
  void rejectsOversizeChunk();
  void rejectsWrongHashAndRemovesPartial();
  void rejectsWrongSizeAndRemovesPartial();
  void cancellationRemovesPartial();
  void preservesDestinationCreatedDuringTransfer();
};

void FileTransferReceiverTests::receivesAndFinalizesVerifiedFile()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents("file transfer payload");
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("payload.txt"), contents.size(), digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.isActive());
  QCOMPARE(receiver.expectedBytes(), static_cast<quint64>(contents.size()));
  QVERIFY(receiver.appendChunk(contents));
  QCOMPARE(receiver.receivedBytes(), static_cast<quint64>(contents.size()));
  QVERIFY(receiver.finish());
  QVERIFY(receiver.isComplete());
  QVERIFY(!receiver.isActive());

  QFile received(receiver.destinationPath());
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), contents);
  QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.part")}, QDir::Files).size(), 0);
}

void FileTransferReceiverTests::receivesEmptyFile()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto digest = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("empty.bin"), 0, digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.finish());
  QCOMPARE(QFileInfo(receiver.destinationPath()).size(), 0);
}

void FileTransferReceiverTests::receivesMultipleBoundedChunks()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QByteArray contents(FileTransferReceiver::kMaximumChunkSize + 7, 'x');
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("unicode-雪.txt"), contents.size(), digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.appendChunk(contents.left(FileTransferReceiver::kMaximumChunkSize)));
  QVERIFY(receiver.appendChunk(contents.mid(FileTransferReceiver::kMaximumChunkSize)));
  QVERIFY(receiver.finish());
  QFile received(receiver.destinationPath());
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), contents);
}

void FileTransferReceiverTests::rejectsUnsafeFileNames()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto digest = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256);

  for (const auto &name : {QStringLiteral("../escape"), QStringLiteral("sub/file"), QStringLiteral("CON.txt")}) {
    FileTransferReceiver receiver(directory.path(), name, 0, digest);
    QVERIFY(!receiver.begin());
  }
  QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void FileTransferReceiverTests::rejectsExistingDestination()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto path = QDir(directory.path()).filePath(QStringLiteral("existing.txt"));
  QFile existing(path);
  QVERIFY(existing.open(QIODevice::WriteOnly));
  QCOMPARE(existing.write("keep"), 4);
  existing.close();
  const auto digest = QCryptographicHash::hash(QByteArray("new"), QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("existing.txt"), 3, digest);

  QVERIFY(!receiver.begin());
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArray("keep"));
}

void FileTransferReceiverTests::rejectsOversizeOffer()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto digest = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("large.bin"),
                                FileTransferReceiver::kMaximumFileSize + 1, digest);

  QVERIFY(!receiver.begin());
  QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void FileTransferReceiverTests::rejectsOversizeChunk()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents("x");
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("chunk.bin"), contents.size(), digest);

  QVERIFY(receiver.begin());
  QVERIFY(!receiver.appendChunk(QByteArray(FileTransferReceiver::kMaximumChunkSize + 1, 'x')));
  QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferReceiverTests::rejectsWrongHashAndRemovesPartial()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  FileTransferReceiver receiver(directory.path(), QStringLiteral("bad-hash.bin"), 4, QByteArray(32, '\0'));

  QVERIFY(receiver.begin());
  QVERIFY(receiver.appendChunk(QByteArray("data")));
  QVERIFY(!receiver.finish());
  QVERIFY(!QFileInfo::exists(receiver.destinationPath()));
  QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void FileTransferReceiverTests::rejectsWrongSizeAndRemovesPartial()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents("abc");
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("short.bin"), 4, digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.appendChunk(contents));
  QVERIFY(!receiver.finish());
  QVERIFY(!QFileInfo::exists(receiver.destinationPath()));
  QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void FileTransferReceiverTests::cancellationRemovesPartial()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents("some data");
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("cancelled.bin"), contents.size(), digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.appendChunk(contents.left(4)));
  receiver.cancel();
  QVERIFY(!receiver.isActive());
  QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferReceiverTests::preservesDestinationCreatedDuringTransfer()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents("new content");
  const auto digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  FileTransferReceiver receiver(directory.path(), QStringLiteral("existing.txt"), contents.size(), digest);

  QVERIFY(receiver.begin());
  QVERIFY(receiver.appendChunk(contents));
  QFile existing(receiver.destinationPath());
  QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  QCOMPARE(existing.write("keep"), 4);
  existing.close();

  QVERIFY(!receiver.finish());
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArray("keep"));
  QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size() == 1);
}

QTEST_APPLESS_MAIN(FileTransferReceiverTests)

#include "FileTransferReceiverTests.moc"
