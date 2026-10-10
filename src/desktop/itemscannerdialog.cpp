// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QButtonGroup>
#include <QCameraDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProgressBar>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QQmlApplicationEngine>

#include "bricklink/core.h"
#include "bricklink/item.h"
#include <common/application.h>
#include "common/config.h"
#include "common/eventfilter.h"
#include "scanner/core.h"
#include "scanner/capture.h"
#include "scanner/camerapreviewwidget.h"
#include "itemscannerdialog.h"


QByteArray ItemScannerDialog::networkCameraId()
{
    return QByteArrayLiteral("__brickstore_network_camera__");
}

ItemScannerDialog::ItemScannerDialog(QWidget *parent)
    : QDialog(parent)
    , m_mediaDevices(new QMediaDevices(this))
    , m_capture(new Scanner::Capture(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_networkPreviewTimer(new QTimer(this))
{
    setSizeGripEnabled(true);

    m_selectCamera = new QComboBox(this);
    m_selectCamera->setFocusPolicy(Qt::NoFocus);
    connect(m_mediaDevices, &QMediaDevices::videoInputsChanged,
            this, &ItemScannerDialog::updateCameraDevices);

    m_networkUrl = new QLineEdit(this);
    m_networkUrl->setText(Config::inst()->value(
        u"MainWindow/ItemScannerDialog/NetworkCameraUrl"_qs,
        u"http://raspberrypi.local:8000"_qs).toString());
    m_networkUrl->setPlaceholderText(u"http://192.168.1.123:8000"_qs);
    connect(m_networkUrl, &QLineEdit::editingFinished, this, [this]() {
        m_networkError.clear();
        if (isNetworkCameraSelected())
            requestNetworkPreview();
    });

    m_selectBackend = new QComboBox(this);
    m_selectBackend->setFocusPolicy(Qt::NoFocus);

    m_selectItemType = new QButtonGroup(this);
    auto *itemTypesWidget = new QWidget(this);
    itemTypesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *itemTypesLayout = new QHBoxLayout(itemTypesWidget);
    itemTypesLayout->setContentsMargins(0, 0, 0, 0);
    itemTypesLayout->setSpacing(2);

    auto createTypeButton = [&, this](const BrickLink::ItemType *itt) {
        auto *b = new QToolButton(this);
        b->setFocusPolicy(Qt::NoFocus);
        b->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
        b->setProperty("iconScaling", true);
        b->setCheckable(true);
        b->setChecked(!itt);
        b->setText(itt ? itt->name() : tr("Any"));
        m_selectItemType->addButton(b, itt ? itt->id() : '*');
        itemTypesLayout->addWidget(b, 1);
    };

    createTypeButton(nullptr);
    for (const auto &itt : BrickLink::core()->itemTypes())
        createTypeButton(&itt);

    m_cameraPreviewWidget = new Scanner::CameraPreviewWidget(Application::inst()->qmlEngine(), this);
    setFocusProxy(m_cameraPreviewWidget);
    m_cameraPreviewWidget->setFocus();
    m_capture->setVideoOutput(m_cameraPreviewWidget->videoOutput());
    m_capture->trackWindowVisibility(this);

    m_networkPreview = new QLabel(this);
    m_networkPreview->setAlignment(Qt::AlignCenter);
    m_networkPreview->setMinimumSize(m_cameraPreviewWidget->minimumSize());
    m_networkPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_networkPreview->setText(tr("Waiting for Raspberry Pi camera..."));

    new EventFilter(m_networkPreview, { QEvent::MouseButtonRelease },
                    [this](QObject *, QEvent *) {
        captureAndScan();
        return EventFilter::StopEventProcessing;
    });

    m_previewStack = new QStackedLayout();
    m_previewStack->setContentsMargins(0, 0, 0, 0);
    m_previewStack->addWidget(m_cameraPreviewWidget);
    m_previewStack->addWidget(m_networkPreview);
    m_previewStack->setCurrentWidget(m_cameraPreviewWidget);

    connect(m_cameraPreviewWidget, &Scanner::CameraPreviewWidget::clicked,
            this, &ItemScannerDialog::captureAndScan);
    connect(m_capture, &Scanner::Capture::cameraActiveChanged,
            m_cameraPreviewWidget, &Scanner::CameraPreviewWidget::setActive);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    connect(m_capture, &Scanner::Capture::progressChanged,
            m_progress, &QProgressBar::setValue);

    m_status = new QLabel(this);
    QPalette pal = m_status->palette();
    pal.setColor(QPalette::Text, pal.color(QPalette::Active, QPalette::Text));
    m_status->setPalette(pal);
    m_status->setTextFormat(Qt::RichText);
    m_status->setAlignment(Qt::AlignCenter);

    m_pinWindow = new QToolButton(this);
    m_pinWindow->setFocusPolicy(Qt::NoFocus);
    m_pinWindow->setCheckable(true);
    m_pinWindow->setProperty("iconScaling", true);

    auto updatePinIcon = [this]() {
        bool b = m_pinWindow->isChecked();
        m_pinWindow->setIcon(QIcon::fromTheme(b ? u"window-unpin"_qs : u"window-pin"_qs));
    };
    connect(m_pinWindow, &QToolButton::toggled, this, updatePinIcon);
    updatePinIcon();

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    form->addRow(m_labelCamera = new QLabel(this), m_selectCamera);
    form->addRow(m_labelNetworkUrl = new QLabel(this), m_networkUrl);
    form->addRow(m_labelBackend = new QLabel(this), m_selectBackend);
    form->addRow(m_labelItemType = new QLabel(this), itemTypesWidget);
    layout->addLayout(form);
    layout->addLayout(m_previewStack, 100);
    m_bottomStack = new QStackedLayout();
    m_bottomStack->setContentsMargins(0, 0, 0, 0);
    m_bottomStack->addWidget(m_status);
    m_bottomStack->addWidget(m_progress);
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(m_pinWindow);
    bottomLayout->addLayout(m_bottomStack, 1);
    layout->addLayout(bottomLayout, 0);

    const QByteArrayList backendIds = Scanner::core()->availableBackendIds();
    for (const QByteArray &backendId : backendIds) {
        const auto *backend = Scanner::core()->backendFromId(backendId);
        m_selectBackend->addItem(QIcon(backend->icon()), backend->name, backendId);
    }

    m_selectCamera->setCurrentIndex(-1);
    connect(m_selectCamera, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0)
            cameraSelectionChanged();
    });
    m_selectBackend->setCurrentIndex(-1);
    connect(m_selectBackend, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0)
            m_capture->setCurrentBackendId(m_selectBackend->itemData(index).toByteArray());
    });
    connect(m_selectItemType, &QButtonGroup::idClicked,
            this, [this](int id) {
        m_capture->setCurrentItemTypeFilter(BrickLink::core()->itemType(char(id)));
    });

    m_networkPreviewTimer->setInterval(500);
    connect(m_networkPreviewTimer, &QTimer::timeout,
            this, &ItemScannerDialog::requestNetworkPreview);

    connect(Scanner::core(), &Scanner::Core::scanFinished,
            this, [this](uint scanId, const QVector<Scanner::Core::Result> &itemsAndScores) {
        if (!m_networkScanId || scanId != m_networkScanId)
            return;

        m_networkScanId = 0;
        QVector<const BrickLink::Item *> items;
        items.reserve(itemsAndScores.size());
        for (const auto &result : itemsAndScores)
            items << result.item;

        if (items.isEmpty()) {
            m_networkError = tr("No matching item found - try again.");
            updateStatusText();
            return;
        }

        m_networkError.clear();
        updateStatusText();
        emit itemsScanned(items);
        if (!m_pinWindow->isChecked())
            accept();
    });
    connect(Scanner::core(), &Scanner::Core::scanFailed,
            this, [this](uint scanId, const QString &error) {
        if (!m_networkScanId || scanId != m_networkScanId)
            return;
        m_networkScanId = 0;
        m_networkError = error;
        updateStatusText();
    });

    updateCameraDevices();

    const QByteArray cameraId = Config::inst()->value(
        u"MainWindow/ItemScannerDialog/CameraId"_qs).toByteArray();
    int cameraIndex = m_selectCamera->findData(cameraId);
    if (cameraIndex < 0)
        cameraIndex = 0;
    m_selectCamera->setCurrentIndex(cameraIndex);
    cameraSelectionChanged();

    const QByteArray backendId = Config::inst()->value(
        u"MainWindow/ItemScannerDialog/BackendId"_qs).toByteArray();
    int backendIndex = m_selectBackend->findData(backendId);
    m_selectBackend->setCurrentIndex(backendIndex < 0 ? 0 : backendIndex);

    languageChange();

    restoreGeometry(Config::inst()->value(
        u"MainWindow/ItemScannerDialog/Geometry"_qs).toByteArray());

    // QtMM bug: the camera preview is most likely just black without this
    QMetaObject::invokeMethod(this, [this]() {
          m_cameraPreviewWidget->resize(m_cameraPreviewWidget->size() + QSize(1, 1));
        }, Qt::QueuedConnection);

    m_pinWindow->setChecked(Config::inst()->value(
        u"MainWindow/ItemScannerDialog/Pinned"_qs, false).toBool());

    updateItemTypeFilters();
    connect(m_capture, &Scanner::Capture::supportedItemTypeFiltersChanged,
            this, &ItemScannerDialog::updateItemTypeFilters);
    connect(m_capture, &Scanner::Capture::currentItemTypeFilterChanged,
            this, &ItemScannerDialog::updateItemTypeFilters);

    connect(m_capture, &Scanner::Capture::stateChanged,
            this, [this](Scanner::Capture::State newState) {
        updateStatusText();

        if (newState == Scanner::Capture::State::Scanning)
            m_bottomStack->setCurrentWidget(m_progress);
        else
            m_bottomStack->setCurrentWidget(m_status);
    });

    connect(m_capture, &Scanner::Capture::captureAndScanFinished,
            this, [this](const QVector<const BrickLink::Item *> &items) {
        emit itemsScanned(items);
        if (!m_pinWindow->isChecked())
            accept();
    });
}

