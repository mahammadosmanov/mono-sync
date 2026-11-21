# File transfer UX and speed plan

Branch: `feat/file-transfer-ux`. Five phases, done in order, one commit per phase. Nothing is pushed.

## Rules for every phase

- Read every file you change in full before editing it. Read `src/lib/gui/MainWindow.cpp` lines 1649-2154 (the whole file-transfer section) before any GUI phase.
- Build and test with one command (builds, then runs all unit tests in an isolated profile):

  ```bash
  /private/tmp/claude-501/-Users-rohtolos--claude-spares-spare-f7G3pK/5e978799-0ff6-4fa6-8cac-d809da316814/scratchpad/check.sh
  ```

  Add `-R TestName` to run one test. `OSXKeyStateTests` is flaky and unrelated; rerun once if it is the only failure. Baseline is 37/37 passing.
- A phase is done only when the build succeeds, all tests pass, and the new test for that phase exists and passes. If you add or remove a `.cpp`/`.h`, add it to the matching `CMakeLists.txt` (`src/lib/gui/CMakeLists.txt`, `src/unittests/gui/CMakeLists.txt`, `src/unittests/deskflow/CMakeLists.txt`) by copying the pattern of a neighbouring entry.
- Do not change: product version numbers, `.github/`, `translations/*.ts`, `docs/UPSTREAM.md`, license headers of existing files, anything under `.rps/`. New files get the same 4-line SPDX header as `src/lib/gui/OutgoingFileWorker.h`.
- Never weaken these invariants: file transfer only with an authenticated TLS peer; receiver consent before any byte is written (explicit accept, global auto-accept, or per-peer trust); SHA-256 and size verified before the file is published; existing files never overwritten; `FileTransferReceiver::isSafeFileName` applied to every incoming name; all bounded reads stay bounded.
- All user-visible strings go through `tr()`. Plain wording, sentence case, no emoji, no decorative labels.
- Use the system palette (`palette()` roles) for all colours so light and dark mode both work. No hard-coded colours, no gradients, no custom fonts.
- Commit at the end of the phase with a conventional message (`feat(gui): ...`, `feat(protocol): ...`) ending with the line `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`. Do not push. Do not switch branches.
- If something in this plan does not match the code, trust the code, make the smallest change that meets the phase goal, and say so in your final report.

## Current flow (for orientation)

- GUI (`MainWindow`) talks to the core process over local IPC with base64-JSON messages (`FileTransferProtocol::encodeMessage`). The core (`FileTransferProtocol`) talks to the peer with `MSOF/MSRP/MSCH/MSAK/MSFN/MSOK/MSER/MSCA` wire messages.
- Send: `startSending` -> worker `prepare` (full SHA-256 pass) -> `sendOffer` -> peer `accepted` -> loop of `readNextChunk` / `sendChunk` / wait for `sendNextChunk` -> `sendFinish` -> `completed`.
- Receive: `offer` -> modal `QMessageBox` -> worker `beginReceive` -> `accept` -> loop of `receiveChunk` / `appendReceivedChunk` / `chunkSaved` -> `finishReceive` -> `receiveResult`.
- One transfer at a time. `m_fileTransferProgress` is a modal `QProgressDialog`. Results are modal `QMessageBox`es.

---

## Phase 1: inline transfers panel (GUI only, no protocol change)

Goal: no modal dialog anywhere in the transfer flow. Progress, offers and results appear as rows in a panel inside the main window.

### New widget `src/lib/gui/widgets/TransferPanel.{h,cpp}`

`class TransferPanel : public QWidget`. Hidden while it has no rows. Rows are stacked vertically, newest at the bottom, at most 6 rows kept (drop the oldest finished row first).

Each row shows: file-type icon (`QFileIconProvider().icon(QFileInfo(name))`, 32 px), file name (elided in the middle), one status line, a thin progress bar (6 px high, text hidden), and buttons on the right.

Public API (keyed by a row id string; the transfer id is used once known, and the local file path is used for queued files):

