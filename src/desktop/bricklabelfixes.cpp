// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <cmath>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
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
#include "common/documentmodel.h"

namespace {

constexpr auto LabelServiceConfigKey = "MainWindow/AddItemDialog/BrotherQLServiceUrl";
constexpr auto DefaultLabelServiceUrl = "http://127.0.0.1:8013";
constexpr auto LabelSettingsPrefix = "Isaku/DK11204/";

QString key(const char *name)
{
    return QString::fromLatin1(LabelSettingsPrefix) + QString::fromLatin1(name);
}

QVariant value(const char *name, const QVariant &defaultValue)
{
    return Config::inst()->value(key(name), defaultValue);
}

void setValue(const char *name, const QVariant &v)
{
    Config::inst()->setValue(key(name), v);
}

QString serviceUrl()
{
    return Config::inst()->value(QString::fromLatin1(LabelServiceConfigKey),
                                 QString::fromLatin1(DefaultLabelServiceUrl))
        .toString().trimmed();
}

QString addItemCurrency(AddItemDialog *dialog)
{
    if (auto *label = dialog->findChild<QLabel *>(u"w_label_currency"_qs)) {
        QString text = label->text();
        const int open = text.indexOf(u'(');
        const int close = text.indexOf(u')', open + 1);
        if (open >= 0 && close > open)
            return text.mid(open + 1, close - open - 1).remove(u'&').trimmed();
    }
    return u"CHF"_qs;
}

double roundedPrice(double raw)
{
    const QString mode = value("RoundingMode", u"none"_qs).toString().toLower();
    const double step = value("RoundingStep", 0.05).toDouble();
    if (mode == u"none"_qs || step <= 0.0)
        return raw;

    const double units = raw / step;
    if (mode == u"up"_qs)
        return std::ceil(units - 1e-9) * step;
    if (mode == u"down"_qs)
        return std::floor(units + 1e-9) * step;
    return std::floor(units + 0.5 + 1e-9) * step;
}

QJsonObject settingsJson()
{
    QJsonObject s;
    s.insert(u"show_item_id"_qs, value("ShowItemId", true).toBool());
    s.insert(u"show_index"_qs, value("ShowIndex", true).toBool());
    s.insert(u"show_name"_qs, value("ShowName", true).toBool());
    s.insert(u"show_color"_qs, value("ShowColor", true).toBool());
    s.insert(u"show_condition"_qs, value("ShowCondition", true).toBool());
    s.insert(u"show_price"_qs, value("ShowPrice", true).toBool());
    s.insert(u"show_currency"_qs, value("ShowCurrency", true).toBool());
    s.insert(u"show_quantity"_qs, value("ShowQuantity", false).toBool());
    s.insert(u"show_remarks"_qs, value("ShowRemarks", false).toBool());
    s.insert(u"show_comments"_qs, value("ShowComments", false).toBool());
    s.insert(u"show_separator"_qs, value("ShowSeparator", true).toBool());

    s.insert(u"font_item_id"_qs, value("FontItemId", 39).toInt());
    s.insert(u"font_index"_qs, value("FontIndex", 19).toInt());
    s.insert(u"font_price"_qs, value("FontPrice", 39).toInt());
    s.insert(u"font_name"_qs, value("FontName", 29).toInt());
    s.insert(u"font_meta"_qs, value("FontMeta", 24).toInt());
    s.insert(u"font_extra"_qs, value("FontExtra", 19).toInt());

    s.insert(u"bold_item_id"_qs, value("BoldItemId", true).toBool());
    s.insert(u"bold_index"_qs, value("BoldIndex", true).toBool());
    s.insert(u"bold_price"_qs, value("BoldPrice", true).toBool());
    s.insert(u"bold_name"_qs, value("BoldName", false).toBool());
    s.insert(u"bold_meta"_qs, value("BoldMeta", false).toBool());
    s.insert(u"bold_extra"_qs, value("BoldExtra", false).toBool());

    s.insert(u"x_index"_qs, value("XIndex", 12).toInt());
    s.insert(u"y_index"_qs, value("YIndex", 132).toInt());
    s.insert(u"index_prefix"_qs, value("IndexPrefix", u"#"_qs).toString());
    s.insert(u"y_item_id"_qs, value("YItemId", 4).toInt());
    s.insert(u"y_price"_qs, value("YPrice", 4).toInt());
    s.insert(u"y_name"_qs, value("YName", 57).toInt());
    s.insert(u"y_meta"_qs, value("YMeta", 105).toInt());
    s.insert(u"y_extra"_qs, value("YExtra", 132).toInt());
    s.insert(u"y_separator"_qs, value("YSeparator", 158).toInt());
    s.insert(u"margin_left"_qs, value("MarginLeft", 12).toInt());
    s.insert(u"margin_right"_qs, value("MarginRight", 12).toInt());
    s.insert(u"top_split"_qs, value("TopSplit", 275).toInt());
    s.insert(u"separator_width"_qs, value("SeparatorWidth", 2).toInt());

    s.insert(u"rounding_mode"_qs, value("RoundingMode", u"none"_qs).toString());
    s.insert(u"rounding_step"_qs, value("RoundingStep", 0.05).toDouble());
    s.insert(u"price_decimals"_qs, value("PriceDecimals", 2).toInt());
    s.insert(u"currency_before"_qs, value("CurrencyBefore", true).toBool());
    s.insert(u"threshold"_qs, value("Threshold", 70.0).toDouble());
    s.insert(u"dither"_qs, value("Dither", false).toBool());
    s.insert(u"hq"_qs, value("HighQuality", true).toBool());
    s.insert(u"cut"_qs, value("AutoCut", true).toBool());
    return s;
}

struct Result
{
    bool ok = false;
    QString error;
};

Result post(const QJsonObject &payload)
{
    QString base = serviceUrl();
    while (base.endsWith(u'/'))
        base.chop(1);

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(base + u"/print"_qs));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_qs);
    QNetworkReply *reply = manager.post(request,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

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

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return { false, QObject::tr("Invalid label-service response: %1").arg(QString::fromUtf8(raw)) };
    const QJsonObject object = doc.object();
    if (!object.value(u"ok"_qs).toBool())
        return { false, object.value(u"error"_qs).toString(QObject::tr("Unknown printing error.")) };
    return { true, { } };
}

