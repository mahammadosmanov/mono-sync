/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class VersionChecker : public QObject
{
  Q_OBJECT
public:
  struct Release
  {
    QString version;
    QString fileName;
    // Installer for this platform when the feed provides one, otherwise the release page.
    QUrl url;
    // Empty when no installer matches, so the caller must fall back to opening the page.
    QString sha256;
  };

  explicit VersionChecker(QObject *parent = nullptr);
  void checkLatest() const;
  const Release &latest() const { return m_latest; }

  /**
   * \brief Reads the newest entry of a GitHub releases list
   * \return false when the payload is not a usable release
   */
  static bool parseRelease(const QByteArray &json, Release &release);

  /**
   * \brief True when \a available is newer than \a current
   */
  static bool isNewer(const QString &available, const QString &current);

  /**
   * \brief True when an automatic prompt or install is due for \a available: it is newer than
   * \a current and the user has not skipped exactly that version
   */
  static bool promptsFor(const QString &available, const QString &current, const QString &skipped);

public Q_SLOTS:
  void replyFinished(QNetworkReply *reply);
Q_SIGNALS:
  void updateFound(const QString &version);
  void upToDate();
  void checkFailed(const QString &error);

private:
  static int compareVersions(const QString &left, const QString &right);

  /**
   * \brief Converts a string stage to a integer value
   * \param stage The string containing the stage version
   * \return An integer representation of the stage, the higher the number the
   * more recent the version
   */
  static int getStageVersion(QString stage);
  QNetworkAccessManager *m_network = nullptr;
  Release m_latest;
};
