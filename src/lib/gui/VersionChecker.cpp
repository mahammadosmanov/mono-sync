/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VersionChecker.h"

#include "common/Settings.h"
#include "common/VersionInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <climits>

VersionChecker::VersionChecker(QObject *parent) : QObject(parent), m_network{new QNetworkAccessManager(this)}
{
  connect(m_network, &QNetworkAccessManager::finished, this, &VersionChecker::replyFinished, Qt::UniqueConnection);
}

void VersionChecker::checkLatest() const
{
  const QString url = Settings::value(Settings::Gui::UpdateCheckUrl).toString();
  qDebug("checking for updates at: %s", qPrintable(url));
  auto request = QNetworkRequest(url);
  auto userAgent = QString("%1 %2 on %3").arg(kAppName, kVersion, QSysInfo::prettyProductName());
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
  request.setRawHeader("X-Deskflow-Version", kVersion);
  request.setRawHeader("X-Deskflow-Language", qPrintable(QLocale::system().name()));
  m_network->get(request);
}

bool VersionChecker::parseRelease(const QByteArray &json, Release &release)
{
#if defined(Q_OS_WIN)
  const auto installerSuffix = QStringLiteral(".exe");
#elif defined(Q_OS_MACOS)
  const auto installerSuffix = QStringLiteral(".dmg");
#else
  const auto installerSuffix = QStringLiteral("");
#endif

  const auto releases = QJsonDocument::fromJson(json).array();
  if (releases.isEmpty()) {
    return false;
  }

  const auto newest = releases.first().toObject();
  auto version = newest.value(QStringLiteral("tag_name")).toString();
  if (version.startsWith(QLatin1Char('v'))) {
    version.remove(0, 1);
  }
  // compareVersions() indexes three numeric parts, so reject anything else before comparing.
  static const QRegularExpression versionPattern(QStringLiteral(R"(^\d+\.\d+\.\d+(\.\d+)?(-[\w.]+)?$)"));
  if (!versionPattern.match(version).hasMatch()) {
    return false;
  }

  release.version = version;
  release.url = QUrl(newest.value(QStringLiteral("html_url")).toString());
  release.fileName.clear();
  release.sha256.clear();

  for (const auto &entry : newest.value(QStringLiteral("assets")).toArray()) {
    const auto asset = entry.toObject();
    const auto name = asset.value(QStringLiteral("name")).toString();
    const auto digest = asset.value(QStringLiteral("digest")).toString();
    const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
    const bool trusted = url.scheme() == QLatin1String("https") && url.host() == QLatin1String("github.com");
    if (!installerSuffix.isEmpty() && name.endsWith(installerSuffix) && digest.startsWith(QStringLiteral("sha256:")) &&
        trusted) {
      release.url = url;
      release.fileName = name;
      release.sha256 = digest.mid(QStringLiteral("sha256:").size());
      break;
    }
  }
  return true;
}

bool VersionChecker::isNewer(const QString &available, const QString &current)
{
  return compareVersions(current, available) > 0;
}

bool VersionChecker::promptsFor(const QString &available, const QString &current, const QString &skipped)
{
  return isNewer(available, current) && available != skipped;
}

void VersionChecker::replyFinished(QNetworkReply *reply)
{
  reply->deleteLater();
  const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qWarning("error checking for updates, http status: %d, error: %s", httpStatus, qPrintable(reply->errorString()));
    Q_EMIT checkFailed(reply->errorString());
    return;
  }

  qDebug("version check server success, http status: %d", httpStatus);

  Release release;
  if (!parseRelease(reply->readAll(), release)) {
    qWarning("version check response could not be read");
    Q_EMIT checkFailed(tr("The update server sent an unreadable response."));
    return;
  }

  if (compareVersions(kVersion, release.version) > 0) {
    qWarning().noquote() //
        << QStringLiteral("current version %1 out of date, update available: %2").arg(kVersion, release.version);
    m_latest = release;
    Q_EMIT updateFound(release.version);
  } else {
    qDebug().noquote() << QStringLiteral("current version %1 is upto date").arg(kVersion);
    Q_EMIT upToDate();
  }
}

int VersionChecker::getStageVersion(QString stage)
{
  const char *stableName = "stable";
  const char *rcName = "rc";
  const char *betaName = "beta";

  // use max int for stable so it's always the highest value.
  const int stableValue = INT_MAX;
  const int rcValue = 2;
  const int betaValue = 1;
  const int otherValue = 0;

  if (stage.isEmpty() || stage == stableName) {
    return stableValue;
  } else if (stage.startsWith(rcName, Qt::CaseInsensitive)) {
    static QRegularExpression re("\\d*", QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(stage);
    if (match.hasMatch()) {
      // return the rc value plus the rc number (e.g. 2 + 1)
      // this should be ok since stable is max int.
      return rcValue + match.captured(1).toInt();
    }
  } else if (stage == betaName) {
    return betaValue;
  }

  return otherValue;
}

int VersionChecker::compareVersions(const QString &left, const QString &right)
{
  if (left.compare(right) == 0)
    return 0; // versions are same.

  QStringList leftParts = left.split("-");
  QStringList rightParts = right.split("-");

  QString leftNumber = leftParts.at(0);
  QString rightNumber = rightParts.at(0);

  QStringList leftNumberParts = leftNumber.split(".");
  QStringList rightNumberParts = rightNumber.split(".");

  auto leftStagePart = leftParts.size() > 1 ? leftParts.at(1) : "";
  auto rightStagePart = rightParts.size() > 1 ? rightParts.at(1) : "";

  const int leftMajor = leftNumberParts.at(0).toInt();
  const int leftMinor = leftNumberParts.at(1).toInt();
  const int leftPatch = leftNumberParts.at(2).toInt();
  const int leftStage = getStageVersion(leftStagePart);

  const int rightMajor = rightNumberParts.at(0).toInt();
  const int rightMinor = rightNumberParts.at(1).toInt();
  const int rightPatch = rightNumberParts.at(2).toInt();
  const int rightStage = getStageVersion(rightStagePart);

  const bool rightWins =
      (rightMajor > leftMajor) || ((rightMajor >= leftMajor) && (rightMinor > leftMinor)) ||
      ((rightMajor >= leftMajor) && (rightMinor >= leftMinor) && (rightPatch > leftPatch)) ||
      ((rightMajor >= leftMajor) && (rightMinor >= leftMinor) && (rightPatch >= leftPatch) && (rightStage > leftStage));

  return rightWins ? 1 : -1;
}