ItemScannerDialog::~ItemScannerDialog()
{
    if (m_previewReply)
        m_previewReply->abort();
    if (m_scanReply)
        m_scanReply->abort();

    Config::inst()->setValue(u"MainWindow/ItemScannerDialog/Geometry"_qs, saveGeometry());
    Config::inst()->setValue(u"MainWindow/ItemScannerDialog/CameraId"_qs,
                             m_selectCamera->currentData().toByteArray());
    Config::inst()->setValue(u"MainWindow/ItemScannerDialog/NetworkCameraUrl"_qs,
                             m_networkUrl->text().trimmed());
    Config::inst()->setValue(u"MainWindow/ItemScannerDialog/BackendId"_qs,
                             m_selectBackend->currentData().toByteArray());
    Config::inst()->setValue(u"MainWindow/ItemScannerDialog/Pinned"_qs,
                             m_pinWindow->isChecked());
}

void ItemScannerDialog::checkSystemPermissions(QObject *context,
                                                const std::function<void(bool)> &callback)
{
    if (Config::inst()->value(u"MainWindow/ItemScannerDialog/CameraId"_qs).toByteArray()
            == networkCameraId()) {
        if (callback)
            callback(true);
        return;
    }
    Scanner::Capture::checkSystemPermissions(context, callback);
}

