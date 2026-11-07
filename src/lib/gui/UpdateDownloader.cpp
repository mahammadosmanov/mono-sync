/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateDownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include "deskflow/FileTransferReceiver.h"

UpdateDownloader::UpdateDownloader(QObject *parent) : QObject(parent), m_network{new QNetworkAccessManager(this)}
{
}

void UpdateDownloader::start(const QUrl &url, const QString &fileName, const QString &sha256)
{
  cancel();
  if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com") || sha256.isEmpty()) {
    Q_EMIT failed(tr("The update package is missing a trusted download address or checksum."));
    return;
  }

  const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (!QDir().mkpath(downloads)) {
    Q_EMIT failed(tr("Could not create the Downloads folder."));
    return;
  }

  m_finalPath = FileTransferReceiver::uniqueFilePath(QDir(downloads).filePath(fileName));
  m_partPath = m_finalPath + QStringLiteral(".part");
  m_sha256 = sha256.toLower();
  m_file.setFileName(m_partPath);
  if (!m_file.open(QIODevice::WriteOnly)) {
    Q_EMIT failed(tr("Could not open %1 for writing.").arg(m_partPath));
    return;
  }

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  m_reply = m_network->get(request);
  connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file.write(m_reply->readAll()); });
  connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);
  connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::onFinished);
}

void UpdateDownloader::cancel()
{
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  if (m_file.isOpen()) {
    m_file.close();
    m_file.remove();
  }
}

void UpdateDownloader::onFinished()
{
  const auto error = m_reply->error();
  const auto errorString = m_reply->errorString();
  m_reply->deleteLater();
  m_reply = nullptr;
  m_file.close();

  if (error != QNetworkReply::NoError) {
    m_file.remove();
    Q_EMIT failed(errorString);
    return;
  }

  QFile written(m_partPath);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const bool hashed = written.open(QIODevice::ReadOnly) && hash.addData(&written);
  const auto digest = hash.result().toHex();
  written.close();
  if (!hashed || digest != m_sha256.toLatin1()) {
    m_file.remove();
    Q_EMIT failed(tr("The downloaded update did not match its published checksum and was discarded."));
    return;
  }

  const auto target = FileTransferReceiver::uniqueFilePath(m_finalPath);
  if (!m_file.rename(target)) {
    m_file.remove();
    Q_EMIT failed(tr("Could not save the update to %1.").arg(target));
    return;
  }
  Q_EMIT finished(target);
}