```cpp
enum class State { Queued, Preparing, WaitingForPeer, Offer, Transferring, Done, Failed };

void addRow(const QString &rowId, const QString &fileName, const QString &peer, quint64 size, bool incoming, State state);
void renameRow(const QString &oldRowId, const QString &newRowId); // queued path -> transfer id
void setState(const QString &rowId, State state, const QString &detail = QString()); // detail: error text or saved path
void setProgress(const QString &rowId, quint64 done, quint64 total);
void removeRow(const QString &rowId);
bool hasRow(const QString &rowId) const;

// Pure, static, unit-tested:
static QString statusText(State state, bool incoming, const QString &peer, quint64 done, quint64 total,
                          double bytesPerSecond, const QString &detail);

Q_SIGNALS:
void cancelRequested(const QString &rowId);
void acceptRequested(const QString &rowId);
void acceptAlwaysRequested(const QString &rowId); // used in phase 3; emit nothing until then
void chooseLocationRequested(const QString &rowId);
void declineRequested(const QString &rowId);
void showInFolderRequested(const QString &path);
```

Buttons per state:

| State | Buttons |
|---|---|
| Queued, Preparing, WaitingForPeer, Transferring | Cancel |
| Offer | Save (default), Choose location…, Decline |
| Done, incoming | Show in folder, Dismiss (x) |
| Done, outgoing / Failed | Dismiss (x) |

`statusText` examples (use `QLocale().formattedDataSize`): `Queued for Office-PC`, `Preparing…`, `Waiting for Office-PC to accept…`, `Office-PC wants to send this · 12.4 MB`, `3.1 MB of 12.4 MB · 8.2 MB/s · 2 s left`, `Sent to Office-PC · 12.4 MB`, `Saved to /Users/x/Downloads/a.pdf`, `Failed: <detail>`. Speed is measured inside the panel with a `QElapsedTimer` per row started at the first `setProgress`; show speed and time left only after 1 second has elapsed and `bytesPerSecond > 0`.

`setProgress` during `Preparing` shows hashing progress in the bar. Outgoing `Done` rows remove themselves after 8 seconds (`QTimer::singleShot`); incoming `Done` and all `Failed` rows stay until dismissed.

Accessibility: every button has text or an accessible name and tooltip; rows are reachable by Tab.

### MainWindow changes

1. Add `TransferPanel *m_transferPanel` and insert it in the constructor at the bottom of the main content: `ui->topLevelWidget->layout()->addWidget(m_transferPanel)`.
2. Delete `m_fileTransferProgress` and every use of it. Replace each use with the matching `m_transferPanel` call.
3. Sending: in `startSending` add a row (`rowId` = path, state `Preparing`); `hashingProgress` -> `setProgress`; in `prepareOutgoingFile` call `renameRow(path, transferId)` and `setState(WaitingForPeer)`; `accepted` -> `Transferring`; `sendOutgoingChunk` -> `setProgress(position, size)`; `completed` -> `Done`; `rejected` -> `Failed` with "The receiving peer declined the file."; `failed` -> `Failed`; `cancelled` -> `Failed` with "The receiving peer cancelled the transfer.". Remove the three `QMessageBox` calls in that block.
4. Queue: `queueFilesToSend` adds a `Queued` row for every path that is waiting; `sendNextQueuedFile` reuses the queued row (do not add a second one). Cancelling a queued row removes that path from `m_sendQueue`. When the queue is cleared because of failure or disconnect, mark its rows `Failed` with the reason.
5. Receiving: replace the modal prompt. On `offer` store the offer in new members (`m_pendingOfferName`, `m_pendingOfferSize`, `m_pendingOfferHash`) and add an `Offer` row, then return. Move the code that currently runs after the prompt into a new `void acceptPendingOffer(const QString &destination)`; an empty destination means decline (send `reject`, mark the row `Failed` "Declined", then `clearFileTransfer()`). Wire the panel signals:
   - `acceptRequested` -> `acceptPendingOffer(defaultPath)` where `defaultPath` is computed exactly as today.
   - `chooseLocationRequested` -> `QFileDialog::getSaveFileName(...)` as today, then `acceptPendingOffer(result)`; a cancelled dialog leaves the offer row untouched (not a decline).
   - `declineRequested` -> `acceptPendingOffer(QString())`.
   - Auto-accept (`Settings::Gui::FileAutoAccept`) calls `acceptPendingOffer(defaultPath)` immediately with no `Offer` row; the row starts in `Transferring`.
   - Every handler must first check that the row id still equals `m_outgoingTransferId` and `m_receivingFile` is true; otherwise ignore it.