void ItemScannerDialog::setItemType(const BrickLink::ItemType *itemType)
{
    m_capture->setCurrentItemTypeFilter(itemType);
}

bool ItemScannerDialog::isNetworkCameraSelected() const
{
    return m_selectCamera->currentData().toByteArray() == networkCameraId();
}

QString ItemScannerDialog::networkBaseUrl() const
{
    QString url = m_networkUrl->text().trimmed();
    while (url.endsWith(u'/'))
        url.chop(1);
    if (!url.isEmpty() && !url.contains(u"://"_qs))
        url.prepend(u"http://"_qs);
    return url;
}

void ItemScannerDialog::cameraSelectionChanged()
{
    const bool network = isNetworkCameraSelected();
    m_labelNetworkUrl->setVisible(network);
    m_networkUrl->setVisible(network);

    if (network) {
        m_capture->setCurrentCameraId({ });
        m_previewStack->setCurrentWidget(m_networkPreview);
        m_networkError.clear();
        m_networkPreviewTimer->start();
        requestNetworkPreview();
    } else {
        m_networkPreviewTimer->stop();
        if (m_previewReply) {
            m_previewReply->abort();
            m_previewReply = nullptr;
        }
        if (m_scanReply) {
            m_scanReply->abort();
            m_scanReply = nullptr;
        }
        m_networkScanId = 0;
        m_networkError.clear();
        m_previewStack->setCurrentWidget(m_cameraPreviewWidget);
        m_capture->setCurrentCameraId(m_selectCamera->currentData().toByteArray());
    }
    updateStatusText();
}

