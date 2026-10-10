// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <functional>

#include <QDialog>
#include <QImage>
#include <QVector>

#include "bricklink/global.h"

QT_FORWARD_DECLARE_CLASS(QButtonGroup)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QMediaDevices)
QT_FORWARD_DECLARE_CLASS(QNetworkAccessManager)
QT_FORWARD_DECLARE_CLASS(QNetworkReply)
QT_FORWARD_DECLARE_CLASS(QProgressBar)
QT_FORWARD_DECLARE_CLASS(QStackedLayout)
QT_FORWARD_DECLARE_CLASS(QTimer)
QT_FORWARD_DECLARE_CLASS(QToolButton)

namespace Scanner {
class CameraPreviewWidget;
class Capture;
}


class ItemScannerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ItemScannerDialog(QWidget *parent = nullptr);
    ~ItemScannerDialog() override;

    static void checkSystemPermissions(QObject *context, const std::function<void(bool)> &callback);

    void setItemType(const BrickLink::ItemType *itemType);

signals:
    void itemsScanned(const QVector<const BrickLink::Item *> &items);

protected:
    void changeEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    static QByteArray networkCameraId();

    bool isNetworkCameraSelected() const;
    QString networkBaseUrl() const;
    void cameraSelectionChanged();
    void requestNetworkPreview();
    void captureAndScan();
    void captureNetworkAndScan();
    void startNetworkScan(const QImage &image);

    void updateCameraDevices();
    void updateItemTypeFilters();
    void updateStatusText();
    void languageChange();

    QComboBox *m_selectCamera;
    QComboBox *m_selectBackend;
    QButtonGroup *m_selectItemType;
    QLabel *m_labelCamera;
    QLabel *m_labelBackend;
    QLabel *m_labelItemType;
    QLabel *m_labelNetworkUrl;
    QLineEdit *m_networkUrl;
    Scanner::CameraPreviewWidget *m_cameraPreviewWidget;
    QLabel *m_networkPreview;
    QStackedLayout *m_previewStack;
    QLabel *m_status;
    QProgressBar *m_progress;
    QStackedLayout *m_bottomStack;
    QToolButton *m_pinWindow;

    QMediaDevices *m_mediaDevices;
    Scanner::Capture *m_capture;

    QNetworkAccessManager *m_networkManager;
    QTimer *m_networkPreviewTimer;
    QNetworkReply *m_previewReply = nullptr;
    QNetworkReply *m_scanReply = nullptr;
    QImage m_lastNetworkImage;
    uint m_networkScanId = 0;
    QString m_networkError;
};
