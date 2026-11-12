/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Diagnostic.h"
#include "OutgoingFileWorker.h"
#include "StyleUtils.h"

#include "dialogs/AboutDialog.h"
#include "dialogs/ClientConfigDialog.h"
#include "dialogs/FingerprintDialog.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/ServerConfigDialog.h"
#include "dialogs/SettingsDialog.h"

#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "deskflow/FileTransferProtocol.h"
#include "gui/Messages.h"
#include "gui/TlsUtility.h"
#include "gui/core/CoreProcess.h"
#include "gui/ipc/DaemonIpcClient.h"
#include "gui/widgets/LogDock.h"
#include "net/FingerprintDatabase.h"
#include "widgets/StatusBar.h"
#include "widgets/TransferPanel.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QMimeData>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>
#include <QTcpSocket>
#include <QThread>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QUuid>

#include <algorithm>
#include <memory>

#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace deskflow::gui;

MainWindow::MainWindow()
    : ui{std::make_unique<Ui::MainWindow>()},
      m_coreProcess(m_serverConfig),
      m_trayIcon{new QSystemTrayIcon(this)},
      m_guiDupeChecker{new QLocalServer(this)},
      m_daemonIpcClient{new ipc::DaemonIpcClient(this)},
      m_logDock{new LogDock(this)},
      m_statusBar{new StatusBar(this)},
      m_menuFile{new QMenu(this)},
      m_menuEdit{new QMenu(this)},
      m_menuView{new QMenu(this)},
      m_menuHelp{new QMenu(this)},
      m_actionAbout{new QAction(this)},
      m_actionMinimize{new QAction(this)},
      m_actionQuit{new QAction(this)},
      m_actionTrayQuit{new QAction(this)},
      m_actionRestore{new QAction(this)},
      m_actionSettings{new QAction(this)},
      m_actionStartCore{new QAction(this)},
      m_actionRestartCore{new QAction(this)},
      m_actionStopCore{new QAction(this)},
      m_actionShowHelp{new QAction(this)},
      m_actionCheckUpdates{new QAction(this)},
      m_actionSendFile{new QAction(this)},
      m_networkMonitor{new NetworkMonitor(this)}
{
  ui->setupUi(this);

  m_connectionProbeSocket = new QTcpSocket(this);
  m_connectionProbeTimer = new QTimer(this);
  m_connectionProbeTimer->setSingleShot(true);
  connect(m_connectionProbeSocket, &QTcpSocket::connected, this, [this] {
    if (!m_connectionProbeActive)
      return;
    m_connectionProbeActive = false;
    m_connectionProbeTimer->stop();
    ui->btnTestConnection->setEnabled(true);
    m_statusBar->showMessage(
        tr("TCP connection reached the server. Mono Sync will verify TLS identity when you connect."), 7000
    );
    m_connectionProbeSocket->disconnectFromHost();
  });
  connect(m_connectionProbeSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
    if (!m_connectionProbeActive)
      return;
    m_connectionProbeActive = false;
    m_connectionProbeTimer->stop();
    ui->btnTestConnection->setEnabled(true);
    const auto details = error == QAbstractSocket::ConnectionRefusedError
                             ? tr("The host responded, but nothing is listening on the configured port.")
                             : m_connectionProbeSocket->errorString();
    m_statusBar->showMessage(tr("Connection check failed: %1").arg(details), 10000);
  });
  connect(m_connectionProbeTimer, &QTimer::timeout, this, [this] {
    if (!m_connectionProbeActive)
      return;
    m_connectionProbeActive = false;
    m_connectionProbeSocket->abort();
    ui->btnTestConnection->setEnabled(true);
    m_statusBar->showMessage(tr("No TCP response within 3 seconds. Check the address, port, and network."), 10000);
  });

  setWindowIcon(QIcon::fromTheme(kRevFqdnName));

  addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

  // Setup Actions
  m_actionAbout->setMenuRole(QAction::AboutRole);
  m_actionAbout->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::HelpAbout));

  m_actionMinimize->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize-pip")));
  m_actionRestore->setIcon(QIcon::fromTheme(QStringLiteral("window-restore-pip")));

  if (!deskflow::platform::isWindows()) {
    m_actionQuit->setShortcut(QKeySequence::Quit);
    m_actionTrayQuit->setShortcut(QKeySequence::Quit);
  }

  m_actionQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionQuit->setMenuRole(QAction::QuitRole);

  m_actionTrayQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionTrayQuit->setMenuRole(QAction::NoRole);

  m_actionSettings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_actionSettings->setMenuRole(QAction::PreferencesRole);
  m_actionSettings->setShortcut(QKeySequence::Preferences);

  m_actionStartCore->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
  m_actionStartCore->setMenuRole(QAction::NoRole);

  m_actionRestartCore->setVisible(false);
  m_actionRestartCore->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
  m_actionRestartCore->setMenuRole(QAction::NoRole);

  m_actionStopCore->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop));
  m_actionStopCore->setMenuRole(QAction::NoRole);

  m_actionShowHelp->setIcon(QIcon::fromTheme(QStringLiteral("question")));
  m_actionShowHelp->setMenuRole(QAction::NoRole);
  m_actionShowHelp->setShortcut(QKeySequence::HelpContents);

  m_actionCheckUpdates->setIcon(QIcon::fromTheme(QStringLiteral("software-updates-release")));
  m_actionCheckUpdates->setMenuRole(QAction::NoRole);

  m_actionReceiveFolder = new QAction(this);
  m_actionAutoAccept = new QAction(this);
  m_actionAutoAccept->setCheckable(true);
  m_actionAutoAccept->setChecked(Settings::value(Settings::Gui::FileAutoAccept).toBool());
  m_actionForgetTrusted = new QAction(this);
  m_actionForgetTrusted->setEnabled(!Settings::value(Settings::Gui::FileAutoAcceptPeers).toStringList().isEmpty());
  setAcceptDrops(true);

  m_transferPanel = new TransferPanel(this);
  ui->topLevelWidget->layout()->addWidget(m_transferPanel);
  m_dropOverlay = new QLabel(centralWidget());
  m_dropOverlay->setAlignment(Qt::AlignCenter);
  m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dropOverlay->setTextFormat(Qt::PlainText);
  m_dropOverlay->hide();
  connect(m_transferPanel, &TransferPanel::cancelRequested, this, &MainWindow::cancelTransfer);
  connect(m_transferPanel, &TransferPanel::acceptRequested, this, [this](const QString &rowId) {
    if (isPendingOffer(rowId)) {
      acceptPendingOffer(defaultReceivePath(m_outgoingFileName));
    }
  });
  connect(m_transferPanel, &TransferPanel::chooseLocationRequested, this, [this](const QString &rowId) {
    if (!isPendingOffer(rowId)) {
      return;
    }
    const QString picked = QFileDialog::getSaveFileName(this, tr("Save file from %1").arg(m_outgoingPeer),
                                                        QDir::home().filePath(m_outgoingFileName));
    if (isPendingOffer(rowId) && !picked.isEmpty()) {
      acceptPendingOffer(picked);
    }
  });
  connect(m_transferPanel, &TransferPanel::declineRequested, this, [this](const QString &rowId) {
    if (isPendingOffer(rowId)) {
      acceptPendingOffer(QString());
    }
  });
  connect(m_transferPanel, &TransferPanel::acceptAlwaysRequested, this, &MainWindow::alwaysAcceptPendingOffer);
  connect(m_transferPanel, &TransferPanel::showInFolderRequested, this, &MainWindow::showInFolder);

  // Setup the Instance Checking
  // In case of a previous crash remove first
  QLocalServer::removeServer(m_guiSocketName);
  m_guiDupeChecker->listen(m_guiSocketName);

  createMenuBar();
  setupControls();
  updateText();
  connectSlots();
  setupTrayIcon();
  updateComputerName();
  setHelpFilePath();

  qDebug().noquote() << "active settings path:" << Settings::settingsPath();

  // Force generation of a certificate on the host
  if (TlsUtility::isEnabled()) {
    if (Settings::value(Settings::Security::KeySize).toInt() < 2048) {
      QMessageBox::information(
          this, kAppName,
          tr("Your current TLS key is smaller than the minimum allowed size, A new key 2048-bit key will be generated.")
      );
      Settings::setValue(Settings::Security::KeySize, 2048);
    }
    if (!TlsUtility::isCertValid()) {
      generateCertificate();
    } else {
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    }
  }

  applyConfig();
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
  restoreWindow();

#ifdef Q_OS_MACOS
  // Route native quits (Cmd+Q / Apple menu Quit / Dock "Quit") to the usual close-to-tray decision instead.
  installQuitHandler([this] { return !maybeHideToTray(); });
#endif
}
MainWindow::~MainWindow()
{
  if (m_fileWorker != nullptr) {
    m_fileWorker->requestCancel();
  }
  if (m_fileWorkerThread != nullptr) {
    auto *thread = m_fileWorkerThread;
    if (thread->isRunning()) {
      thread->setParent(nullptr);
      connect(thread, &QThread::finished, thread, &QObject::deleteLater);
      thread->quit();
    } else {
      delete thread;
    }
    m_fileWorkerThread = nullptr;
    m_fileWorker = nullptr;
  }
  // Stop network monitoring
  if (m_networkMonitor) {
    m_networkMonitor->stopMonitoring();
  }

  m_guiDupeChecker->close();
  m_coreProcess.cleanup();
}

void MainWindow::restoreWindow()
{
  auto windowGeometry = Settings::value(Settings::Gui::WindowGeometry).toRect();
  const auto totalGeometry = QGuiApplication::primaryScreen()->availableGeometry();
  if (!windowGeometry.isValid()) {
    adjustSize();
    windowGeometry = geometry();
  } else {
    setGeometry(windowGeometry);
  }
  m_expandedSize = geometry().size();

  if (!totalGeometry.contains(windowGeometry)) {
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    move(screenGeometry.center() - rect().center());
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool())
    setFixedSize(size());
}

