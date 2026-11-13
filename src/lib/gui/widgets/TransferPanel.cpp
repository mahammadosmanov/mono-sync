/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TransferPanel.h"

#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxRows = 6;
constexpr int kOutgoingDoneDelayMs = 8000;
constexpr qint64 kSpeedDelayMs = 1000;

// Label that elides its text in the middle to fit its current width.
class ElidedLabel : public QLabel
{
public:
  explicit ElidedLabel(QWidget *parent)
      : QLabel(parent)
  {
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

  void setFullText(const QString &text)
  {
    m_fullText = text;
    setToolTip(text);
    updateElision();
  }

protected:
  void resizeEvent(QResizeEvent *event) override
  {
    QLabel::resizeEvent(event);
    updateElision();
  }

private:
  void updateElision()
  {
    QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideMiddle, width()));
  }

  QString m_fullText;
};

QPushButton *makeButton(QWidget *parent, const char *objectName, const QString &text)
{
  auto *button = new QPushButton(text, parent);
  button->setObjectName(QLatin1String(objectName));
  button->setToolTip(text);
  return button;
}

} // namespace

struct TransferPanel::Row
{
  QString id;
  QString peer;
  QString detail;
  bool incoming = false;
  State state = State::Queued;
  quint64 done = 0;
  quint64 startDone = 0;
  quint64 total = 0;
  QElapsedTimer timer;
  QFrame *frame = nullptr;
  ElidedLabel *name = nullptr;
  QLabel *status = nullptr;
  QProgressBar *bar = nullptr;
  QPushButton *cancel = nullptr;
  QPushButton *save = nullptr;
  QPushButton *choose = nullptr;
  QPushButton *alwaysAccept = nullptr;
  QPushButton *decline = nullptr;
  QPushButton *showFolder = nullptr;
  QToolButton *dismiss = nullptr;
};

TransferPanel::TransferPanel(QWidget *parent)
    : QWidget(parent),
      m_layout(new QVBoxLayout(this))
{
  m_layout->setContentsMargins(0, 0, 0, 0);
  hide();
}

TransferPanel::~TransferPanel()
{
  qDeleteAll(m_rows);
}

void TransferPanel::addRow(const QString &rowId, const QString &fileName, const QString &peer, quint64 size,
                           bool incoming, State state)
{
  Row *row = findRow(rowId);
  if (!row) {
    row = new Row;
    row->id = rowId;
    row->frame = new QFrame(this);
    row->frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QHBoxLayout(row->frame);

    auto *icon = new QLabel(row->frame);
    icon->setPixmap(QFileIconProvider().icon(QFileInfo(fileName)).pixmap(32, 32));
    layout->addWidget(icon);

    auto *texts = new QVBoxLayout;
    row->name = new ElidedLabel(row->frame);
    QFont nameFont = row->name->font();
    nameFont.setBold(true);
    row->name->setFont(nameFont);
    row->name->setFullText(fileName);
    row->status = new QLabel(row->frame);
    row->status->setTextFormat(Qt::PlainText);
    row->bar = new QProgressBar(row->frame);
    row->bar->setTextVisible(false);
    row->bar->setFixedHeight(6);
    texts->addWidget(row->name);
    texts->addWidget(row->status);
    texts->addWidget(row->bar);
    layout->addLayout(texts, 1);

    row->cancel = makeButton(row->frame, "transferCancel", tr("Cancel"));
    row->save = makeButton(row->frame, "transferSave", tr("Save"));
    row->save->setDefault(true);
    row->choose = makeButton(row->frame, "transferChoose", tr("Choose location…"));
    row->alwaysAccept = makeButton(row->frame, "transferAlwaysAccept", QString());
    row->decline = makeButton(row->frame, "transferDecline", tr("Decline"));
    row->showFolder = makeButton(row->frame, "transferShowInFolder", tr("Show in folder"));
    row->dismiss = new QToolButton(row->frame);
    row->dismiss->setObjectName(QStringLiteral("transferDismiss"));
    row->dismiss->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    row->dismiss->setToolTip(tr("Dismiss"));
    row->dismiss->setAccessibleName(tr("Dismiss"));
    row->dismiss->setAutoRaise(true);
    // macOS draws a bezel on tool buttons even with autoRaise
    row->dismiss->setStyleSheet(QStringLiteral("QToolButton { border: none; }"));
    layout->addWidget(row->cancel);
    layout->addWidget(row->save);
    layout->addWidget(row->choose);
    layout->addWidget(row->alwaysAccept);
    layout->addWidget(row->decline);
    layout->addWidget(row->showFolder);
    layout->addWidget(row->dismiss);

    QFrame *frame = row->frame;
    connect(row->cancel, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT cancelRequested(r->id);
      }
    });
    connect(row->save, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT acceptRequested(r->id);
      }
    });
    connect(row->choose, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT chooseLocationRequested(r->id);
      }
    });
    connect(row->alwaysAccept, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT acceptAlwaysRequested(r->id);
      }
    });
    connect(row->decline, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT declineRequested(r->id);
      }
    });
    connect(row->showFolder, &QPushButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        Q_EMIT showInFolderRequested(r->detail);
      }
    });
    connect(row->dismiss, &QToolButton::clicked, this, [this, frame] {
      if (const Row *r = rowForFrame(frame)) {
        removeRow(r->id);
      }
    });

    m_rows.append(row);
    m_layout->addWidget(row->frame);
  }
  row->peer = peer;
  row->alwaysAccept->setText(tr("Always accept from %1").arg(peer));
  row->alwaysAccept->setToolTip(row->alwaysAccept->text());
  row->incoming = incoming;
  row->state = state;
  row->done = 0;
  row->startDone = 0;
  row->total = size;
  row->detail.clear();
  row->timer.invalidate();
  refreshRow(row);
  trimRows();
  setVisible(!m_rows.isEmpty());
}