6. `receiveWorkerChunkSaved` -> `setProgress`; `receiveWorkerFinished` -> `Done` with the saved path, or `Failed` with the error. Remove its `QMessageBox` calls.
7. `cancelRequested` for the active transfer does what the old `QProgressDialog::canceled` lambdas did (send `cancel`, `clearFileTransfer(tr("File transfer canceled."))`) and marks the row `Failed` "Canceled".
8. `clearFileTransfer(message)`: if a row for the current transfer is still in a non-final state, set it to `Failed` with `message` (or "Stopped" when empty). Keep the status-bar message.
9. `showInFolderRequested(path)`: macOS `QProcess::startDetached("open", {"-R", path})`; Windows `QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(path)})`; otherwise `QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()))`.
10. Drop overlay: a child `QLabel` of `centralWidget()` covering it (`setGeometry(centralWidget()->rect())`, re-applied in `resizeEvent`), centred text `Drop to send to <peer>` (or `Drop to send` with several peers), background `palette().highlight()` at ~20% alpha with a 2 px dashed `palette().highlight()` border, `Qt::WA_TransparentForMouseEvents`. Show in `dragEnterEvent` when accepted; hide in a new `dragLeaveEvent` override and in `dropEvent`.
11. The remaining `QMessageBox` uses that stay: the auto-accept confirmation in `setAutoAcceptFiles`, and `pickSendPeer`'s dialogs.

### Test

`src/unittests/gui/TransferPanelTests.cpp` (QtTest, copy the CMake entry style of `OutgoingFileWorkerTests`): `statusText` for every state; add/rename/remove rows; panel hidden when empty and visible with a row; `acceptRequested` and `cancelRequested` emitted with the right row id when the buttons are clicked (`findChild<QPushButton *>` by object name).

---

## Phase 2: tray notifications and tray send

1. Add `m_actionSendFile` to the tray menu in `setupTrayIcon()` (before `m_actionMinimize`, with a separator).
2. New helper `void notifyTransfer(const QString &title, const QString &text)`: if the window is hidden, minimized or not the active window, and `QSystemTrayIcon::supportsMessages()`, call `m_trayIcon->showMessage(title, text, QSystemTrayIcon::Information, 8000)`. Otherwise do nothing (the panel already shows it).
3. Call it for: incoming offer waiting for a decision (`"<peer> wants to send <name>"`), file received (`"Received <name>"`), queue or single send finished (`"Sent <name>"`), and any failure.
4. Connect `QSystemTrayIcon::messageClicked` to the existing `showAndActivate`.
5. When an offer arrives while the window is hidden and auto-accept is off, do not show the window automatically; the notification and the tray menu are the way in.

Test: none needed beyond the build and existing tests (pure glue, no branching logic worth a unit test).

---

## Phase 3: per-peer trust ("Always accept from this computer")

No wire change. This removes the one-prompt-per-file problem for a trusted pair.