void MainWindow::setupControls()
{
  secureSocket(false);

  ui->btnConfigureServer->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  ui->btnConfigureClient->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

  if (Settings::value(Settings::Core::LastVersion).toString() != kVersion) {
    Settings::setValue(Settings::Core::LastVersion, kVersion);
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool()) {
    m_logDock->hide();
  }

  ui->serverOptions->setVisible(false);
  ui->clientOptions->setVisible(false);

  const auto coreMode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  ui->rbModeClient->setChecked(coreMode == Settings::CoreMode::Client);
  ui->rbModeServer->setChecked(coreMode == Settings::CoreMode::Server);

  ui->lineEditName->setValidator(new QRegularExpressionValidator(m_nameRegEx, this));
  ui->lineEditName->setVisible(false);
  ui->lineEditName->installEventFilter(this);

  if (deskflow::platform::isMac()) {
    ui->rbModeServer->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->rbModeClient->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->btnSaveServerConfig->setFixedWidth(ui->btnSaveServerConfig->height());
  } else {
    ui->btnSaveServerConfig->setIconSize(QSize(22, 22));
  }
  setStatusBar(m_statusBar);
}

//////////////////////////////////////////////////////////////////////////////
// Begin slots
//////////////////////////////////////////////////////////////////////////////

// remember: using queued connection allows the render loop to run before
// executing the slot. the default is to instantly call the slot when the
// signal is emitted from the thread that owns the receiver's object.
void MainWindow::connectSlots()
{
  connect(Settings::instance(), &Settings::serverSettingsChanged, this, &MainWindow::serverConfigSaving);
  connect(Settings::instance(), &Settings::settingsChanged, this, &MainWindow::settingsChanged);

  connect(&m_coreProcess, &CoreProcess::error, this, &MainWindow::coreProcessError);
  connect(&m_coreProcess, &CoreProcess::logLine, this, &MainWindow::handleLogLine);
  connect(
      &m_coreProcess, &CoreProcess::processStateChanged, this, &MainWindow::coreProcessStateChanged,
      Qt::QueuedConnection
  );
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, this, &MainWindow::coreConnectionStateChanged);
  connect(&m_coreProcess, &CoreProcess::secureSocket, this, &MainWindow::secureSocket);
  connect(
      &m_coreProcess, &CoreProcess::daemonIpcClientConnectionFailed, this, &MainWindow::daemonIpcClientConnectionFailed
  );
  connect(&m_coreProcess, &CoreProcess::securityLevelChanged, m_statusBar, &StatusBar::setSecurityLevel);

  connect(m_actionAbout, &QAction::triggered, this, &MainWindow::openAboutDialog);
  connect(m_actionMinimize, &QAction::triggered, this, &MainWindow::hide);

  connect(m_actionQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionTrayQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionRestore, &QAction::triggered, this, &MainWindow::showAndActivate);
  connect(m_actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
  connect(m_actionStartCore, &QAction::triggered, this, &MainWindow::startCore);
  connect(m_actionRestartCore, &QAction::triggered, this, &MainWindow::resetCore);
  connect(m_actionStopCore, &QAction::triggered, this, &MainWindow::stopCore);
  connect(m_actionShowHelp, &QAction::triggered, this, &MainWindow::showHelpViewer);
  connect(m_actionCheckUpdates, &QAction::triggered, this, &MainWindow::checkForUpdates);
  connect(m_actionReceiveFolder, &QAction::triggered, this, &MainWindow::chooseReceiveFolder);
  connect(m_actionAutoAccept, &QAction::toggled, this, &MainWindow::setAutoAcceptFiles);
  connect(m_actionForgetTrusted, &QAction::triggered, this, [this] {
    Settings::setValue(Settings::Gui::FileAutoAcceptPeers);
    m_actionForgetTrusted->setEnabled(false);
  });
  connect(m_actionSendFile, &QAction::triggered, this, &MainWindow::sendFile);
  connect(&m_coreProcess, &CoreProcess::fileTransferMessageReceived, this, &MainWindow::handleFileTransferMessage);
  connect(&m_coreProcess, &CoreProcess::fileTransferIpcUnavailable, this,
          [this] { clearFileTransferAvailability(tr("The local core connection closed.")); });

  // Mac os tray will only show a menu
  if (!deskflow::platform::isMac())
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayIconActivated);
  connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &MainWindow::showAndActivate);

  connect(&m_coreProcess, &CoreProcess::connectedClientsChanged, this, &MainWindow::serverClientsChanged);
  connect(&m_coreProcess, &CoreProcess::unrecognisedClient, this, &MainWindow::handleUnrecognisedClient);
  connect(&m_coreProcess, &CoreProcess::connectionRefused, this, &MainWindow::handleConnectionRefused);
  connect(&m_coreProcess, &CoreProcess::retryIn, this, &MainWindow::updateTimeoutDelay);
  connect(&m_coreProcess, &CoreProcess::peerFingerprint, this, &MainWindow::handlePeerFingerprint);
  connect(&m_coreProcess, &CoreProcess::missingKeyboardLayouts, this, &MainWindow::handleMissingKeyboardLayouts);

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);
  } else {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);
  }

  connect(ui->btnRestartCore, &QPushButton::clicked, this, &MainWindow::resetCore);

  connect(ui->lineHostname, &QLineEdit::returnPressed, ui->btnRestartCore, &QPushButton::click);
  connect(ui->lineHostname, &QLineEdit::textChanged, this, &MainWindow::remoteHostChanged);
  connect(ui->btnTestConnection, &QPushButton::clicked, this, &MainWindow::testServerConnection);
  connect(ui->btnUseLastServer, &QPushButton::clicked, this, &MainWindow::useLastSuccessfulServer);
  connect(ui->btnCopyAddress, &QPushButton::clicked, this, &MainWindow::copyServerAddress);

  connect(ui->btnSaveServerConfig, &QPushButton::clicked, this, &MainWindow::saveServerConfig);
  connect(ui->btnConfigureServer, &QPushButton::clicked, this, [this] { showConfigureServer(""); });
  connect(ui->btnConfigureClient, &QPushButton::clicked, this, [this] { showConfigureClient(); });
  connect(ui->lblComputerName, &QLabel::linkActivated, this, &MainWindow::openSettings);

  connect(ui->rbModeServer, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);
  connect(ui->rbModeClient, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);

  connect(m_logDock->toggleViewAction(), &QAction::toggled, this, &MainWindow::toggleLogVisible);

  connect(m_statusBar, &StatusBar::requestShowMyFingerprints, this, &MainWindow::showMyFingerprint);
  connect(m_statusBar, &StatusBar::requestUpdateVersion, this, &MainWindow::openGetNewVersionUrl);
  connect(&m_versionChecker, &VersionChecker::updateFound, this, &MainWindow::onUpdateFound);
  connect(&m_versionChecker, &VersionChecker::upToDate, this, &MainWindow::onUpToDate);
  connect(&m_versionChecker, &VersionChecker::checkFailed, this, &MainWindow::onUpdateCheckFailed);
  connect(&m_updateDownloader, &UpdateDownloader::progress, this, [this](qint64 received, qint64 total) {
    if (m_updateProgress && total > 0) {
      m_updateProgress->setValue(static_cast<int>(received * 100 / total));
    }
  });
  connect(&m_updateDownloader, &UpdateDownloader::finished, this, &MainWindow::onUpdateDownloaded);
  connect(&m_updateDownloader, &UpdateDownloader::failed, this, &MainWindow::onUpdateDownloadFailed);

  connect(m_guiDupeChecker, &QLocalServer::newConnection, this, &MainWindow::showAndActivate);

  connect(ui->btnEditName, &QPushButton::clicked, this, &MainWindow::showHostNameEditor);

  connect(ui->lineEditName, &QLineEdit::editingFinished, this, &MainWindow::setHostName);

  connect(m_networkMonitor, &NetworkMonitor::ipAddressesChanged, this, &MainWindow::updateIpLabel);
}

void MainWindow::toggleLogVisible(bool visible)
{
  // When the main window is hidden e.g. close to tray / minimized), this also triggers the log visibility toggle,
  // but we don't want to hide the log in this case since we would need to un-hide it when the window is shown again.
  if (!isVisible() || isMinimized()) {
    qDebug() << "not toggling log, window not visible";
    return;
  }

  setFixedSize(16777215, 16777215);
  Settings::setValue(Settings::Gui::LogExpanded, visible);
  if (visible) {
    if (m_logDock->isFloating()) {
      adjustSize();
      setFixedSize(size());
    } else {
      QTimer::singleShot(15, this, [&] { resize(m_expandedSize); });
    }
  } else {
    if (!m_logDock->isFloating()) {
      m_expandedSize = geometry().size();
    }
    m_logDock->hide();
    if (!m_logDock->isFloating()) {
      adjustSize();
    }
    setFixedSize(size());
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
}

void MainWindow::settingsChanged(const QString &key)
{
  if (key == Settings::Log::Level) {
    m_coreProcess.applyLogLevel();
    return;
  }

  if (key == Settings::Core::ComputerName)
    updateComputerName();

  if ((key == Settings::Security::Certificate) || (key == Settings::Security::KeySize) ||
      (key == Settings::Security::TlsEnabled) || (key == Settings::Security::CheckPeers)) {
    if (TlsUtility::isEnabled()) {
      if (!TlsUtility::isCertValid()) {
        qWarning() << tr("invalid certificate, generating a new one");
        TlsUtility::generateCertificate();
      }
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
      updateFingerprintButton();
    }
    updateSecurityIcon(m_statusBar->securityIconVisible());
    return;
  }
}

void MainWindow::serverConfigSaving()
{
  m_serverConfig.commit();
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
  if (reason != QSystemTrayIcon::Trigger)
    return;
  isVisible() ? hide() : showAndActivate();
}

void MainWindow::coreProcessError(CoreProcess::Error error)
{
  if (error == CoreProcess::Error::AddressMissing) {
    QMessageBox::warning(
        this, tr("Address missing"), tr("Please enter the hostname or IP address of the other computer.")
    );
  } else if (error == CoreProcess::Error::StartFailed) {
    show();
    auto message = tr("The Core executable could not be started.\n"
                      "Please check if you have sufficient permissions to run %1.")
                       .arg(kCoreBinName);

    if (Settings::value(Settings::Core::CoreMode) == Settings::CoreMode::Server) {
      const auto mode =
          Settings::value(Settings::Server::ExternalConfigFile).toBool() ? tr("read") : tr("read and write");
      message.append(tr("\nAdditionally, check you are able to %1 the server config file: %2")
                         .arg(mode, Settings::serverConfigFile()));
    }
    QMessageBox::warning(this, kAppName, message);
  }
}

void MainWindow::startCore()
{
  // Save current IP state when server starts
  if (m_coreProcess.mode() == CoreMode::Server && Settings::value(Settings::Core::Interface).toString().isEmpty()) {
    m_serverStartIPs = NetworkMonitor::validAddresses();
    m_serverStartSuggestedIP = m_serverStartIPs.isEmpty() ? "" : m_serverStartIPs.first();
  }

  m_actionStartCore->setVisible(false);
  m_actionRestartCore->setVisible(true);
  m_coreProcess.start();
}

void MainWindow::stopCore()
{
  qDebug() << "stopping core process";
  m_coreProcess.stop();
  m_actionStartCore->setVisible(true);
  m_actionRestartCore->setVisible(false);
}

void MainWindow::clearSettings()
{
  qDebug() << "clearing settings";

  m_networkMonitor->stopMonitoring();

  disconnect(&m_coreProcess, nullptr, this, nullptr);
  disconnect(&m_versionChecker, nullptr, this, nullptr);
  disconnect(m_guiDupeChecker, nullptr, this, nullptr);
  disconnect(m_trayIcon, nullptr, this, nullptr);
  disconnect(m_logDock->toggleViewAction(), nullptr, this, nullptr);

  m_coreProcess.stop();
  m_coreProcess.clearSettings();

  m_saveOnExit = false;
  diagnostic::clearSettings(true);
}

bool MainWindow::saveServerConfig()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Save server configuration as..."));

  if (!fileName.isEmpty() && !m_serverConfig.save(fileName)) {
    QMessageBox::warning(this, tr("Save failed"), tr("Could not save server configuration to file."));
    return true;
  }

  return false;
}

