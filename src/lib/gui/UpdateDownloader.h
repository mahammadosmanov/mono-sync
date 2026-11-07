/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Downloads a release installer into the Downloads folder. The file is only
 * published under its final name after its SHA-256 digest matches the release
 * feed. Existing files are never replaced.
 */
class UpdateDownloader : public QObject
{
  Q_OBJECT
public:
  explicit UpdateDownloader(QObject *parent = nullptr);

  void start(const QUrl &url, const QString &fileName, const QString &sha256);
  void cancel();

Q_SIGNALS:
  void progress(qint64 received, qint64 total);
  void finished(const QString &path);
  void failed(const QString &error);

private:
  void onFinished();

  QNetworkAccessManager *m_network = nullptr;
  QNetworkReply *m_reply = nullptr;
  QFile m_file;
  QString m_partPath;
  QString m_finalPath;
  QString m_sha256;
};