1. New setting `Settings::Gui::FileAutoAcceptPeers = "gui/fileAutoAcceptPeers"` (a `QStringList`). Register it everywhere `FileAutoAccept` is registered in `src/lib/common/Settings.h` and `Settings.cpp` (key list near line 317; default value: empty list). Run `SettingsTests`.
2. `Offer` rows get a fourth button `Always accept from <peer>` that emits `acceptAlwaysRequested`. MainWindow handles it by showing the same style of confirmation as `setAutoAcceptFiles` (text: `Accept files from <peer> automatically?` plus the save folder and "Existing files are never replaced."), and on Yes appends the peer to the setting and calls `acceptPendingOffer(defaultPath)`.
3. In the `offer` handler: `m_transferAutoAccepted = global auto-accept || peers list contains peer`.
4. File menu: new action `Forget trusted senders` below `Accept files automatically`, enabled only when the list is non-empty; it clears the list.
5. Peer names are screen names of TLS-authenticated peers. Add a `// ponytail:` comment at the lookup: trust is keyed by screen name; key by certificate fingerprint if screen names can ever collide across trusted peers.

Test: extend `TransferPanelTests` to cover the new button and signal. If `SettingsTests` enumerates keys, extend it for the new key.

---

## Phase 4: speed (protocol 1.10, backward compatible with 1.9 peers)

Two changes, each gated on the negotiated protocol minor version. With a 1.9 peer the wire behaviour must stay byte-for-byte identical, and all existing `FileTransferProtocolTests` must pass unchanged.

### 4.0 Negotiation

- `kProtocolMinorVersion` 9 -> 10 in `src/lib/deskflow/ProtocolTypes.h`.
- `ClientProxyUnknown::initProxy`: add `case 10` that creates the same `ClientProxy1_9` with an extra constructor argument `bool fastFileTransfer = true` (default `false`, so `case 9` is unchanged).
- `Client::setupComputer`: add `case 10` that creates `ServerProxy1_9` with the same extra argument.
- Both proxies pass the flag to `FileTransferProtocol` (new constructor parameter `bool fast = false`, placed last).
- The `availability` notification gains `{"fast": m_fast}` so the GUI knows. `MainWindow` stores it per peer (`QHash<QString, bool> m_fileTransferFastPeers`).
- Update `docs/dev/protocol_reference.md` with a short 1.10 section. Update the log text in `Server.cpp:343` to say "protocol 1.9+".
- `grep -rn "kProtocolMinorVersion\|1_9\|minor" src/unittests` and fix any test that hard-codes minor 9 as the current version.

Why this is safe: a new client talking to an old server downgrades its hello-back to 9 (`Client.cpp:706`); an old client answers a new server with 9 and gets `case 9`.

### 4a Windowed acknowledgements

Today exactly one chunk may be in flight. Generalise to a window `W`: `W = m_fast ? 32 : 1`.

Sender side of `FileTransferProtocol`:
- Replace `m_pendingChunkSize` (for sending) with `std::deque<uint32_t> m_inFlightChunks` and a `bool m_nextRequested`.
- `sendChunk` is accepted when `m_sending`, state is `SenderReadyForChunk` or `AwaitingChunkAck`, `m_inFlightChunks.size() < W`, and the chunk fits in `m_expectedSize - m_sentSize - inFlightBytes`. It clears `m_nextRequested`, pushes the size, writes `MSCH`, sets state `AwaitingChunkAck`, then calls `requestNextIfRoom()`.
- `MSAK` carries the cumulative acknowledged size as today. Pop chunks from the front while the running total is <= the acknowledged value; the acknowledged value must land exactly on a chunk boundary and must be greater than `m_sentSize`, otherwise throw `BadClientException` as today. Then: if everything is acknowledged (`m_sentSize == m_expectedSize`) set `SenderReadyForChunk` and notify `sendReadyToFinish`; else call `requestNextIfRoom()` (state `SenderReadyForChunk` when nothing is in flight, otherwise `AwaitingChunkAck`).
- `requestNextIfRoom()`: if `!m_nextRequested`, the window has room, and `m_sentSize + inFlightBytes < m_expectedSize`, notify `sendNextChunk` once and set `m_nextRequested = true`. With `W == 1` this yields exactly the old sequence of notifications (one `sendNextChunk` per ack, none straight after a send).
- The initial `accepted` notification still triggers the first read in the GUI; set `m_nextRequested = true` when notifying `accepted` so the first `sendChunk` is accounted for.