void MainWindow::openAboutDialog()
{
  AboutDialog about(this);
  about.exec();
}

void MainWindow::openGetNewVersionUrl()
{
  const auto &latest = m_versionChecker.latest();
  if (latest.sha256.isEmpty()) {
    QDesktopServices::openUrl(latest.url.isValid() ? latest.url : QUrl(kUrlDownload));
    return;
  }

  m_updateProgress =
      new QProgressDialog(tr("Downloading %1 %2…").arg(kAppName, latest.version), tr("Cancel"), 0, 100, this);
  m_updateProgress->setWindowModality(Qt::WindowModal);
  m_updateProgress->setAutoClose(false);
  m_updateProgress->setAutoReset(false);
  connect(m_updateProgress, &QProgressDialog::canceled, this, &MainWindow::closeUpdateProgress);
  m_updateProgress->show();
  m_updateDownloader.start(latest.url, latest.fileName, latest.sha256);
}

void MainWindow::chooseReceiveFolder()
{
  const QString folder = QFileDialog::getExistingDirectory(
      this, tr("Choose where received files are saved"), Settings::value(Settings::Gui::FileReceiveDirectory).toString());
  if (!folder.isEmpty()) {
    Settings::setValue(Settings::Gui::FileReceiveDirectory, folder);
  }
}

void MainWindow::setAutoAcceptFiles(bool enabled)
{
  if (!enabled) {
    Settings::setValue(Settings::Gui::FileAutoAccept, false);
    return;
  }
  const auto answer = QMessageBox::warning(
      this, kAppName,
      tr("Accept files automatically?\n\n"
         "Any connected Mono Sync peer can then save files into %1 without asking. "
         "Existing files are never replaced.")
          .arg(Settings::value(Settings::Gui::FileReceiveDirectory).toString()),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer != QMessageBox::Yes) {
    m_actionAutoAccept->setChecked(false);
    return;
  }
  Settings::setValue(Settings::Gui::FileAutoAccept, true);
}

void MainWindow::checkForUpdates()
{
  m_manualUpdateCheck = true;
  m_versionChecker.checkLatest();
}

void MainWindow::onUpdateFound(const QString &version)
{
  m_manualUpdateCheck = false;
  m_statusBar->updateFound(version);
}

void MainWindow::onUpToDate()
{
  if (m_manualUpdateCheck) {
    m_manualUpdateCheck = false;
    statusBar()->showMessage(tr("%1 is up to date.").arg(kAppName), 8000);
  }
}

void MainWindow::onUpdateCheckFailed(const QString &error)
{
  if (m_manualUpdateCheck) {
    m_manualUpdateCheck = false;
    QMessageBox::warning(this, kAppName, tr("Could not check for updates:\n%1").arg(error));
  }
}

void MainWindow::onUpdateDownloaded(const QString &path)
{
  closeUpdateProgress();
  statusBar()->showMessage(tr("Update saved to %1. Run it to install.").arg(path), 8000);
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::onUpdateDownloadFailed(const QString &error)
{
  closeUpdateProgress();
  QMessageBox::warning(this, kAppName, tr("Could not download the update:\n%1").arg(error));
}

void MainWindow::closeUpdateProgress()
{
  m_updateDownloader.cancel();
  if (m_updateProgress) {
    m_updateProgress->disconnect(this);
    m_updateProgress->close();
    m_updateProgress->deleteLater();
    m_updateProgress = nullptr;
  }
}

void MainWindow::openSettings()
{
  auto dialog = SettingsDialog(this, m_serverConfig);

  connect(&dialog, &SettingsDialog::requestRemoveAllSettings, this, &MainWindow::clearSettings, Qt::UniqueConnection);
  if (dialog.exec() == QDialog::Accepted) {
    Settings::save();
    disconnect(&dialog, &SettingsDialog::requestRemoveAllSettings, nullptr, nullptr);

    applyConfig();

    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
  }
}

void MainWindow::resetCore()
{
  m_coreProcess.restart();
}

void MainWindow::showMyFingerprint()
{
  FingerprintDialog fingerprintDialog(this, m_fingerprint);
  fingerprintDialog.exec();
}

void MainWindow::coreModeToggled(bool checked)
{
  // this method is called when rbClient or rbServer toggles
  // with both being in the same group one must be turned on if the other is turned off
  // only react to toggle on to avoid calling everything twice when the user switches modes
  if (!checked)
    return;

  Settings::CoreMode mode = Settings::CoreMode::None;

  if (ui->rbModeServer->isChecked())
    mode = Settings::CoreMode::Server;
  if (ui->rbModeClient->isChecked())
    mode = Settings::CoreMode::Client;

  qDebug() << QStringLiteral("change mode to: %1").arg(QVariant::fromValue(mode).toString());

  if (m_coreProcess.isStarted() && m_coreProcess.mode() != mode)
    m_coreProcess.stop();
  m_coreProcess.setMode(mode);

  Settings::setValue(Settings::Core::CoreMode, mode);
  Settings::save();

  updateModeControls();
}

void MainWindow::updateModeControls()
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;

  ui->serverOptions->setVisible(isServer);
  ui->clientOptions->setVisible(isClient);
  ui->lblNoMode->setVisible(!isServer && !isClient);
  ui->btnCopyAddress->setVisible(isServer);
  ui->btnTestConnection->setVisible(isClient);
  ui->btnUseLastServer->setVisible(isClient);
  ui->btnUseLastServer->setEnabled(isClient &&
                                   !Settings::value(Settings::Client::LastSuccessfulRemoteHost).toString().isEmpty());
  ui->btnTestConnection->setEnabled(isClient && !ui->lineHostname->text().trimmed().isEmpty() &&
                                    !m_connectionProbeActive);
  toggleCanRunCore(canRunCore());

  ui->lblIpAddresses->setVisible(
      (isClient && !Settings::value(Settings::Core::Interface).toString().isEmpty()) || isServer
  );

  if (ui->lblIpAddresses->isVisible())
    updateNetworkInfo();

  if (isServer) {
    m_networkMonitor->startMonitoring();
  } else {
    m_networkMonitor->stopMonitoring();
  }

  if (isServer || isClient)
    updateModeControlLabels();
}

void MainWindow::updateModeControlLabels()
{
  const bool isServer = m_coreProcess.mode() == CoreMode::Server;
  const bool isStarted = m_coreProcess.isStarted();

  QString startText;
  QString stopText;
  QIcon startIcon;
  QIcon stopIcon;

  if (isServer) {
    startText = tr("Start");
    stopText = tr("Stop");
    startIcon = QIcon::fromTheme(QStringLiteral("system-run"));
    stopIcon = QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop);
  } else {
    startText = tr("Connect");
    stopText = tr("Disconnect");
    startIcon = QIcon::fromTheme(QStringLiteral("network-connect"));
    stopIcon = QIcon::fromTheme(QStringLiteral("network-disconnect"));
  }

  m_actionStartCore->setText(startText);
  m_actionStartCore->setIcon(startIcon);
  m_actionStopCore->setText(stopText);
  m_actionStopCore->setIcon(stopIcon);

  if (isStarted) {
    ui->btnToggleCore->setText(stopText);
    ui->btnToggleCore->setIcon(stopIcon);
  } else {
    ui->btnToggleCore->setText(startText);
    ui->btnToggleCore->setIcon(startIcon);
  }
}

void MainWindow::updateSecurityIcon(bool visible)
{
  m_statusBar->setSecurityIconVisible(visible);
  if (!visible)
    return;
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
}

void MainWindow::updateNetworkInfo()
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  if (mode == CoreMode::Server)
    updateIpLabel(NetworkMonitor::validAddresses());
  else
    updateIpLabel({Settings::value(Settings::Core::Interface).toString()});
}

void MainWindow::serverConnectionConfigureClient(const QString &clientName)
{
  m_serverConfigDialogVisible = true;
  ServerConfigDialog dialog(this, m_serverConfig);
  if (dialog.addClient(clientName) && dialog.exec() == QDialog::Accepted) {
    m_coreProcess.restart();
  }
  m_serverConfigDialogVisible = false;
}

//////////////////////////////////////////////////////////////////////////////
// End slots
//////////////////////////////////////////////////////////////////////////////

void MainWindow::open()
{
  if (!Settings::value(Settings::Gui::Autohide).toBool())
    showAndActivate();
  else if (deskflow::platform::isMac())
    // macOS to call hide after this function ends
    QTimer::singleShot(1, this, &MainWindow::hide);
  else
    hide();

  // if a critical error was shown just before the main window (i.e. on app
  // load), it will be hidden behind the main window. therefore we need to raise
  // it up in front of the main window.
  // HACK: because the `onShown` event happens just as the window is shown, the
  // message box has a chance of being raised under the main window. to solve
  // this we delay the error dialog raise by a split second. this seems a bit
  // hacky and fragile, so maybe there's a better approach.
  const auto kCriticalDialogDelay = 100;
  QTimer::singleShot(kCriticalDialogDelay, this, &messages::raiseCriticalDialog);

  if (!Settings::isSet(Settings::Gui::AutoUpdateCheck)) {
    Settings::setValue(Settings::Gui::AutoUpdateCheck, messages::showUpdateCheckOption(this));
  }

  if (Settings::value(Settings::Gui::AutoUpdateCheck).toBool()) {
    m_versionChecker.checkLatest();
  } else {
    qDebug() << "skipping check for new version, disabled";
  }

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    if (ui->rbModeClient->isChecked() && ui->lineHostname->text().isEmpty())
      return;
    startCore();
  }
}

