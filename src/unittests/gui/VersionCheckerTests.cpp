/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/VersionChecker.h"

#include <QTest>

class VersionCheckerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void parsesNewestReleaseAndPicksPlatformInstaller();
  void fallsBackToReleasePageWithoutTrustedInstaller();
  void rejectsMalformedFeed();
};

namespace {

const auto kFeed = R"([{
  "tag_name": "v0.2.4.0",
  "html_url": "https://github.com/mahammadosmanov/mono-sync/releases/tag/v0.2.4.0",
  "assets": [
    {
      "name": "MonoSync-Setup-0.2.4.exe",
      "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      "browser_download_url": "https://github.com/mahammadosmanov/mono-sync/releases/download/v0.2.4.0/MonoSync-Setup-0.2.4.exe"
    },
    {
      "name": "mono-sync-0.2.4-macos-arm64.dmg",
      "digest": "sha256:2222222222222222222222222222222222222222222222222222222222222222",
      "browser_download_url": "https://github.com/mahammadosmanov/mono-sync/releases/download/v0.2.4.0/mono-sync-0.2.4-macos-arm64.dmg"
    }
  ]
}])";

} // namespace

void VersionCheckerTests::parsesNewestReleaseAndPicksPlatformInstaller()
{
  VersionChecker::Release release;
  QVERIFY(VersionChecker::parseRelease(kFeed, release));
  QCOMPARE(release.version, QStringLiteral("0.2.4.0"));

#if defined(Q_OS_WIN)
  QCOMPARE(release.fileName, QStringLiteral("MonoSync-Setup-0.2.4.exe"));
  QCOMPARE(release.sha256, QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"));
#elif defined(Q_OS_MACOS)
  QCOMPARE(release.fileName, QStringLiteral("mono-sync-0.2.4-macos-arm64.dmg"));
  QCOMPARE(release.sha256, QStringLiteral("2222222222222222222222222222222222222222222222222222222222222222"));
#endif
}

void VersionCheckerTests::fallsBackToReleasePageWithoutTrustedInstaller()
{
  // Same feed, but the installer is served from an untrusted host and has no digest.
  const auto feed = QByteArray(kFeed)
                        .replace("https://github.com/mahammadosmanov/mono-sync/releases/download",
                                 "http://example.com/download")
                        .replace("sha256:", "md5:");

  VersionChecker::Release release;
  QVERIFY(VersionChecker::parseRelease(feed, release));
  QVERIFY(release.sha256.isEmpty());
  QVERIFY(release.fileName.isEmpty());
  QCOMPARE(release.url.toString(), QStringLiteral("https://github.com/mahammadosmanov/mono-sync/releases/tag/v0.2.4.0"));
}

void VersionCheckerTests::rejectsMalformedFeed()
{
  VersionChecker::Release release;
  QVERIFY(!VersionChecker::parseRelease("[]", release));
  QVERIFY(!VersionChecker::parseRelease("<html>maintenance</html>", release));
  QVERIFY(!VersionChecker::parseRelease(R"([{"tag_name": "latest"}])", release));
  QVERIFY(!VersionChecker::parseRelease(R"([{"tag_name": "1.2"}])", release));
}

QTEST_GUILESS_MAIN(VersionCheckerTests)

#include "VersionCheckerTests.moc"