Receiver side:
- Replace the single pending chunk with `std::deque<uint32_t> m_pendingChunks`.
- `MSCH` is accepted in state `Receiving` or `AwaitingLocalChunkCommit` when `m_pendingChunks.size() < W` and the chunk fits in `m_expectedSize - m_receivedSize - pendingBytes`; otherwise throw as today. Push, set `AwaitingLocalChunkCommit`, notify `receiveChunk`.
- `chunkSaved` must match the front of the deque; pop it, add to `m_receivedSize`, write `MSAK` with the cumulative size, state becomes `Receiving` when the deque is empty.
- `MSFN` is still only valid when `m_receivedSize == m_expectedSize` and nothing is pending.
- `resetTransfer()` clears both deques and `m_nextRequested`.

GUI: no change needed for 4a. `MainWindow` already reads one chunk per `sendNextChunk` and commits one chunk per `receiveChunk`; queued worker invocations keep order. Check that `sendOutgoingChunk`'s progress now reports sent-not-acked bytes, which is fine.

Tests in `FileTransferProtocolTests.cpp` (use the existing stream-mock helpers in that file):
- fast sender: after `accepted`, it accepts 32 `sendChunk` calls without any ack, rejects the 33rd, and emits `sendNextChunk` after each of the first 31.
- fast sender: one cumulative ack covering 3 chunks frees 3 slots; an ack that does not land on a chunk boundary throws.
- fast receiver: 3 `MSCH` in a row are accepted, 3 `chunkSaved` produce 3 `MSAK` with cumulative sizes, and the 33rd unacknowledged `MSCH` throws.
- non-fast (default) protocol: a second `sendChunk` before the ack is rejected and a second `MSCH` before `chunkSaved` throws (proves 1.9 behaviour is unchanged).

### 4b Hash while streaming

Removes the full-file hashing pass before the offer when the peer is fast.

- Wire: new message `kMsgFileFinishHash = "MSFH%s%s"` (transfer id, 32 raw SHA-256 bytes), declared next to the other `kMsgFile*` constants and added to the `isFileMessage` check. A fast sender puts 32 zero bytes in the `MSOF` hash field and ends with `MSFH` instead of `MSFN`. A fast receiver accepts `MSFH` exactly where `MSFN` is accepted, validates the hash length is 32, and includes `"sha256"` (hex) in the `finishReceive` notification. A fast receiver that gets a plain `MSFN` after an all-zero offer hash must fail the transfer (notify `finishReceive` is not allowed; send `MSER`, notify `failed`, reset). A non-fast protocol instance treats `MSFH` as an unexpected message (throw).
- Local `sendFinish` gains an optional `"sha256"` field. When `m_fast`, it is required (32 bytes after hex decoding) and `MSFH` is written; when not fast it is ignored and `MSFN` is written.
- `FileTransferReceiver`: add `void setExpectedSha256(const QByteArray &sha256)`, callable any time before `finish()`. `finish()` must fail if the expected hash is empty or all zeros.
- `OutgoingFileWorker`:
  - `prepare(path, bool streamingHash)`: when `streamingHash` is true, skip the hashing loop, validate and open the file as today, and emit `prepared(name, size, 32 zero bytes)`.
  - `readNextChunk` adds each chunk it emits to a member `QCryptographicHash`. New slot `finishSend()` emits new signal `sendHashReady(QByteArray sha256)` after checking that the bytes hashed equal `m_size`; otherwise `failed(...)`.
  - `finishReceive(const QByteArray &sha256 = {})`: when non-empty, call `setExpectedSha256` before `finish`.
- `MainWindow`: `startSending` passes `m_fileTransferFastPeers.value(peer)` to `prepare`. On `sendReadyToFinish` for a fast peer, invoke `finishSend()` on the worker and send `sendFinish` with the hash when `sendHashReady` arrives; for a non-fast peer keep today's behaviour. On `finishReceive`, pass the decoded `sha256` field (if present) to `worker->finishReceive(hash)`. The progress mapping no longer needs the 40/60 split when streaming.