void MainWindow::createMenuBar()
{
  m_menuFile->addAction(m_actionSendFile);
  m_menuFile->addAction(m_actionReceiveFolder);
  m_menuFile->addAction(m_actionAutoAccept);
  m_menuFile->addAction(m_actionForgetTrusted);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionStartCore);
  m_menuFile->addAction(m_actionRestartCore);
  m_menuFile->addAction(m_actionStopCore);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionQuit);

  m_menuEdit->addAction(m_actionSettings);

  m_menuView->addAction(m_logDock->toggleViewAction());

  m_menuHelp->addAction(m_actionAbout);
  m_menuHelp->addAction(m_actionCheckUpdates);
  m_menuHelp->addAction(m_actionShowHelp);

  auto menuBar = new QMenuBar(this);
  menuBar->addMenu(m_menuFile);
  menuBar->addMenu(m_menuEdit);
  menuBar->addMenu(m_menuView);
  menuBar->addMenu(m_menuHelp);

  setMenuBar(menuBar);
}

void MainWindow::setupTrayIcon()
{
  auto trayMenu = new QMenu(this);
  trayMenu->addActions({m_actionStartCore, m_actionRestartCore, m_actionStopCore, m_actionSendFile, m_actionMinimize,
                        m_actionRestore, m_actionTrayQuit});
  trayMenu->insertSeparator(m_actionSendFile);
  trayMenu->insertSeparator(m_actionMinimize);
  trayMenu->insertSeparator(m_actionTrayQuit);
  m_trayIcon->setContextMenu(trayMenu);

  setTrayIcon();
  m_trayIcon->show();
}

void MainWindow::applyConfig()
{
  if (Settings::value(Settings::Gui::ShowVersionInTitle).toBool()) {
    setWindowTitle(QStringLiteral("%1 - %2").arg(kAppName, kDisplayVersion));
  } else {
    setWindowTitle(kAppName);
  }

  if (const auto host = Settings::value(Settings::Client::RemoteHost).toString(); !host.isEmpty())
    ui->lineHostname->setText(host);

  updateFingerprintButton();
  setTrayIcon();

  if (const auto ip = Settings::value(Settings::Core::Interface).toString(); !ip.isEmpty()) {
    m_serverStartIPs = {ip};
    m_serverStartSuggestedIP = ip;
  }

  coreModeToggled(true);
}

void MainWindow::saveSettings() const
{
  if (ui->rbModeClient->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  } else if (ui->rbModeServer->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
  }
  if (!ui->lineHostname->text().isEmpty())
    Settings::setValue(Settings::Client::RemoteHost, ui->lineHostname->text());
  Settings::save();
}

void MainWindow::setTrayIcon()
{
  static const auto fallbackPath = QStringLiteral(":/icons/mono-sync-%1/apps/64/%2");

  QString themeIcon = kRevFqdnName;
  if (!Settings::value(Settings::Gui::SymbolicTrayIcon).toBool()) {
    if (deskflow::platform::isMac())
      m_trayIcon->setIcon(QIcon::fromTheme(themeIcon));
    else
      m_trayIcon->setIcon(QIcon(fallbackPath.arg(QStringLiteral("dark"), themeIcon)));
    return;
  }

  themeIcon.append(QStringLiteral("-symbolic"));

  if (deskflow::platform::isWindows()) {
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QString theme = settings.value(QStringLiteral("SystemUsesLightTheme"), 1).toBool() ? QStringLiteral("light")
                                                                                             : QStringLiteral("dark");
    m_trayIcon->setIcon(QIcon(fallbackPath.arg(theme, themeIcon)));
    return;
  }

  auto icon = QIcon::fromTheme(themeIcon, QIcon(fallbackPath.arg(iconMode(), themeIcon)));
  icon.setIsMask(true);
  m_trayIcon->setIcon(icon);
}

void MainWindow::handleLogLine(const QString &line)
{
  m_logDock->appendLine(line);
}

void MainWindow::handleUnrecognisedClient(const QString &clientName)
{
  if (m_ignoredClients.contains(clientName)) {
    qDebug("ignoring %s:", qPrintable(clientName));
    return;
  }

  if (m_newClientPromptShowing || m_serverConfigDialogVisible)
    return;

  if (Settings::value(Settings::Server::ExternalConfig).toBool())
    return;

  if (m_serverConfig.isFull() || m_serverConfig.computerExists(clientName))
    return;

  m_newClientPromptShowing = true;

  showAndActivate();

  if (deskflow::gui::messages::showNewClientPrompt(this, clientName)) {
    serverConnectionConfigureClient(clientName);
  } else {
    m_ignoredClients.insert(clientName);
  }

  m_newClientPromptShowing = false;
}

void MainWindow::handleConnectionRefused(deskflow::core::ConnectionRefusal reason)
{
  if (reason != deskflow::core::ConnectionRefusal::AlreadyConnected)
    return;

  if (!isVisible() || m_clientErrorVisible)
    return;

  m_clientErrorVisible = true;
  showAndActivate();

  const auto address = Settings::value(Settings::Client::RemoteHost).toString();
  QMessageBox::warning(
      this, tr("%1 Connection Error").arg(kAppName),
      tr("<p>Failed to connect to the server '%1'.</p>"
         "<p>A Client with your name is already connected to the server.</p>"
         "Please ensure that you're using a unique name and that only a "
         "single instance of the client process is running.</p>")
          .arg(address)
  );

  m_clientErrorVisible = false;
}

void MainWindow::handleMissingKeyboardLayouts(const QString &layouts)
{
  if (Settings::value(Settings::Gui::IgnoreMissingKeyboardLayouts).toBool())
    return;

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(tr("Missing Keyboard Layouts"));
  msgBox.setText(tr("<p>Keyboard layout support requires matching layouts on all computers. "
                    "The following layouts from the other computer are not installed on this computer:</p>"
                    "<p><b>%1</b></p>"
                    "<p>Please install them to enable support for these layouts.</p>")
                     .arg(layouts));

  auto *checkBox = new QCheckBox(tr("Don't show this again"), &msgBox);
  msgBox.setCheckBox(checkBox);
  msgBox.exec();

  if (checkBox->isChecked()) {
    Settings::setValue(Settings::Gui::IgnoreMissingKeyboardLayouts, true);
  }
}

void MainWindow::handlePeerFingerprint(const QString &fingerprint)
{
  const auto sha256Text = QString(fingerprint).remove(':');
  const Fingerprint sha256 = {QCryptographicHash::Sha256, QByteArray::fromHex(sha256Text.toLatin1())};

  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  if ((isClient && m_checkedServers.contains(sha256Text)) || (!isClient && m_checkedClients.contains(sha256Text))) {
    qDebug("ignoring fingerprint, already handled");
    return;
  }

  FingerprintDatabase db;
  db.read(trustedFingerprintDatabase());

  if (db.isTrusted(sha256)) {
    qDebug("fingerprint is trusted");
    return;
  }

  if (isClient) {
    m_checkedServers.append(sha256Text);
    m_coreProcess.stop();
  } else {
    m_checkedClients.append(sha256Text);
  }

  auto mode = isClient ? FingerprintDialogMode::Client : FingerprintDialogMode::Server;
  FingerprintDialog fingerprintDialog(this, m_fingerprint, mode, sha256);

  if (fingerprintDialog.exec() == QDialog::Accepted) {
    db.addTrusted(sha256);
    if (!db.write(trustedFingerprintDatabase())) {
      qCritical().noquote() << "unable to write fingerprint to db:" << trustedFingerprintDatabase();
      m_coreProcess.stop();
      return;
    }
    if (isClient) {
      m_checkedServers.removeAll(sha256Text);
      m_coreProcess.start();
    } else {
      m_checkedClients.removeAll(sha256Text);
    }
  }
}

bool MainWindow::maybeHideToTray()
{
  if (!Settings::value(Settings::Gui::CloseToTray).toBool()) {
    return false;
  }

  if (Settings::value(Settings::Gui::CloseReminder).toBool()) {
    messages::showCloseReminder(this);
    Settings::setValue(Settings::Gui::CloseReminder, false);
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
  qDebug() << "hiding to tray";
  hide();
  return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (!m_closeAfterFileWorkerStops && event->spontaneous() && maybeHideToTray()) {
    event->ignore();
    return;
  }

  if (m_fileWorkerThread != nullptr && m_fileWorkerThread->isRunning()) {
    m_closeAfterFileWorkerStops = true;
    if (!m_outgoingTransferId.isEmpty()) {
      const QJsonObject cancel{{QStringLiteral("action"), QStringLiteral("cancel")},
                               {QStringLiteral("id"), m_outgoingTransferId},
                               {QStringLiteral("peer"), m_outgoingPeer}};
      m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(cancel));
    }
    clearFileTransfer();
    statusBar()->showMessage(tr("Stopping file operation before quitting…"));
    event->ignore();
    return;
  }
  m_closeAfterFileWorkerStops = false;

  if (m_saveOnExit) {
    Settings::setValue(Settings::Gui::WindowGeometry, geometry());
    Settings::setValue(Settings::Gui::AutoStartCore, m_coreProcess.isStarted());
  }
  qDebug() << "quitting application";

  // any connected dock view acitons will be triggered
  // disconnect them before accepting the event
  disconnect(m_logDock->toggleViewAction(), &QAction::toggled, nullptr, nullptr);

  event->accept();
  QApplication::quit();
}

void MainWindow::showFirstConnectedMessage()
{
  if (Settings::value(Settings::Gui::ShownFirstConnectedMessage).toBool())
    return;
  Settings::setValue(Settings::Gui::ShownFirstConnectedMessage, true);
  messages::showFirstConnectedMessage(this);
}