void showError(QWidget *parent, const QString &error)
{
    QMessageBox::critical(parent, QObject::tr("Print DK-11204 label"),
                          QObject::tr("Brother QL printing failed.\n\nService: %1\n\n%2")
                              .arg(serviceUrl(), error));
}

QString displayedIndex(DocumentModel *model, const BrickLink::Lot *lot)
{
    if (!model || !lot)
        return { };
    QString result = model->dataForDisplayRole(lot, DocumentModel::Index, false).trimmed();
    if (!result.isEmpty())
        return result;
    const QModelIndex idx = model->index(lot);
    return idx.isValid() ? QString::number(idx.row() + 1) : QString();
}

QJsonObject lotPayload(DocumentModel *model, const BrickLink::Lot *lot,
                       const QString &currency, int copies)
{
    QJsonObject p;
    if (!lot)
        return p;
    p.insert(u"index"_qs, displayedIndex(model, lot));
    p.insert(u"item_id"_qs, QString::fromLatin1(lot->itemId()));
    p.insert(u"name"_qs, lot->itemName());
    p.insert(u"color"_qs, lot->colorName().isEmpty() ? QObject::tr("No color") : lot->colorName());
    p.insert(u"condition"_qs, lot->condition() == BrickLink::Condition::Used
                                      ? QObject::tr("Used") : QObject::tr("New"));
    p.insert(u"currency"_qs, currency.isEmpty() ? u"CHF"_qs : currency);
    p.insert(u"price"_qs, roundedPrice(lot->price()));
    p.insert(u"quantity"_qs, lot->quantity());
    p.insert(u"remarks"_qs, lot->remarks());
    p.insert(u"comments"_qs, lot->comments());
    p.insert(u"copies"_qs, copies);
    return p;
}