void ItemScannerDialog::requestNetworkPreview()
{
    if (!isNetworkCameraSelected() || m_previewReply)
        return;

    const QString baseUrl = networkBaseUrl();
    if (baseUrl.isEmpty())
        return;

    QNetworkRequest request { QUrl(baseUrl + u"/snapshot.jpg"_qs) };
    request.setRawHeader(QByteArrayLiteral("Cache-Control"),
                         QByteArrayLiteral("no-cache, no-store"));
    m_previewReply = m_networkManager->get(request);
    QNetworkReply *reply = m_previewReply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_previewReply == reply)
            m_previewReply = nullptr;

        if (reply->error() == QNetworkReply::NoError) {
            QImage image;
            if (image.loadFromData(reply->readAll())) {
                m_lastNetworkImage = image;
                const QSize targetSize = m_networkPreview->size().boundedTo(image.size());
                m_networkPreview->setPixmap(QPixmap::fromImage(image).scaled(
                    targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                if (!m_networkScanId)
                    m_networkError.clear();
            } else if (!m_networkScanId) {
                m_networkError = tr("The network camera returned an invalid image.");
            }
        } else if (reply->error() != QNetworkReply::OperationCanceledError && !m_networkScanId) {
            m_networkError = tr("Network camera error: %1").arg(reply->errorString());
        }

        reply->deleteLater();
        updateStatusText();
    });
}

void ItemScannerDialog::captureAndScan()
{
    if (isNetworkCameraSelected())
        captureNetworkAndScan();
    else
        m_capture->captureAndScan();
}

void ItemScannerDialog::captureNetworkAndScan()
{
    if (m_scanReply || m_networkScanId)
        return;

    const QString baseUrl = networkBaseUrl();
    if (baseUrl.isEmpty()) {
        m_networkError = tr("Enter the Raspberry Pi camera URL first.");
        updateStatusText();
        return;
    }

    m_networkError.clear();
    m_status->setText(tr("Capturing network camera..."));
    m_bottomStack->setCurrentWidget(m_status);

    QNetworkRequest request { QUrl(baseUrl + u"/snapshot.jpg"_qs) };
    request.setRawHeader(QByteArrayLiteral("Cache-Control"),
                         QByteArrayLiteral("no-cache, no-store"));
    m_scanReply = m_networkManager->get(request);
    QNetworkReply *reply = m_scanReply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_scanReply == reply)
            m_scanReply = nullptr;

        if (reply->error() == QNetworkReply::NoError) {
            QImage image;
            if (image.loadFromData(reply->readAll())) {
                m_lastNetworkImage = image;
                reply->deleteLater();
                startNetworkScan(image);
                return;
            }
            m_networkError = tr("The network camera returned an invalid image.");
        } else if (reply->error() != QNetworkReply::OperationCanceledError) {
            m_networkError = tr("Network camera error: %1").arg(reply->errorString());
        }

        reply->deleteLater();
        updateStatusText();
    });
}

void ItemScannerDialog::startNetworkScan(const QImage &image)
{
    if (image.isNull()) {
        m_networkError = tr("There is no network camera image to scan.");
        updateStatusText();
        return;
    }

    m_status->setText(tr("Scanning..."));
    m_bottomStack->setCurrentWidget(m_status);
    m_networkScanId = Scanner::core()->scan(image,
                                             m_capture->currentItemTypeFilter(),
                                             m_capture->currentBackendId());
    if (!m_networkScanId) {
        m_networkError = tr("Scanning failed");
        updateStatusText();
    }
}