void MainWindow::updateStatus()
{
  using enum ProcessState;
  const auto connection = m_coreProcess.connectionState();
  const auto process = m_coreProcess.processState();
  const bool isServer = (m_coreProcess.mode() == CoreMode::Server);
  if (process == Stopped || process == Started) {
    updateNetworkInfo();
    ui->btnEditName->setVisible(process == Stopped);
  }
  m_statusBar->setStatus(connection, process, isServer);
}

void MainWindow::coreProcessStateChanged(ProcessState state)
{
  using enum ProcessState;
  updateStatus();
  if (state == Started) {
    qDebug() << "recording that core has started";
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    if (m_coreProcess.mode() == CoreMode::Server &&
        !Settings::value(Settings::Gui::ShownServerFirstStartMessage).toBool()) {
      qDebug() << "starting core server for first time";
      messages::showFirstServerStartMessage(this);
      Settings::setValue(Settings::Gui::ShownServerFirstStartMessage, true);
    }
  }

  if (state == Started || state == Starting || state == RetryPending) {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(true);
    m_actionStartCore->setVisible(false);
    m_actionRestartCore->setVisible(true);
    m_actionStopCore->setEnabled(true);

    if (state == Starting) {
      saveSettings();
    }

  } else {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(false);
    m_actionStartCore->setVisible(true);
    m_actionRestartCore->setVisible(false);
    m_actionStopCore->setEnabled(false);
  }
  updateModeControlLabels();
}

void MainWindow::coreConnectionStateChanged(ConnectionState state)
{
  qDebug() << "core connection state changed:" << static_cast<int>(state);

  if (state == ConnectionState::Connected && m_coreProcess.mode() == CoreMode::Client) {
    const auto remoteHost = Settings::value(Settings::Client::RemoteHost).toString().trimmed();
    if (!remoteHost.isEmpty() && !remoteHost.contains(',')) {
      Settings::setValue(Settings::Client::LastSuccessfulRemoteHost, remoteHost);
      Settings::save();
      ui->btnUseLastServer->setEnabled(true);
    }
  }

  updateStatus();

  // always assume connection is not secure when connection changes
  // to anything except connected. the only way the padlock shows is
  // when the correct TLS version string is detected.
  if (state != ConnectionState::Connected) {
    secureSocket(false);
    if (!m_fileTransferPeers.isEmpty()) {
      const auto stateName = QMetaEnum::fromType<ConnectionState>().valueToKey(static_cast<int>(state));
      handleLogLine(QStringLiteral("Cleared file-transfer availability after core state changed to %1.")
                        .arg(QString::fromLatin1(stateName)));
    }
    clearFileTransferAvailability(tr("File transfer stopped because the peer disconnected."));
  } else if (isVisible()) {
    showFirstConnectedMessage();
  }
}

void MainWindow::clearFileTransferAvailability(const QString &message)
{
  m_fileTransferPeers.clear();
  m_actionSendFile->setEnabled(false);
  if (m_fileWorkerThread != nullptr || m_fileWorker != nullptr || !m_outgoingTransferId.isEmpty()) {
    clearFileTransfer(message);
  }
}

void MainWindow::updateFingerprintButton()
{
  m_statusBar->setBtnFingerprintVisible(TlsUtility::isEnabled() && !m_fingerprint.data.isEmpty());
}

void MainWindow::hide()
{
#ifdef Q_OS_MACOS
  macOSNativeHide();
#else
  QMainWindow::hide();
#endif
  m_actionRestore->setVisible(true);
  m_actionMinimize->setVisible(false);
}

void MainWindow::changeEvent(QEvent *e)
{
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::PaletteChange) {
    updateIconTheme();
    setWindowIcon(QIcon::fromTheme(kRevFqdnName));
    setTrayIcon();
  } else if (e->type() == QEvent::LanguageChange) {
    ui->retranslateUi(this);
    updateModeControlLabels();
    updateNetworkInfo();
    updateStatus();
    serverClientsChanged({});
    updateText();
  }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (obj != ui->lineEditName || event->type() != QEvent::KeyPress)
    return false;
  if (const auto keyEvent = static_cast<QKeyEvent *>(event); keyEvent->key() != Qt::Key_Escape)
    return false;
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  ui->lineEditName->setText(Settings::value(Settings::Core::ComputerName).toString());
  toggleCanRunCore(canRunCore());
  return true;
}

void MainWindow::updateText()
{
  m_menuFile->setTitle(tr("&File"));
  m_menuEdit->setTitle(tr("&Edit"));
  m_menuView->setTitle(tr("&View"));
  m_menuHelp->setTitle(tr("&Help"));

  m_actionMinimize->setText(tr("&Minimize to tray"));
  m_actionQuit->setText(tr("&Quit"));
  m_actionTrayQuit->setText(tr("&Quit"));
  //: %1 will be the replaced with the appname
  m_actionRestore->setText(tr("&Open %1").arg(kAppName));
  m_actionSettings->setText(tr("&Preferences"));
  m_actionStartCore->setText(tr("&Start"));
  m_actionRestartCore->setText(tr("Rest&art"));
  m_actionStopCore->setText(tr("S&top"));
  //: %1 will be the replaced with the appname
  m_actionAbout->setText(tr("About %1...").arg(kAppName));

  m_actionShowHelp->setText(tr("View &Help"));
  m_actionCheckUpdates->setText(tr("Check for &Updates…"));
  m_actionReceiveFolder->setText(tr("Receive folder…"));
  m_actionAutoAccept->setText(tr("Accept files automatically"));
  m_actionForgetTrusted->setText(tr("Forget trusted senders"));
  m_actionSendFile->setText(tr("Send a file…"));
  m_actionSendFile->setEnabled(false);

  //: start / restart core shortcut
  m_actionStartCore->setShortcut(QKeySequence(tr("Ctrl+S")));
  m_actionRestartCore->setShortcut(QKeySequence(tr("Ctrl+S")));

  //: stop core shortcut
  m_actionStopCore->setShortcut(QKeySequence(tr("Ctrl+T")));

  if (deskflow::platform::isWindows()) {
    //: Quit shortcut
    m_actionQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    m_actionTrayQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
  }
}