void printSelected(MainWindow *window)
{
    View *view = window ? window->activeView() : nullptr;
    if (!view || view->selectedLots().isEmpty()) {
        QMessageBox::information(window, QObject::tr("Print DK-11204 labels"),
                                 QObject::tr("Select one or more rows first."));
        return;
    }

    bool ok = false;
    const auto lots = view->selectedLots();
    const int copies = QInputDialog::getInt(window, QObject::tr("Print DK-11204 labels"),
        lots.size() == 1 ? QObject::tr("Number of labels:")
                         : QObject::tr("Copies per selected row (%1 rows):").arg(lots.size()),
        1, 1, 250, 1, &ok);
    if (!ok)
        return;

    DocumentModel *model = view->model();
    const QString currency = model ? model->currencyCode() : u"CHF"_qs;
    QJsonArray labels;
    for (const BrickLink::Lot *lot : lots)
        labels.append(lotPayload(model, lot, currency, copies));

    QJsonObject payload;
    payload.insert(u"labels"_qs, labels);
    payload.insert(u"settings"_qs, settingsJson());
    const Result result = post(payload);
    if (!result.ok)
        showError(window, result.error);
}

void printAddItem(AddItemDialog *dialog)
{
    auto *itemSelector = dialog->findChild<SelectItem *>();
    auto *colorSelector = dialog->findChild<SelectColor *>();
    auto *price = dialog->findChild<QDoubleSpinBox *>(u"w_price"_qs);
    auto *quantity = dialog->findChild<QSpinBox *>(u"w_qty"_qs);
    auto *remarks = dialog->findChild<QLineEdit *>(u"w_remarks"_qs);
    auto *comments = dialog->findChild<QLineEdit *>(u"w_comments"_qs);
    auto *used = dialog->findChild<QRadioButton *>(u"w_radio_used"_qs);
    const BrickLink::Item *item = itemSelector ? itemSelector->currentItem() : nullptr;
    const BrickLink::Color *color = colorSelector ? colorSelector->currentColor() : nullptr;
    if (!item || !price) {
        QMessageBox::warning(dialog, QObject::tr("Print DK-11204 label"),
                             QObject::tr("Select an item first."));
        return;
    }

    bool ok = false;
    const int copies = QInputDialog::getInt(dialog, QObject::tr("Print DK-11204 label"),
                                             QObject::tr("Number of labels:"),
                                             1, 1, 250, 1, &ok);
    if (!ok)
        return;

    QJsonObject payload;
    payload.insert(u"item_id"_qs, QString::fromLatin1(item->id()));
    payload.insert(u"name"_qs, item->name());
    payload.insert(u"color"_qs, color ? color->name() : QObject::tr("No color"));
    payload.insert(u"condition"_qs, used && used->isChecked() ? QObject::tr("Used") : QObject::tr("New"));
    payload.insert(u"currency"_qs, addItemCurrency(dialog));
    payload.insert(u"price"_qs, roundedPrice(price->value()));
    payload.insert(u"quantity"_qs, quantity ? quantity->value() : 1);
    payload.insert(u"remarks"_qs, remarks ? remarks->text() : QString());
    payload.insert(u"comments"_qs, comments ? comments->text() : QString());
    payload.insert(u"copies"_qs, copies);
    payload.insert(u"settings"_qs, settingsJson());

    const Result result = post(payload);
    if (!result.ok)
        showError(dialog, result.error);
}