void ItemScannerDialog::updateCameraDevices()
{
    const auto allCameraDevices = QMediaDevices::videoInputs();

    const QByteArray oldCameraId = m_selectCamera->currentData().toByteArray();
    int newCurrentIndex = -1;

    {
        // Repopulating emits currentIndexChanged all by itself - adding the first item to an
        // empty combo box makes it the current one - which would switch cameras behind the
        // user's back whenever a device is plugged in or removed.
        const QSignalBlocker blocker(m_selectCamera);
        m_selectCamera->clear();

        const auto camIcon = QIcon::fromTheme(u"camera-photo"_qs);
        for (const auto &cameraDevice : allCameraDevices) {
            m_selectCamera->addItem(camIcon, cameraDevice.description(), cameraDevice.id());
            if (cameraDevice.id() == oldCameraId)
                newCurrentIndex = m_selectCamera->count() - 1;
        }

        m_selectCamera->addItem(camIcon, tr("Raspberry Pi / Network Camera"), networkCameraId());
        if (oldCameraId == networkCameraId())
            newCurrentIndex = m_selectCamera->count() - 1;

        if (newCurrentIndex < 0)
            newCurrentIndex = allCameraDevices.isEmpty() ? m_selectCamera->count() - 1 : 0;
        m_selectCamera->setCurrentIndex(newCurrentIndex);
    }

    cameraSelectionChanged();
    setEnabled(true);
    updateStatusText();
}

void ItemScannerDialog::updateItemTypeFilters()
{
    const auto supportedItemTypes = m_capture->supportedItemTypeFilters();
    const auto *currentItemType = m_capture->currentItemTypeFilter();

    QAbstractButton *first = nullptr;
    bool checkedAny = false;

    const auto buttons = m_selectItemType->buttons();
    for (auto *b : buttons) {
        int id = m_selectItemType->id(b);
        const auto itt = BrickLink::core()->itemType(char(id));
        bool supported = !itt || supportedItemTypes.contains(itt);
        bool checked = supported && (itt == currentItemType);
        b->setVisible(supported);
        b->setChecked(checked);
        checkedAny = checkedAny || checked;
        if (!first && supported)
            first = b;
    }
    if (!checkedAny && first)
        first->setChecked(true);
}

void ItemScannerDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        languageChange();
    QDialog::changeEvent(e);
}

void ItemScannerDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space)
        captureAndScan();
    QDialog::keyPressEvent(e);
}

void ItemScannerDialog::languageChange()
{
    setWindowTitle(tr("Item Scanner"));
    updateStatusText();

    m_labelCamera->setText(tr("Camera"));
    m_labelNetworkUrl->setText(tr("Pi camera URL"));
    m_labelBackend->setText(tr("Service"));
    m_labelItemType->setText(tr("Item type"));

    m_pinWindow->setToolTip(tr("Keep this window open"));
}

void ItemScannerDialog::updateStatusText()
{
    if (isNetworkCameraSelected()) {
        m_bottomStack->setCurrentWidget(m_status);
        if (m_networkScanId) {
            m_status->setText(tr("Scanning..."));
        } else if (m_scanReply) {
            m_status->setText(tr("Capturing network camera..."));
        } else if (!m_networkError.isEmpty()) {
            m_status->setText(u"<b>"_qs + m_networkError.toHtmlEscaped() + u"</b>"_qs);
        } else if (m_lastNetworkImage.isNull()) {
            m_status->setText(tr("Connecting to the Raspberry Pi camera..."));
        } else {
            m_status->setText(tr("Click into the camera preview or press Space to capture an image."));
        }
        return;
    }

    switch (m_capture->state()) {
    case Scanner::Capture::State::Idle:
        if (m_selectCamera->count())
            m_status->setText(tr("Click into the camera preview or press Space to capture an image."));
        else
            m_status->setText(u"<b>" + tr("There is no camera connected to this computer.") + u"</b>");
        break;
    case Scanner::Capture::State::NoMatch:
        m_status->setText(u"<b>" + tr("No matching item found - try again.") + u"</b>");
        break;
    case Scanner::Capture::State::Error:
        m_status->setText(u"<b>"_qs + tr("An error occurred:") + u"</b><br>" + m_capture->lastError());
        break;
    default:
        break;
    }
}


#include "moc_itemscannerdialog.cpp"