void TransferPanel::renameRow(const QString &oldRowId, const QString &newRowId)
{
  Row *row = findRow(oldRowId);
  if (row && !findRow(newRowId)) {
    row->id = newRowId;
  }
}

void TransferPanel::setState(const QString &rowId, State state, const QString &detail)
{
  Row *row = findRow(rowId);
  if (!row || row->state == State::Done || row->state == State::Failed) {
    return;
  }
  row->state = state;
  row->detail = detail;
  if (state == State::Transferring) {
    row->timer.invalidate();
  }
  refreshRow(row);
  if (state == State::Done && !row->incoming) {
    const QString id = row->id;
    QTimer::singleShot(kOutgoingDoneDelayMs, this, [this, id] {
      const Row *r = findRow(id);
      if (r && r->state == State::Done) {
        removeRow(id);
      }
    });
  }
}

void TransferPanel::setProgress(const QString &rowId, quint64 done, quint64 total)
{
  Row *row = findRow(rowId);
  if (!row) {
    return;
  }
  row->done = done;
  row->total = total;
  if (row->state == State::Transferring && !row->timer.isValid()) {
    row->timer.start();
    row->startDone = done;
  }
  refreshRow(row);
}

void TransferPanel::removeRow(const QString &rowId)
{
  const QString key = rowId;
  Row *row = findRow(key);
  if (!row) {
    return;
  }
  m_rows.removeOne(row);
  m_layout->removeWidget(row->frame);
  row->frame->hide();
  row->frame->deleteLater();
  delete row;
  setVisible(!m_rows.isEmpty());
}

bool TransferPanel::hasRow(const QString &rowId) const
{
  return findRow(rowId) != nullptr;
}

QString TransferPanel::statusText(State state, bool incoming, const QString &peer, quint64 done, quint64 total,
                                  double bytesPerSecond, const QString &detail)
{
  const auto size = [](quint64 bytes) { return QLocale().formattedDataSize(static_cast<qint64>(bytes)); };
  switch (state) {
  case State::Queued:
    return tr("Queued for %1").arg(peer);
  case State::Preparing:
    return tr("Preparing…");
  case State::WaitingForPeer:
    return tr("Waiting for %1 to accept…").arg(peer);
  case State::Offer:
    return tr("%1 wants to send this · %2").arg(peer, size(total));
  case State::Transferring: {
    QStringList parts{tr("%1 of %2").arg(size(done), size(total))};
    if (bytesPerSecond > 0) {
      parts << tr("%1/s").arg(size(static_cast<quint64>(bytesPerSecond)));
      if (total > done) {
        const auto seconds = static_cast<quint64>(std::ceil(static_cast<double>(total - done) / bytesPerSecond));
        parts << (seconds < 60 ? tr("%1 s left").arg(seconds) : tr("%1 min left").arg((seconds + 59) / 60));
      }
    }
    return parts.join(QStringLiteral(" · "));
  }
  case State::Done:
    return incoming ? tr("Saved to %1").arg(detail) : tr("Sent to %1 · %2").arg(peer, size(total));
  case State::Failed:
    return detail.isEmpty() ? tr("Failed") : tr("Failed: %1").arg(detail);
  }
  return {};
}

TransferPanel::Row *TransferPanel::findRow(const QString &rowId) const
{
  for (Row *row : m_rows) {
    if (row->id == rowId) {
      return row;
    }
  }
  return nullptr;
}

TransferPanel::Row *TransferPanel::rowForFrame(const QFrame *frame) const
{
  for (Row *row : m_rows) {
    if (row->frame == frame) {
      return row;
    }
  }
  return nullptr;
}

void TransferPanel::refreshRow(Row *row)
{
  const State state = row->state;
  const bool active = state == State::Queued || state == State::Preparing || state == State::WaitingForPeer ||
                      state == State::Transferring;
  row->cancel->setVisible(active);
  row->save->setVisible(state == State::Offer);
  row->choose->setVisible(state == State::Offer);
  row->alwaysAccept->setVisible(state == State::Offer);
  row->decline->setVisible(state == State::Offer);
  row->showFolder->setVisible(state == State::Done && row->incoming);
  row->dismiss->setVisible(state == State::Done || state == State::Failed);
  row->bar->setVisible(state == State::Preparing || state == State::Transferring);
  if (row->total == 0) {
    row->bar->setRange(0, 0);
  } else {
    row->bar->setRange(0, 1000);
    row->bar->setValue(static_cast<int>(std::min(row->done, row->total) * 1000 / row->total));
  }

  double bytesPerSecond = 0;
  const qint64 elapsed = row->timer.isValid() ? row->timer.elapsed() : 0;
  if (state == State::Transferring && elapsed >= kSpeedDelayMs && row->done > row->startDone) {
    bytesPerSecond = static_cast<double>(row->done - row->startDone) * 1000.0 / static_cast<double>(elapsed);
  }
  row->status->setText(statusText(state, row->incoming, row->peer, row->done, row->total, bytesPerSecond, row->detail));
}

void TransferPanel::trimRows()
{
  while (m_rows.size() > kMaxRows) {
    const auto finished = std::find_if(m_rows.cbegin(), m_rows.cend(), [](const Row *row) {
      return row->state == State::Done || row->state == State::Failed;
    });
    if (finished == m_rows.cend()) {
      return;
    }
    const QString id = (*finished)->id;
    removeRow(id);
  }
}
