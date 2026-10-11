// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include "additemdialog.h"
#include "mainwindow.h"
#include "selectcolor.h"
#include "selectitem.h"
#include "view.h"
#include "bricklink/color.h"
#include "bricklink/item.h"
#include "bricklink/lot.h"
#include "common/config.h"

namespace {

constexpr auto LabelServiceConfigKey = "MainWindow/AddItemDialog/BrotherQLServiceUrl";
constexpr auto DefaultLabelServiceUrl = "http://127.0.0.1:8013";

QString currencyCode(AddItemDialog *dialog)
{
    if (auto *label = dialog->findChild<QLabel *>(u"w_label_currency"_qs)) {
        static const QRegularExpression re(u"\\(([^)]+)\\)"_qs);
        const auto match = re.match(label->text());
        if (match.hasMatch())
            return match.captured(1).remove(u'&').trimmed();
    }
    return u"CHF"_qs;
}

QString labelServiceUrl()
{
    return Config::inst()->value(QString::fromLatin1(LabelServiceConfigKey),
                                 QString::fromLatin1(DefaultLabelServiceUrl))
        .toString().trimmed();
}

bool configureLabelService(QWidget *parent)
{
    bool ok = false;
    QString value = QInputDialog::getText(
        parent,
        QObject::tr("Brother QL label service"),
        QObject::tr("Service URL:"),
        QLineEdit::Normal,
        labelServiceUrl(),
        &ok);
    if (!ok)
        return false;

    value = value.trimmed();
    while (value.endsWith(u'/'))
        value.chop(1);

    const QUrl url(value);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        QMessageBox::warning(parent,
                             QObject::tr("Brother QL label service"),
                             QObject::tr("Please enter a valid HTTP URL, for example http://127.0.0.1:8013."));
        return false;
    }

    Config::inst()->setValue(QString::fromLatin1(LabelServiceConfigKey), value);
    return true;
}

struct PrintResult
{
    bool ok = false;
    QString error;
};

PrintResult postLabel(const QJsonObject &payload)
{
    QString base = labelServiceUrl();
    while (base.endsWith(u'/'))
        base.chop(1);

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(base + u"/print"_qs));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_qs);

    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(15000);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    if (!timeout.isActive()) {
        reply->abort();
        return { false, QObject::tr("The label service did not respond within 15 seconds.") };
    }
    timeout.stop();

    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
        return { false, QObject::tr("%1\n%2").arg(reply->errorString(), QString::fromUtf8(raw)) };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return { false, QObject::tr("The label service returned an invalid response: %1")
                           .arg(QString::fromUtf8(raw)) };

    const QJsonObject obj = doc.object();
    if (!obj.value(u"ok"_qs).toBool())
        return { false, obj.value(u"error"_qs).toString(QObject::tr("Unknown printing error.")) };

    return { true, { } };
}

void showServiceError(QWidget *parent, const QString &error)
{
    QMessageBox box(QMessageBox::Critical,
                    QObject::tr("Print DK-11204 label"),
                    QObject::tr("Brother QL printing failed.\n\nService: %1\n\n%2")
                        .arg(labelServiceUrl(), error),
                    QMessageBox::NoButton,
                    parent);
    auto *changeButton = box.addButton(QObject::tr("Change service URL..."),
                                       QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == changeButton)
        configureLabelService(parent);
}

QJsonObject lotPayload(const BrickLink::Lot *lot, const QString &currency, int copies)
{
    QJsonObject payload;
    if (!lot)
        return payload;

    payload.insert(u"item_id"_qs, QString::fromLatin1(lot->itemId()));
    payload.insert(u"name"_qs, lot->itemName());
    payload.insert(u"color"_qs,
                   lot->colorName().isEmpty() ? QObject::tr("No color") : lot->colorName());
    payload.insert(u"condition"_qs,
                   lot->condition() == BrickLink::Condition::Used
                       ? QObject::tr("Used") : QObject::tr("New"));
    payload.insert(u"currency"_qs, currency.isEmpty() ? u"CHF"_qs : currency);
    payload.insert(u"price"_qs, lot->price());
    payload.insert(u"copies"_qs, copies);
    return payload;
}

void printLabel(AddItemDialog *dialog)
{
    auto *itemSelector = dialog->findChild<SelectItem *>();
    auto *colorSelector = dialog->findChild<SelectColor *>();
    auto *priceSpin = dialog->findChild<QDoubleSpinBox *>(u"w_price"_qs);
    auto *usedRadio = dialog->findChild<QRadioButton *>(u"w_radio_used"_qs);

    const BrickLink::Item *item = itemSelector ? itemSelector->currentItem() : nullptr;
    const BrickLink::Color *color = colorSelector ? colorSelector->currentColor() : nullptr;

    if (!item || !priceSpin) {
        QMessageBox::warning(dialog,
                             QObject::tr("Print DK-11204 label"),
                             QObject::tr("Select an item first."));
        return;
    }

    bool ok = false;
    const int copies = QInputDialog::getInt(dialog,
                                            QObject::tr("Print DK-11204 label"),
                                            QObject::tr("Number of labels:"),
                                            1, 1, 250, 1, &ok);
    if (!ok)
        return;

    const QString condition = (usedRadio && usedRadio->isChecked())
            ? QObject::tr("Used") : QObject::tr("New");

    QJsonObject payload;
    payload.insert(u"item_id"_qs, QString::fromLatin1(item->id()));
    payload.insert(u"name"_qs, item->name());
    payload.insert(u"color"_qs, color ? color->name() : QObject::tr("No color"));
    payload.insert(u"condition"_qs, condition);
    payload.insert(u"currency"_qs, currencyCode(dialog));
    payload.insert(u"price"_qs, priceSpin->value());
    payload.insert(u"copies"_qs, copies);

    const PrintResult result = postLabel(payload);
    if (!result.ok)
        showServiceError(dialog, result.error);
}