void MainWindow::showConfigureServer(const QString &message)
{
  ServerConfigDialog dialog(this, serverConfig());
  dialog.message(message);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::showConfigureClient()
{
  ClientConfigDialog dialog(this);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::secureSocket(bool secureSocket)
{
  m_secureSocket = secureSocket;
  updateSecurityIcon(m_statusBar->securityIconVisible());
}

void MainWindow::updateComputerName()
{
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();
  ui->lblComputerName->setText(computerName);
  ui->lineEditName->setText(computerName);
  m_serverConfig.updateServerName();
}

void MainWindow::showAndActivate()
{
  const auto wasVisible = isVisible();
#ifdef Q_OS_MACOS
  forceAppActive();
#endif
  showNormal();
  raise();
  activateWindow();
  m_actionRestore->setVisible(false);
  m_actionMinimize->setVisible(true);
  if (!wasVisible)
    restoreWindow();
}

void MainWindow::showHostNameEditor()
{
  ui->lineEditName->show();
  ui->lblComputerName->hide();
  ui->btnEditName->hide();
  toggleCanRunCore(false);
  ui->lineEditName->setFocus();
}

void MainWindow::setHostName()
{
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  toggleCanRunCore(canRunCore());

  QString text = ui->lineEditName->text();
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();

  if (text == computerName)
    return;

  const bool isServer = ui->rbModeServer->isChecked();
  bool existingComputer = false;
  if (isServer)
    existingComputer = serverConfig().computerExists(text);

  if (!ui->lineEditName->hasAcceptableInput() || text.isEmpty() || existingComputer) {
    blockSignals(true);
    ui->lineEditName->setText(computerName);
    blockSignals(false);

    const auto title = tr("Invalid Computer Name");
    QString body;
    if (existingComputer) {
      body = tr("Computer name already exists");
    } else {
      body =
          tr("The name you have chosen is invalid.\n\n"
             "Valid names:\n"
             "• Use letters and numbers\n"
             "• May also use _ or -\n"
             "• Are between 1 and 255 characters");
    }
    QMessageBox::information(this, title, body);
    return;
  }

  ui->lblComputerName->setText(ui->lineEditName->text());
  Settings::setValue(Settings::Core::ComputerName, ui->lineEditName->text());
  if (isServer)
    serverConfig().updateServerName();
  applyConfig();
}

QString MainWindow::trustedFingerprintDatabase() const
{
  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  return isClient ? Settings::tlsTrustedServersDb() : Settings::tlsTrustedClientsDb();
}

bool MainWindow::generateCertificate()
{
  const auto certificate = Settings::value(Settings::Security::Certificate).toString();
  if (!QFile::exists(certificate) && !TlsUtility::generateCertificate()) {
    return false;
  }

  m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
  updateFingerprintButton();
  return true;
}

void MainWindow::serverClientsChanged(const QStringList &clients)
{
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  m_statusBar->setServerClients(clients);
}

void MainWindow::daemonIpcClientConnectionFailed()
{
  if (deskflow::gui::messages::showDaemonOffline(this)) {
    m_coreProcess.retryDaemon();
  }
}

void MainWindow::toggleCanRunCore(bool enableButtons)
{
  const bool isStarted = m_coreProcess.isStarted();
  ui->btnToggleCore->setEnabled(enableButtons);
  ui->btnRestartCore->setEnabled(enableButtons && isStarted);
  m_actionStartCore->setEnabled(enableButtons);
  m_actionStopCore->setEnabled(enableButtons && isStarted);
}

void MainWindow::remoteHostChanged(const QString &newRemoteHost)
{
  m_coreProcess.setAddress(newRemoteHost);
  toggleCanRunCore(!newRemoteHost.isEmpty() && ui->rbModeClient->isChecked());
  ui->btnTestConnection->setEnabled(!newRemoteHost.trimmed().isEmpty() && !m_connectionProbeActive);
  ui->btnUseLastServer->setEnabled(
      ui->rbModeClient->isChecked() &&
      !Settings::value(Settings::Client::LastSuccessfulRemoteHost).toString().isEmpty()
  );
  if (newRemoteHost.isEmpty()) {
    Settings::setValue(Settings::Client::RemoteHost);
  } else {
    Settings::setValue(Settings::Client::RemoteHost, newRemoteHost);
  }
}

void MainWindow::updateIpLabel(const QStringList &addresses)
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  static const auto colorText = QStringLiteral(R"(<span style="color:%1;">%2</span>)");
  const bool serverStarted = m_coreProcess.isStarted();
  const bool fixedIP = !Settings::value(Settings::Core::Interface).toString().isEmpty();

  if (!fixedIP && addresses.isEmpty() && !serverStarted || (serverStarted && m_serverStartSuggestedIP.isEmpty())) {
    m_currentIpAddress.clear();
    ui->btnCopyAddress->setEnabled(false);
    ui->lblIpAddresses->setText(colorText.arg(palette().linkVisited().color().name(), tr("No IP Detected")));
    ui->lblIpAddresses->setToolTip(tr("Unable to detect an IP address. Check your network connection is active."));
    return;
  }

  QString labelText = fixedIP ? tr("Using IP: ") : tr("Suggested IP: ");
  QString toolTipText = tr("<p>If connecting via the hostname fails, try %1</p>");

  const bool filterIpList = (serverStarted || fixedIP);
  const QRegularExpression ipListFilter(filterIpList ? QStringLiteral("(%1)").arg(m_serverStartIPs.join("|")) : "");
  const QStringList ipList = addresses.filter(ipListFilter);

  bool IPValid = true;
  if (filterIpList && (m_serverStartSuggestedIP != m_currentIpAddress) || !ipList.contains(m_serverStartSuggestedIP)) {
    IPValid = !ipList.isEmpty();
  }

  if (IPValid) {
    m_currentIpAddress = ipList.first();
    labelText.append(m_currentIpAddress);
    if (mode == CoreMode::Server && !fixedIP) {
      const auto candidates = NetworkMonitor::rankedCandidates();
      const auto candidate = std::ranges::find_if(candidates, [this](const auto &entry) {
        return entry.address.toString() == m_currentIpAddress;
      });
      if (candidate != candidates.cend()) {
        const auto interfaceName = candidate->interfaceName.toHtmlEscaped();
        const auto prefix = candidate->prefixLength >= 0 ? QStringLiteral("/%1").arg(candidate->prefixLength)
                                                          : tr("unknown prefix");
        labelText.append(tr(" (%1, %2)").arg(interfaceName, prefix));
      }
    }
  } else {
    m_currentIpAddress.clear();
    labelText.append(colorText.arg(palette().linkVisited().color().name(), m_serverStartSuggestedIP));
    toolTipText.append(tr("\nA bound IP is now invalid, you may need to restart the server."));
  }

  if (ipList.count() < 2 || fixedIP) {
    toolTipText = toolTipText.arg(tr("the suggested IP."));
  } else {
    QStringList alternatives;
    for (const auto &candidate : NetworkMonitor::rankedCandidates()) {
      const auto address = candidate.address.toString();
      if (!ipList.contains(address))
        continue;
      const auto prefix = candidate.prefixLength >= 0 ? QStringLiteral("/%1").arg(candidate.prefixLength)
                                                      : tr("unknown prefix");
      alternatives.append(tr("%1 (%2, %3)").arg(address, candidate.interfaceName.toHtmlEscaped(), prefix));
    }
    toolTipText = toolTipText.arg(tr("one of the following IPs:<br/>%1").arg(alternatives.join("<br/>")));
  }

  ui->lblIpAddresses->setText(labelText);
  ui->lblIpAddresses->setToolTip(
      mode == CoreMode::Server
          ? toolTipText +
                tr("<p>Direct subnet addresses are suggested before single-host routes; other addresses remain listed "
                   "for VPN or routed connections.</p>")
          : QString()
  );
  ui->btnCopyAddress->setEnabled(mode == CoreMode::Server && !m_currentIpAddress.isEmpty());
}

void MainWindow::copyServerAddress()
{
  if (m_currentIpAddress.isEmpty())
    return;

  QGuiApplication::clipboard()->setText(m_currentIpAddress);
  m_statusBar->showMessage(tr("Server address copied: %1").arg(m_currentIpAddress), 7000);
}

void MainWindow::useLastSuccessfulServer()
{
  const auto host = Settings::value(Settings::Client::LastSuccessfulRemoteHost).toString();
  if (!host.isEmpty())
    ui->lineHostname->setText(host);
}

void MainWindow::testServerConnection()
{
  if (m_connectionProbeActive)
    return;

  const auto hosts = ui->lineHostname->text().split(',', Qt::SkipEmptyParts);
  const auto host = hosts.value(0).trimmed();
  if (host.isEmpty()) {
    m_statusBar->showMessage(tr("Enter the server address first."), 5000);
    return;
  }

  const int port = Settings::value(Settings::Core::Port).toInt();
  if (port < 1 || port > 65535) {
    m_statusBar->showMessage(tr("The configured server port is invalid."), 7000);
    return;
  }
  m_connectionProbeSocket->abort();
  m_connectionProbeActive = true;
  ui->btnTestConnection->setEnabled(false);
  m_statusBar->showMessage(tr("Checking only the first entered endpoint %1:%2…").arg(host).arg(port));
  m_connectionProbeSocket->connectToHost(host, static_cast<quint16>(port));
  m_connectionProbeTimer->start(3000);
}

void MainWindow::updateTimeoutDelay(int newDelay)
{
  m_statusBar->setConnectionInterval(newDelay);
}

void MainWindow::setHelpFilePath()
{
  const QString appPath = QCoreApplication::applicationDirPath();
  auto buildPath = QString("%1/../docs/HelpMain.md").arg(appPath);
  auto installPath = QString("%1/../share/doc/%2/HelpMain.md").arg(appPath, kAppId);
  if (deskflow::platform::isMac()) {
    installPath = QString("%1/../Resources/docs/HelpMain.md").arg(appPath);
    buildPath = QString("%1/../../../../docs/HelpMain.md").arg(appPath);
  } else if (deskflow::platform::isWindows()) {
    installPath = QString("%1/docs/HelpMain.md").arg(appPath);
  }

  buildPath = QDir::cleanPath(buildPath);
  installPath = QDir::cleanPath(installPath);

  if (QFile::exists(installPath))
    m_helpPath = QUrl::fromLocalFile(installPath);
  else if (QFile::exists(buildPath))
    m_helpPath = QUrl::fromLocalFile(buildPath);
  else
    m_helpPath = kUrlWiki;
}

void MainWindow::showHelpViewer() const
{
  if (m_helpPath.isLocalFile()) {
    HelpDialog dialogHelp(this->centralWidget(), m_helpPath);
    dialogHelp.exec();
  } else {
    QDesktopServices::openUrl(m_helpPath);
  }
}

namespace {

void addQueuedRow(TransferPanel *panel, const QString &path, const QString &peer)
{
  const QFileInfo info(path);
  panel->addRow(path, info.fileName(), peer, static_cast<quint64>(info.size()), false, TransferPanel::State::Queued);
}

} // namespace

void MainWindow::startFileWorker()
{
  if (m_fileWorker != nullptr) {
    return;
  }
  m_fileWorkerThread = new QThread(this);
  m_fileWorker = new OutgoingFileWorker;
  auto *worker = m_fileWorker;
  m_fileWorker->moveToThread(m_fileWorkerThread);

  connect(worker, &OutgoingFileWorker::hashingProgress, this, [this, worker](quint64 processed, quint64 total) {
    if (m_fileWorker == worker && !m_outgoingPath.isEmpty()) {
      m_transferPanel->setProgress(m_outgoingPath, processed, total);
    }
  });
  connect(worker, &OutgoingFileWorker::prepared, this, [this, worker](const QString &name, quint64 size,
                                                                      const QByteArray &sha256) {
    if (m_fileWorker == worker) {
      prepareOutgoingFile(name, size, sha256);
    }
  });
  connect(worker, &OutgoingFileWorker::chunkReady, this, [this, worker](const QByteArray &chunk, quint64 position) {
    if (m_fileWorker == worker) {
      sendOutgoingChunk(chunk, position);
    }
  });
  connect(worker, &OutgoingFileWorker::receiverReady, this,
          [this, worker](bool success, const QString &error) { receiveWorkerReady(worker, success, error); });
  connect(worker, &OutgoingFileWorker::receiveChunkSaved, this,
          [this, worker](bool success, quint64 bytes, quint64 received, quint64 total, const QString &error) {
            receiveWorkerChunkSaved(worker, success, bytes, received, total, error);
          });
  connect(worker, &OutgoingFileWorker::receiveFinished, this,
          [this, worker](bool success, const QString &path, const QString &error) {
            receiveWorkerFinished(worker, success, path, error);
          });
  connect(worker, &OutgoingFileWorker::failed, this, [this, worker](const QString &error) {
    if (m_fileWorker == worker) {
      if (!m_outgoingTransferId.isEmpty()) {
        const QJsonObject cancel{{QStringLiteral("action"), QStringLiteral("cancel")},
                                 {QStringLiteral("id"), m_outgoingTransferId},
                                 {QStringLiteral("peer"), m_outgoingPeer}};
        m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(cancel));
      }
      clearFileTransfer(error);
    }
  });
  connect(m_fileWorkerThread, &QThread::finished, worker, &QObject::deleteLater);
  auto *thread = m_fileWorkerThread;
  connect(thread, &QThread::finished, this, [this, thread] {
    if (m_fileWorkerThread == thread) {
      m_fileWorkerThread = nullptr;
    }
    thread->deleteLater();
    if (m_closeAfterFileWorkerStops) {
      close();
    } else if (!m_sendQueue.isEmpty()) {
      QTimer::singleShot(0, this, &MainWindow::sendNextQueuedFile);
    }
  });
  m_fileWorkerThread->start();
}

void MainWindow::sendFile()
{
  if (m_closeAfterFileWorkerStops) {
    return;
  }
  if (m_fileWorkerThread != nullptr) {
    statusBar()->showMessage(tr("Wait for the previous file operation to stop."), 8000);
    return;
  }
  const QString peer = pickSendPeer();
  if (peer.isEmpty()) {
    return;
  }
  if (!isPeerReadyForFile(peer)) {
    statusBar()->showMessage(tr("The selected peer disconnected. Try again when it reconnects."), 8000);
    return;
  }

  const QString path = QFileDialog::getOpenFileName(this, tr("Choose a file to send"));
  if (path.isEmpty()) {
    return;
  }
  if (!isPeerReadyForFile(peer)) {
    statusBar()->showMessage(tr("The selected peer disconnected. Try again when it reconnects."), 8000);
    return;
  }
  startSending(peer, path);
}