void augmentSettingsDialog(QDialog *dialog)
{
    if (!dialog || dialog->property("isaku.index-settings-added").toBool())
        return;
    QTabWidget *tabs = dialog->findChild<QTabWidget *>();
    if (!tabs || tabs->count() < 2)
        return;

    QWidget *content = tabs->widget(0);
    auto *contentGrid = qobject_cast<QGridLayout *>(content->layout());
    if (!contentGrid)
        return;

    auto *showIndex = new QCheckBox(QObject::tr("Index number"), content);
    showIndex->setObjectName(u"isaku_show_index"_qs);
    showIndex->setChecked(value("ShowIndex", true).toBool());
    contentGrid->addWidget(showIndex, 5, 0);

    QWidget *appearance = tabs->widget(1);
    auto *appearanceGrid = qobject_cast<QGridLayout *>(appearance->layout());
    if (!appearanceGrid)
        return;

    auto *group = new QGroupBox(QObject::tr("Index number"), appearance);
    auto *form = new QFormLayout(group);
    auto *font = new QSpinBox(group);
    font->setRange(6, 100);
    font->setValue(value("FontIndex", 19).toInt());
    auto *x = new QSpinBox(group);
    x->setRange(0, 565);
    x->setValue(value("XIndex", 12).toInt());
    auto *y = new QSpinBox(group);
    y->setRange(0, 164);
    y->setValue(value("YIndex", 132).toInt());
    auto *bold = new QCheckBox(QObject::tr("Bold"), group);
    bold->setChecked(value("BoldIndex", true).toBool());
    auto *prefix = new QLineEdit(value("IndexPrefix", u"#"_qs).toString(), group);
    prefix->setMaxLength(8);
    form->addRow(QObject::tr("Font px:"), font);
    form->addRow(QObject::tr("X position:"), x);
    form->addRow(QObject::tr("Y position:"), y);
    form->addRow(QObject::tr("Prefix:"), prefix);
    form->addRow(bold);
    appearanceGrid->addWidget(group, 8, 0, 1, 4);

    QObject::connect(dialog, &QDialog::accepted, dialog, [=]() {
        setValue("ShowIndex", showIndex->isChecked());
        setValue("FontIndex", font->value());
        setValue("XIndex", x->value());
        setValue("YIndex", y->value());
        setValue("BoldIndex", bold->isChecked());
        setValue("IndexPrefix", prefix->text());
    });

    const auto buttons = dialog->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text().contains(QObject::tr("Reset defaults"), Qt::CaseInsensitive)) {
            QObject::connect(button, &QPushButton::clicked, dialog, [=]() {
                showIndex->setChecked(true);
                font->setValue(19);
                x->setValue(12);
                y->setValue(132);
                bold->setChecked(true);
                prefix->setText(u"#"_qs);
            });
            break;
        }
    }

    dialog->setProperty("isaku.index-settings-added", true);
}

void patchMainWindow(MainWindow *window)
{
    if (!window || window->property("isaku.label-fixes-installed").toBool())
        return;
    QAction *action = window->findChild<QAction *>(u"action_print_selected_dk11204"_qs);
    if (!action)
        return;
    QObject::disconnect(action, nullptr, nullptr, nullptr);
    QObject::connect(action, &QAction::triggered, window, [window]() { printSelected(window); });
    window->setProperty("isaku.label-fixes-installed", true);
}

void patchAddItem(AddItemDialog *dialog)
{
    if (!dialog || dialog->property("isaku.label-fixes-installed").toBool())
        return;
    auto *button = dialog->findChild<QPushButton *>(u"w_print_dk11204"_qs);
    if (!button)
        return;
    QObject::disconnect(button, nullptr, nullptr, nullptr);
    QObject::connect(button, &QPushButton::clicked, dialog, [dialog]() { printAddItem(dialog); });
    dialog->setProperty("isaku.label-fixes-installed", true);
}

class LabelFixFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return QObject::eventFilter(watched, event);

        if (auto *window = qobject_cast<MainWindow *>(watched)) {
            QTimer::singleShot(50, window, [window]() { patchMainWindow(window); });
        } else if (auto *add = qobject_cast<AddItemDialog *>(watched)) {
            QTimer::singleShot(50, add, [add]() { patchAddItem(add); });
        } else if (auto *dialog = qobject_cast<QDialog *>(watched)) {
            if (dialog->windowTitle().contains(u"DK-11204 label settings"_qs, Qt::CaseInsensitive))
                QTimer::singleShot(0, dialog, [dialog]() { augmentSettingsDialog(dialog); });
        }
        return QObject::eventFilter(watched, event);
    }
};

void installLabelFixes()
{
    if (!qApp)
        return;
    auto *filter = new LabelFixFilter(qApp);
    qApp->installEventFilter(filter);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installLabelFixes)
