/*
 * Mono Sync -- mouse, keyboard, and file sharing
 * SPDX-FileCopyrightText: (C) 2026 Mono Sync contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QWidget>

class QFrame;
class QVBoxLayout;

class TransferPanel : public QWidget
{
  Q_OBJECT

public:
  enum class State
  {
    Queued,
    Preparing,
    WaitingForPeer,
    Offer,
    Transferring,
    Done,
    Failed
  };

  explicit TransferPanel(QWidget *parent = nullptr);
  ~TransferPanel() override;

  /// Adds a row, or resets the row with this id if it already exists.
  void addRow(const QString &rowId, const QString &fileName, const QString &peer, quint64 size, bool incoming,
              State state);
  void renameRow(const QString &oldRowId, const QString &newRowId);
  /// Ignored once a row is Done or Failed.
  void setState(const QString &rowId, State state, const QString &detail = QString());
  void setProgress(const QString &rowId, quint64 done, quint64 total);
  void removeRow(const QString &rowId);
  bool hasRow(const QString &rowId) const;

  static QString statusText(State state, bool incoming, const QString &peer, quint64 done, quint64 total,
                            double bytesPerSecond, const QString &detail);

Q_SIGNALS:
  void cancelRequested(const QString &rowId);
  void acceptRequested(const QString &rowId);
  void acceptAlwaysRequested(const QString &rowId);
  void chooseLocationRequested(const QString &rowId);
  void declineRequested(const QString &rowId);
  void showInFolderRequested(const QString &path);

private:
  struct Row;
  Row *findRow(const QString &rowId) const;
  Row *rowForFrame(const QFrame *frame) const;
  void refreshRow(Row *row);
  void trimRows();

  QList<Row *> m_rows;
  QVBoxLayout *m_layout = nullptr;
};