void printSelectedLots(MainWindow *window)
{
    View *view = window ? window->activeView() : nullptr;
    if (!view) {
        QMessageBox::information(window,
                                 QObject::tr("Print DK-11204 labels"),
                                 QObject::tr("Open a BrickStore document first."));
        return;
    }

    const BrickLink::LotList lots = view->selectedLots();
    if (lots.isEmpty()) {
        QMessageBox::information(window,
                                 QObject::tr("Print DK-11204 labels"),
                                 QObject::tr("Select one or more rows first."));
        return;
    }

    bool ok = false;
    const int copies = QInputDialog::getInt(
        window,
        QObject::tr("Print DK-11204 labels"),
        lots.size() == 1
            ? QObject::tr("Number of labels:")
            : QObject::tr("Copies per selected row (%1 rows):").arg(lots.size()),
        1, 1, 250, 1, &ok);
    if (!ok)
        return;

    const QString currency = view->model() ? view->model()->currencyCode() : u"CHF"_qs;
    QJsonArray labels;
    for (const BrickLink::Lot *lot : lots) {
        const QJsonObject label = lotPayload(lot, currency, copies);
        if (!label.isEmpty())
            labels.append(label);
    }

    if (labels.isEmpty()) {
        QMessageBox::warning(window,
                             QObject::tr("Print DK-11204 labels"),
                             QObject::tr("The selected rows did not contain printable items."));
        return;
    }

    QJsonObject payload;
    payload.insert(u"labels"_qs, labels);

    const PrintResult result = postLabel(payload);
    if (!result.ok)
        showServiceError(window, result.error);
}

void installAddItemButton(AddItemDialog *dialog)
{
    if (!dialog || dialog->property("brickstore.dk11204.installed").toBool())
        return;

    auto *box = dialog->findChild<QDialogButtonBox *>();
    if (!box)
        return;

    auto *button = new QPushButton(QObject::tr("Print DK-11204"), box);
    button->setObjectName(u"w_print_dk11204"_qs);
    button->setToolTip(QObject::tr(
        "Print through the local brother_ql service. This bypasses the Windows Brother printer driver."));
    box->addButton(button, QDialogButtonBox::ActionRole);
    QObject::connect(button, &QPushButton::clicked, dialog,
                     [dialog]() { printLabel(dialog); });
    dialog->setProperty("brickstore.dk11204.installed", true);
}

void installMainMenu(MainWindow *window)
{
    if (!window || window->property("brickstore.dk11204.menu-installed").toBool())
        return;

    QMenu *menu = window->menuBar()->addMenu(QObject::tr("&Labels"));
    menu->setObjectName(u"menu_isaku_labels"_qs);

    QAction *printAction = menu->addAction(QObject::tr("Print selected DK-11204 label(s)..."));
    printAction->setObjectName(u"action_print_selected_dk11204"_qs);
    printAction->setToolTip(QObject::tr(
        "Print one or more selected BrickStore rows through the local brother_ql service."));
    QObject::connect(printAction, &QAction::triggered, window,
                     [window]() { printSelectedLots(window); });

    menu->addSeparator();
    QAction *configAction = menu->addAction(QObject::tr("Brother QL service URL..."));
    QObject::connect(configAction, &QAction::triggered, window,
                     [window]() { configureLabelService(window); });

    QObject::connect(menu, &QMenu::aboutToShow, window, [window, printAction]() {
        View *view = window->activeView();
        printAction->setEnabled(view && !view->selectedLots().isEmpty());
    });

    window->setProperty("brickstore.dk11204.menu-installed", true);
}

class BrickLabelIntegration final : public QObject
{
public:
    explicit BrickLabelIntegration(QObject *parent = nullptr)
        : QObject(parent)
    { }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            if (auto *dialog = qobject_cast<AddItemDialog *>(watched)) {
                QTimer::singleShot(0, dialog, [dialog]() {
                    installAddItemButton(dialog);
                });
            } else if (auto *window = qobject_cast<MainWindow *>(watched)) {
                QTimer::singleShot(0, window, [window]() {
                    installMainMenu(window);
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void installBrickLabelIntegration()
{
    if (!qApp)
        return;
    auto *integration = new BrickLabelIntegration(qApp);
    qApp->installEventFilter(integration);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installBrickLabelIntegration)