Tests:
- `FileTransferProtocolTests`: fast sender writes `MSFH` with the given hash; fast receiver surfaces the hash in `finishReceive`; fast receiver fails on `MSFN` after an all-zero offer; non-fast instance throws on `MSFH`.
- `FileTransferReceiverTests`: `finish()` fails with an all-zero expected hash; succeeds after `setExpectedSha256` with the right hash; fails with the wrong one and leaves no destination file.
- `OutgoingFileWorkerTests`: streaming round trip (sender in streaming mode, receiver given the hash at finish) produces an identical file.

Commit 4.0+4a and 4b as two commits if 4a is green on its own.

---

## Phase 5: send without opening the window

### 5a Second-instance hand-off (all platforms; this is what Windows "Send to" uses)

- `src/apps/deskflow-gui/deskflow-gui.cpp`: positional arguments are file paths. When another instance is running (the `!sharedMemory.create(1)` branch), open the local socket `ReadWrite`, write each absolute path that is an existing regular file as a UTF-8 line, flush, `waitForBytesWritten(1000)`, then disconnect. With no paths the behaviour is unchanged.
- `MainWindow`: on `m_guiDupeChecker` `newConnection`, take the socket, keep calling `showAndActivate` only when no paths arrive, and read lines for at most 2 seconds and at most 64 KiB total and 100 paths. For each line: must be an absolute path, `QFileInfo(path).isFile()`; collect and call `queueFilesToSend(paths)`. Ignore everything else. This socket is a trust boundary: never execute, open or display the content beyond treating it as a candidate path to send, and the normal peer picker and peer-ready checks still apply.
- When the first instance itself is started with paths, queue them once a peer becomes available is out of scope; show the status-bar message "Connect to a peer, then send the file again." instead.

### 5b macOS Dock drop

- In the plist template used for the GUI bundle (`src/apps/res/deskflow.plist.in`, check `src/apps/deskflow-gui/CMakeLists.txt` to confirm which template generates `MonoSync.plist`), add `CFBundleDocumentTypes` with one entry: `CFBundleTypeName` "File", `CFBundleTypeRole` "Viewer", `LSHandlerRank` "None", `LSItemContentTypes` [`public.data`]. `LSHandlerRank` "None" keeps Mono Sync from becoming a default opener.
- `MainWindow`: `qApp->installEventFilter(this)` and handle `QEvent::FileOpen` (`QFileOpenEvent::file()`) by calling `queueFilesToSend({path})` after the same `isFile` check. Batch several events arriving within 200 ms into one call with a single-shot `QTimer`.

### 5c Windows "Send to" shortcut

- `deploy/windows/standard-user-setup.nsi`: create `$SENDTO\Mono Sync.lnk` pointing at the installed GUI executable at install time and delete it at uninstall. Follow exactly how the existing Start Menu shortcut is created, removed and listed in the managed-shortcut list (and in `deploy/windows/package-standard-user-setup.py` if that script generates the list).
- `deploy/windows/smoke-test-setup-lifecycle.ps1`: assert the shortcut exists after install and is gone after uninstall, mirroring the existing shortcut assertions.
- This part cannot be executed on macOS. Run `makensis` only if the script has a self-contained way to compile; otherwise state clearly in the final report that 5c is unverified.

Tests: a `MainWindow`-free unit test is not practical here; instead factor the line parsing into a small static function `QStringList MainWindow::parseHandOffPaths(const QByteArray &data)` (or a free function in a new small header) and test it: accepts absolute existing files, rejects relative paths, directories, missing files, more than 100 entries, and oversized input.

---

## Out of scope

Folders, resume, clipboard copy/paste of files between computers, simultaneous transfers in both directions, product version bump, pushing or opening a PR, translation catalog refresh.

## Still required from a human

A real two-machine check (README.md line 17): send one file each way, cancel one mid-transfer, and confirm a 0.2.4 peer still interoperates with this build.