QString MainWindow::pickSendPeer()
{
  QStringList peers;
  for (auto iterator = m_fileTransferPeers.cbegin(); iterator != m_fileTransferPeers.cend(); ++iterator) {
    if (iterator.value()) {
      peers.append(iterator.key());
    }
  }
  if (peers.isEmpty()) {
    QMessageBox::information(this, kAppName, tr("File transfer is available only with a connected Mono Sync peer."));
    return QString();
  }
  if (peers.size() == 1) {
    return peers.first();
  }
  bool accepted = false;
  const QString peer = QInputDialog::getItem(this, tr("Send file"), tr("Connected peer:"), peers, 0, false, &accepted);
  return accepted ? peer : QString();
}

bool MainWindow::isPeerReadyForFile(const QString &peer) const
{
  return m_fileWorkerThread == nullptr && m_fileWorker == nullptr && m_outgoingTransferId.isEmpty() &&
         m_fileTransferPeers.value(peer, false);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasUrls() && m_actionSendFile->isEnabled()) {
    event->acceptProposedAction();
    QStringList peers;
    for (auto iterator = m_fileTransferPeers.cbegin(); iterator != m_fileTransferPeers.cend(); ++iterator) {
      if (iterator.value()) {
        peers.append(iterator.key());
      }
    }
    m_dropOverlay->setText(peers.size() == 1 ? tr("Drop to send to %1").arg(peers.first()) : tr("Drop to send"));
    const QColor accent = palette().color(QPalette::Highlight);
    m_dropOverlay->setStyleSheet(QStringLiteral("QLabel { border: 2px dashed %1; background-color: rgba(%2, %3, %4, 51); }")
                                     .arg(accent.name(), QString::number(accent.red()),
                                          QString::number(accent.green()), QString::number(accent.blue())));
    m_dropOverlay->setGeometry(centralWidget()->rect());
    m_dropOverlay->raise();
    m_dropOverlay->show();
  } else {
    event->ignore();
  }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
  QMainWindow::dragLeaveEvent(event);
  m_dropOverlay->hide();
}

void MainWindow::dropEvent(QDropEvent *event)
{
  m_dropOverlay->hide();
  QStringList paths;
  for (const auto &url : event->mimeData()->urls()) {
    if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
      paths.append(url.toLocalFile());
    }
  }
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  queueFilesToSend(paths);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
  QMainWindow::resizeEvent(event);
  m_dropOverlay->setGeometry(centralWidget()->rect());
}

void MainWindow::queueFilesToSend(const QStringList &paths)
{
  if (m_closeAfterFileWorkerStops || m_receivingFile) {
    statusBar()->showMessage(tr("Wait for the current file operation to finish."), 8000);
    return;
  }
  const bool sending = m_fileWorker != nullptr || m_fileWorkerThread != nullptr || !m_outgoingTransferId.isEmpty();
  const QString peer = sending ? m_outgoingPeer : pickSendPeer();
  if (peer.isEmpty()) {
    return;
  }
  if (sending) {
    m_sendQueue.append(paths);
    m_sendQueuePeer = peer;
    for (const auto &path : paths) {
      addQueuedRow(m_transferPanel, path, peer);
    }
    statusBar()->showMessage(tr("Queued %1 file(s) to send.").arg(paths.size()), 8000);
    return;
  }
  if (!isPeerReadyForFile(peer)) {
    statusBar()->showMessage(tr("The selected peer disconnected. Try again when it reconnects."), 8000);
    return;
  }
  m_sendQueue = paths.mid(1);
  m_sendQueuePeer = peer;
  for (const auto &path : m_sendQueue) {
    addQueuedRow(m_transferPanel, path, peer);
  }
  startSending(peer, paths.first());
}

void MainWindow::sendNextQueuedFile()
{
  if (m_sendQueue.isEmpty()) {
    return;
  }
  if (!isPeerReadyForFile(m_sendQueuePeer)) {
    const QString reason = tr("Queued files were not sent because the peer disconnected.");
    failQueuedRows(reason);
    m_sendQueue.clear();
    statusBar()->showMessage(reason, 8000);
    return;
  }
  const QString path = m_sendQueue.takeFirst();
  startSending(m_sendQueuePeer, path);
}

void MainWindow::startSending(const QString &peer, const QString &path)
{
  m_outgoingPeer = peer;
  m_outgoingPath = path;
  const QFileInfo info(path);
  m_transferPanel->addRow(path, info.fileName(), peer, static_cast<quint64>(info.size()), false,
                          TransferPanel::State::Preparing);
  startFileWorker();
  auto *worker = m_fileWorker;
  QMetaObject::invokeMethod(worker, [worker, path] { worker->prepare(path); }, Qt::QueuedConnection);
}

void MainWindow::prepareOutgoingFile(const QString &fileName, quint64 size, const QByteArray &sha256)
{
  if (m_fileWorker == nullptr || m_outgoingPeer.isEmpty()) {
    clearFileTransfer();
    return;
  }
  m_outgoingFileName = fileName;
  m_outgoingSize = size;
  m_outgoingTransferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_transferPanel->renameRow(m_outgoingPath, m_outgoingTransferId);
  m_transferPanel->setProgress(m_outgoingTransferId, 0, m_outgoingSize);
  m_transferPanel->setState(m_outgoingTransferId, TransferPanel::State::WaitingForPeer);
  QJsonObject offer{{QStringLiteral("action"), QStringLiteral("sendOffer")},
                    {QStringLiteral("id"), m_outgoingTransferId},
                    {QStringLiteral("peer"), m_outgoingPeer},
                    {QStringLiteral("name"), m_outgoingFileName},
                    {QStringLiteral("size"), QString::number(m_outgoingSize)},
                    {QStringLiteral("sha256"), QString::fromLatin1(sha256.toHex())}};
  m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(offer));
}

void MainWindow::sendOutgoingChunk(const QByteArray &chunk, quint64 position)
{
  if (m_fileWorker == nullptr || m_outgoingTransferId.isEmpty() || chunk.isEmpty()) {
    return;
  }
  QJsonObject message{{QStringLiteral("action"), QStringLiteral("sendChunk")},
                      {QStringLiteral("id"), m_outgoingTransferId},
                      {QStringLiteral("peer"), m_outgoingPeer},
                      {QStringLiteral("data"), QString::fromLatin1(chunk.toBase64(
                           QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))}};
  m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(message));
  m_transferPanel->setProgress(m_outgoingTransferId, position, m_outgoingSize);
}

void MainWindow::handleFileTransferMessage(const QString &encodedMessage)
{
  QJsonObject message;
  if (!FileTransferProtocol::decodeMessage(encodedMessage, message)) {
    return;
  }
  const QString action = message.value(QStringLiteral("action")).toString();
  const QString peer = message.value(QStringLiteral("peer")).toString();
  const QString id = message.value(QStringLiteral("id")).toString();
  if (action == QStringLiteral("availability")) {
    const bool available = message.value(QStringLiteral("available")).toBool();
    handleLogLine(QStringLiteral("File-transfer availability received: %1.")
                      .arg(available ? QStringLiteral("available") : QStringLiteral("unavailable")));
    m_fileTransferPeers.insert(peer, available);
    if (!available && peer == m_outgoingPeer &&
        (m_fileWorkerThread != nullptr || m_fileWorker != nullptr || !m_outgoingTransferId.isEmpty())) {
      clearFileTransfer(tr("File transfer stopped because the peer disconnected."));
    }
    const bool anyAvailable = std::any_of(m_fileTransferPeers.cbegin(), m_fileTransferPeers.cend(),
                                          [](bool available) { return available; });
    m_actionSendFile->setEnabled(anyAvailable);
    return;
  }
  if (action == QStringLiteral("offer")) {
    if (m_closeAfterFileWorkerStops || m_fileWorkerThread != nullptr || m_fileWorker != nullptr ||
        !m_outgoingTransferId.isEmpty()) {
      QJsonObject reject{{QStringLiteral("action"), QStringLiteral("reject")},
                         {QStringLiteral("peer"), peer},
                         {QStringLiteral("id"), id}};
      m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(reject));
      return;
    }
    const QString name = message.value(QStringLiteral("name")).toString();
    const quint64 size = message.value(QStringLiteral("size")).toString().toULongLong();
    m_outgoingPeer = peer;
    m_outgoingTransferId = id;
    m_outgoingFileName = name;
    m_outgoingSize = size;
    m_pendingOfferHash = QByteArray::fromHex(message.value(QStringLiteral("sha256")).toString().toLatin1());
    m_receivingFile = true;
    m_offerPending = true;
    // ponytail: trust is keyed by screen name; key by certificate fingerprint if names can collide across trusted peers.
    const QStringList trustedPeers = Settings::value(Settings::Gui::FileAutoAcceptPeers).toStringList();
    m_transferAutoAccepted = Settings::value(Settings::Gui::FileAutoAccept).toBool() || trustedPeers.contains(peer);
    if (m_transferAutoAccepted) {
      m_transferPanel->addRow(id, name, peer, size, true, TransferPanel::State::Transferring);
      statusBar()->showMessage(tr("Receiving \"%1\" from %2…").arg(name, peer), 8000);
      acceptPendingOffer(defaultReceivePath(name));
    } else {
      m_transferPanel->addRow(id, name, peer, size, true, TransferPanel::State::Offer);
      notifyTransfer(kAppName, tr("%1 wants to send %2").arg(peer, name));
    }
    return;
  }
  if (action == QStringLiteral("accepted") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    m_transferPanel->setState(id, TransferPanel::State::Transferring);
    if (m_outgoingSize == 0) {
      QJsonObject finish{{QStringLiteral("action"), QStringLiteral("sendFinish")},
                         {QStringLiteral("peer"), peer},
                         {QStringLiteral("id"), id}};
      m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(finish));
    } else {
      if (m_fileWorker != nullptr) {
        QMetaObject::invokeMethod(m_fileWorker, "readNextChunk", Qt::QueuedConnection);
      }
    }
    return;
  }
  if (action == QStringLiteral("sendNextChunk") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    if (m_fileWorker != nullptr) {
      QMetaObject::invokeMethod(m_fileWorker, "readNextChunk", Qt::QueuedConnection);
    }
    return;
  }
  if (action == QStringLiteral("sendReadyToFinish") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    QJsonObject finish{{QStringLiteral("action"), QStringLiteral("sendFinish")},
                       {QStringLiteral("peer"), peer},
                       {QStringLiteral("id"), id}};
    m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(finish));
    return;
  }
  if (action == QStringLiteral("receiveChunk") && id == m_outgoingTransferId && peer == m_outgoingPeer &&
      m_receivingFile && m_fileWorker != nullptr) {
    const QByteArray chunk = QByteArray::fromBase64(
        message.value(QStringLiteral("data")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (chunk.isEmpty()) {
      QJsonObject cancel{{QStringLiteral("action"), QStringLiteral("cancel")},
                         {QStringLiteral("peer"), peer},
                         {QStringLiteral("id"), id}};
      m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(cancel));
      clearFileTransfer(tr("Received an invalid file chunk."));
      return;
    }
    auto *worker = m_fileWorker;
    QMetaObject::invokeMethod(worker, [worker, chunk] { worker->appendReceivedChunk(chunk); }, Qt::QueuedConnection);
    return;
  }
  if (action == QStringLiteral("finishReceive") && id == m_outgoingTransferId && peer == m_outgoingPeer &&
      m_receivingFile && m_fileWorker != nullptr) {
    auto *worker = m_fileWorker;
    QMetaObject::invokeMethod(worker, [worker] { worker->finishReceive(); }, Qt::QueuedConnection);
    return;
  }
  if (action == QStringLiteral("completed") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    const QString name = m_outgoingFileName;
    m_continueSendQueue = !m_sendQueue.isEmpty();
    const int queued = m_sendQueue.size();
    m_transferPanel->setState(id, TransferPanel::State::Done);
    clearFileTransfer();
    notifyTransfer(kAppName, tr("Sent %1").arg(name));
    if (queued > 0) {
      statusBar()->showMessage(tr("Sent \"%1\". %2 more queued.").arg(name).arg(queued), 8000);
    } else {
      statusBar()->showMessage(tr("Sent and verified: %1").arg(name), 8000);
    }
  } else if (action == QStringLiteral("rejected") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    clearFileTransfer(tr("The receiving peer declined the file."));
  } else if (action == QStringLiteral("failed") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    clearFileTransfer(tr("The receiving peer could not complete the transfer."));
  } else if (action == QStringLiteral("cancelled") && id == m_outgoingTransferId && peer == m_outgoingPeer) {
    clearFileTransfer(tr("The receiving peer cancelled the transfer."));
  }
}

