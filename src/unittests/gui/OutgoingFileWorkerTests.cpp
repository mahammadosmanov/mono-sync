/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/OutgoingFileWorker.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

class OutgoingFileWorkerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void senderAndReceiverWorkersRoundTrip_data();
  void senderAndReceiverWorkersRoundTrip();
  void sourceMutationBetweenOfferAndChunkIsRejected_data();
  void sourceMutationBetweenOfferAndChunkIsRejected();
};

void OutgoingFileWorkerTests::senderAndReceiverWorkersRoundTrip_data()
{
  QTest::addColumn<QByteArray>("content");
  QTest::newRow("empty") << QByteArray();
  QTest::newRow("multiple chunks") << QByteArray(70 * 1024, 'm');
}

void OutgoingFileWorkerTests::senderAndReceiverWorkersRoundTrip()
{
  QFETCH(QByteArray, content);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString sourcePath = temporary.filePath(QStringLiteral("source.bin"));
  const QString receiveDirectory = temporary.filePath(QStringLiteral("received"));
  QVERIFY(QDir().mkpath(receiveDirectory));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(content), content.size());
  source.close();

  QThread senderThread;
  QThread receiverThread;
  struct ThreadGuard
  {
    QThread &sender;
    QThread &receiver;
    ~ThreadGuard()
    {
      sender.quit();
      receiver.quit();
      sender.wait();
      receiver.wait();
    }
  } threadGuard{senderThread, receiverThread};
  Q_UNUSED(threadGuard);
  auto *sender = new OutgoingFileWorker;
  auto *receiver = new OutgoingFileWorker;
  sender->moveToThread(&senderThread);
  receiver->moveToThread(&receiverThread);
  connect(&senderThread, &QThread::finished, sender, &QObject::deleteLater);
  connect(&receiverThread, &QThread::finished, receiver, &QObject::deleteLater);

  bool finished = false;
  bool transferSucceeded = false;
  QString error;
  connect(sender, &OutgoingFileWorker::prepared, receiver,
          [receiver, receiveDirectory](const QString &name, quint64 size, const QByteArray &hash) {
            receiver->beginReceive(receiveDirectory, name, size, hash);
          });
  connect(receiver, &OutgoingFileWorker::receiverReady, this,
          [sender, receiver, content, &finished, &error](bool success, const QString &message) {
            if (!success) {
              error = message;
              finished = true;
              return;
            }
            if (content.isEmpty()) {
              QMetaObject::invokeMethod(receiver, [receiver] { receiver->finishReceive(); }, Qt::QueuedConnection);
            } else {
              QMetaObject::invokeMethod(sender, [sender] { sender->readNextChunk(); }, Qt::QueuedConnection);
            }
          });
  connect(sender, &OutgoingFileWorker::chunkReady, receiver,
          [receiver](const QByteArray &chunk, quint64) { receiver->appendReceivedChunk(chunk); });
  connect(receiver, &OutgoingFileWorker::receiveChunkSaved, this,
          [sender, receiver, &finished, &error](bool success, quint64, quint64 received, quint64 total,
                                                 const QString &message) {
            if (!success) {
              error = message;
              finished = true;
              return;
            }
            if (received < total) {
              QMetaObject::invokeMethod(sender, [sender] { sender->readNextChunk(); }, Qt::QueuedConnection);
            } else {
              QMetaObject::invokeMethod(receiver, [receiver] { receiver->finishReceive(); }, Qt::QueuedConnection);
            }
          });
  connect(receiver, &OutgoingFileWorker::receiveFinished, this,
          [&finished, &transferSucceeded, &error](bool success, const QString &, const QString &message) {
            finished = true;
            transferSucceeded = success;
            error = message;
          });
  connect(sender, &OutgoingFileWorker::failed, this, [&finished, &error](const QString &message) {
    finished = true;
    error = message;
  });

  receiverThread.start();
  senderThread.start();
  QMetaObject::invokeMethod(sender, [sender, sourcePath] { sender->prepare(sourcePath); }, Qt::QueuedConnection);
  QElapsedTimer deadline;
  deadline.start();
  while (!finished && deadline.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(5);
  }
  senderThread.quit();
  receiverThread.quit();
  QVERIFY(senderThread.wait(10000));
  QVERIFY(receiverThread.wait(10000));
  QVERIFY(finished);
  QVERIFY2(transferSucceeded, qPrintable(error));

  QFile received(temporary.filePath(QStringLiteral("received/source.bin")));
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), content);
}

void OutgoingFileWorkerTests::sourceMutationBetweenOfferAndChunkIsRejected_data()
{
  QTest::addColumn<bool>("grow");
  QTest::newRow("growth") << true;
  QTest::newRow("shrink") << false;
}

void OutgoingFileWorkerTests::sourceMutationBetweenOfferAndChunkIsRejected()
{
  QFETCH(bool, grow);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString sourcePath = temporary.filePath(QStringLiteral("source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write("data"), 4);
  source.close();

  QThread thread;
  struct ThreadGuard
  {
    QThread &thread;
    ~ThreadGuard()
    {
      thread.quit();
      thread.wait();
    }
  } threadGuard{thread};
  Q_UNUSED(threadGuard);
  auto *worker = new OutgoingFileWorker;
  worker->moveToThread(&thread);
  connect(&thread, &QThread::finished, worker, &QObject::deleteLater);

  bool failed = false;
  bool chunkSent = false;
  QString error;
  connect(worker, &OutgoingFileWorker::prepared, this,
          [worker, sourcePath, grow](const QString &, quint64, const QByteArray &) {
            QFile mutated(sourcePath);
            if (!mutated.open(QIODevice::WriteOnly | (grow ? QIODevice::Append : QIODevice::Truncate))) {
              return;
            }
            if (grow) {
              mutated.write("more");
            }
            mutated.close();
            QMetaObject::invokeMethod(worker, [worker] { worker->readNextChunk(); }, Qt::QueuedConnection);
          });
  connect(worker, &OutgoingFileWorker::chunkReady, this,
          [&chunkSent](const QByteArray &, quint64) { chunkSent = true; });
  connect(worker, &OutgoingFileWorker::failed, this, [&failed, &error](const QString &message) {
    failed = true;
    error = message;
  });

  thread.start();
  QMetaObject::invokeMethod(worker, [worker, sourcePath] { worker->prepare(sourcePath); }, Qt::QueuedConnection);
  QElapsedTimer deadline;
  deadline.start();
  while (!failed && !chunkSent && deadline.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(5);
  }
  thread.quit();
  QVERIFY(thread.wait(10000));
  QVERIFY2(failed, "mutated source file was not rejected");
  QVERIFY2(!chunkSent, qPrintable(error));
}

QTEST_GUILESS_MAIN(OutgoingFileWorkerTests)

#include "OutgoingFileWorkerTests.moc"