bool MainWindow::isPendingOffer(const QString &rowId) const
{
  return m_offerPending && m_receivingFile && rowId == m_outgoingTransferId;
}

QString MainWindow::defaultReceivePath(const QString &name) const
{
  const QString saveDirectory = Settings::value(Settings::Gui::FileReceiveDirectory).toString();
  QDir().mkpath(saveDirectory);
  return FileTransferReceiver::uniqueFilePath(QDir(saveDirectory).filePath(name));
}

void MainWindow::acceptPendingOffer(const QString &destination)
{
  m_offerPending = false;
  const QString id = m_outgoingTransferId;
  if (destination.isEmpty()) {
    const QJsonObject reject{{QStringLiteral("action"), QStringLiteral("reject")},
                             {QStringLiteral("peer"), m_outgoingPeer},
                             {QStringLiteral("id"), id}};
    m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(reject));
    clearFileTransfer(tr("Declined"));
    return;
  }
  m_transferPanel->setState(id, TransferPanel::State::Transferring);
  startFileWorker();
  auto *worker = m_fileWorker;
  const quint64 size = m_outgoingSize;
  const QByteArray hash = m_pendingOfferHash;
  QMetaObject::invokeMethod(worker, [worker, destination, size, hash] {
    const QFileInfo target(destination);
    worker->beginReceive(target.absolutePath(), target.fileName(), size, hash);
  }, Qt::QueuedConnection);
}

void MainWindow::alwaysAcceptPendingOffer(const QString &rowId)
{
  if (!isPendingOffer(rowId)) {
    return;
  }
  const QString peer = m_outgoingPeer;
  const auto answer = QMessageBox::warning(
      this, kAppName,
      tr("Accept files from %1 automatically?\n\n"
         "%1 can then save files into %2 without asking. "
         "Existing files are never replaced.")
          .arg(peer, Settings::value(Settings::Gui::FileReceiveDirectory).toString()),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer != QMessageBox::Yes || !isPendingOffer(rowId)) {
    return;
  }
  QStringList trustedPeers = Settings::value(Settings::Gui::FileAutoAcceptPeers).toStringList();
  if (!trustedPeers.contains(peer)) {
    trustedPeers.append(peer);
    Settings::setValue(Settings::Gui::FileAutoAcceptPeers, trustedPeers);
  }
  m_actionForgetTrusted->setEnabled(true);
  acceptPendingOffer(defaultReceivePath(m_outgoingFileName));
}

void MainWindow::cancelTransfer(const QString &rowId)
{
  const bool active = m_fileWorker != nullptr && (rowId == m_outgoingTransferId || rowId == m_outgoingPath);
  if (!active) {
    if (m_sendQueue.removeAll(rowId) > 0) {
      m_transferPanel->removeRow(rowId);
    }
    return;
  }
  if (!m_outgoingTransferId.isEmpty()) {
    const QJsonObject cancel{{QStringLiteral("action"), QStringLiteral("cancel")},
                             {QStringLiteral("id"), m_outgoingTransferId},
                             {QStringLiteral("peer"), m_outgoingPeer}};
    m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(cancel));
  }
  m_transferPanel->setState(rowId, TransferPanel::State::Failed, tr("Canceled"));
  clearFileTransfer(tr("File transfer canceled."));
}

void MainWindow::failQueuedRows(const QString &reason)
{
  for (const auto &path : m_sendQueue) {
    m_transferPanel->setState(path, TransferPanel::State::Failed, reason);
  }
}

void MainWindow::showInFolder(const QString &path)
{
#if defined(Q_OS_MACOS)
  QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WIN)
  QProcess::startDetached(QStringLiteral("explorer"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
  QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void MainWindow::receiveWorkerReady(OutgoingFileWorker *worker, bool success, const QString &error)
{
  if (m_fileWorker != worker || !m_receivingFile || m_outgoingTransferId.isEmpty()) {
    return;
  }
  if (!success) {
    const QJsonObject reject{{QStringLiteral("action"), QStringLiteral("reject")},
                             {QStringLiteral("peer"), m_outgoingPeer},
                             {QStringLiteral("id"), m_outgoingTransferId}};
    m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(reject));
    clearFileTransfer(error);
    return;
  }
  const QJsonObject accept{{QStringLiteral("action"), QStringLiteral("accept")},
                           {QStringLiteral("peer"), m_outgoingPeer},
                           {QStringLiteral("id"), m_outgoingTransferId}};
  m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(accept));
}

void MainWindow::receiveWorkerChunkSaved(OutgoingFileWorker *worker, bool success, quint64 bytes,
                                         quint64 received, quint64 total, const QString &error)
{
  if (m_fileWorker != worker || !m_receivingFile || m_outgoingTransferId.isEmpty()) {
    return;
  }
  if (!success) {
    const QJsonObject cancel{{QStringLiteral("action"), QStringLiteral("cancel")},
                             {QStringLiteral("peer"), m_outgoingPeer},
                             {QStringLiteral("id"), m_outgoingTransferId}};
    m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(cancel));
    clearFileTransfer(error.isEmpty() ? tr("Could not save the received file.") : error);
    return;
  }
  m_transferPanel->setProgress(m_outgoingTransferId, received, total);
  const QJsonObject saved{{QStringLiteral("action"), QStringLiteral("chunkSaved")},
                          {QStringLiteral("peer"), m_outgoingPeer},
                          {QStringLiteral("id"), m_outgoingTransferId},
                          {QStringLiteral("bytes"), QString::number(bytes)}};
  m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(saved));
}

void MainWindow::receiveWorkerFinished(OutgoingFileWorker *worker, bool success, const QString &path,
                                       const QString &error)
{
  if (m_fileWorker != worker || !m_receivingFile || m_outgoingTransferId.isEmpty()) {
    return;
  }
  const QJsonObject result{{QStringLiteral("action"), QStringLiteral("receiveResult")},
                           {QStringLiteral("peer"), m_outgoingPeer},
                           {QStringLiteral("id"), m_outgoingTransferId},
                           {QStringLiteral("success"), success}};
  m_coreProcess.sendFileTransferMessage(FileTransferProtocol::encodeMessage(result));
  if (success) {
    m_transferPanel->setState(m_outgoingTransferId, TransferPanel::State::Done, path);
    clearFileTransfer();
    notifyTransfer(kAppName, tr("Received %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(tr("Received and verified: %1").arg(path), 8000);
  } else {
    clearFileTransfer(error);
  }
}

void MainWindow::clearFileTransfer(const QString &message)
{
  const QString reason = message.isEmpty() ? tr("Stopped") : message;
  const QString rowId = m_outgoingTransferId.isEmpty() ? m_outgoingPath : m_outgoingTransferId;
  if (!rowId.isEmpty()) {
    m_transferPanel->setState(rowId, TransferPanel::State::Failed, reason);
  }
  if (m_fileWorker != nullptr) {
    m_fileWorker->requestCancel();
    m_fileWorker = nullptr;
  }
  if (m_fileWorkerThread != nullptr) {
    m_fileWorkerThread->quit();
  }
  m_receivingFile = false;
  m_offerPending = false;
  m_transferAutoAccepted = false;
  if (!m_continueSendQueue) {
    failQueuedRows(reason);
    m_sendQueue.clear();
  }
  m_continueSendQueue = false;
  m_outgoingTransferId.clear();
  m_outgoingPeer.clear();
  m_outgoingFileName.clear();
  m_outgoingSize = 0;
  m_outgoingPath.clear();
  m_pendingOfferHash.clear();
  if (!message.isEmpty()) {
    statusBar()->showMessage(message, 8000);
    notifyTransfer(kAppName, message);
  }
}

void MainWindow::notifyTransfer(const QString &title, const QString &text)
{
  if (!QSystemTrayIcon::supportsMessages() || (isVisible() && !isMinimized() && isActiveWindow())) {
    return;
  }
  m_trayIcon->showMessage(title, text, QSystemTrayIcon::Information, 8000);
}

bool MainWindow::canRunCore() const
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;
  return ((isServer || isClient) && (isClient && !ui->lineHostname->text().isEmpty()) || isServer);
}
