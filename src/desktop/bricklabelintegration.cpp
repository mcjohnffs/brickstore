// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

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
constexpr auto LabelSettingsPrefix = "Isaku/DK11204/";

QString labelKey(const char *name)
{
    return QString::fromLatin1(LabelSettingsPrefix) + QString::fromLatin1(name);
}

QVariant labelValue(const char *name, const QVariant &defaultValue)
{
    return Config::inst()->value(labelKey(name), defaultValue);
}

void setLabelValue(const char *name, const QVariant &value)
{
    Config::inst()->setValue(labelKey(name), value);
}

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
        QObject::tr("Local service URL (not the printer IP):"),
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
                             QObject::tr("Please enter the local HTTP service URL, normally http://127.0.0.1:8013."));
        return false;
    }

    Config::inst()->setValue(QString::fromLatin1(LabelServiceConfigKey), value);
    return true;
}

QJsonObject labelSettingsJson()
{
    QJsonObject s;
    s.insert(u"show_item_id"_qs, labelValue("ShowItemId", true).toBool());
    s.insert(u"show_name"_qs, labelValue("ShowName", true).toBool());
    s.insert(u"show_color"_qs, labelValue("ShowColor", true).toBool());
    s.insert(u"show_condition"_qs, labelValue("ShowCondition", true).toBool());
    s.insert(u"show_price"_qs, labelValue("ShowPrice", true).toBool());
    s.insert(u"show_currency"_qs, labelValue("ShowCurrency", true).toBool());
    s.insert(u"show_quantity"_qs, labelValue("ShowQuantity", false).toBool());
    s.insert(u"show_remarks"_qs, labelValue("ShowRemarks", false).toBool());
    s.insert(u"show_comments"_qs, labelValue("ShowComments", false).toBool());
    s.insert(u"show_separator"_qs, labelValue("ShowSeparator", true).toBool());

    s.insert(u"font_item_id"_qs, labelValue("FontItemId", 39).toInt());
    s.insert(u"font_price"_qs, labelValue("FontPrice", 39).toInt());
    s.insert(u"font_name"_qs, labelValue("FontName", 29).toInt());
    s.insert(u"font_meta"_qs, labelValue("FontMeta", 24).toInt());
    s.insert(u"font_extra"_qs, labelValue("FontExtra", 19).toInt());

    s.insert(u"bold_item_id"_qs, labelValue("BoldItemId", true).toBool());
    s.insert(u"bold_price"_qs, labelValue("BoldPrice", true).toBool());
    s.insert(u"bold_name"_qs, labelValue("BoldName", false).toBool());
    s.insert(u"bold_meta"_qs, labelValue("BoldMeta", false).toBool());
    s.insert(u"bold_extra"_qs, labelValue("BoldExtra", false).toBool());

    s.insert(u"y_item_id"_qs, labelValue("YItemId", 4).toInt());
    s.insert(u"y_price"_qs, labelValue("YPrice", 4).toInt());
    s.insert(u"y_name"_qs, labelValue("YName", 57).toInt());
    s.insert(u"y_meta"_qs, labelValue("YMeta", 105).toInt());
    s.insert(u"y_extra"_qs, labelValue("YExtra", 132).toInt());
    s.insert(u"y_separator"_qs, labelValue("YSeparator", 158).toInt());
    s.insert(u"margin_left"_qs, labelValue("MarginLeft", 12).toInt());
    s.insert(u"margin_right"_qs, labelValue("MarginRight", 12).toInt());
    s.insert(u"top_split"_qs, labelValue("TopSplit", 275).toInt());
    s.insert(u"separator_width"_qs, labelValue("SeparatorWidth", 2).toInt());

    s.insert(u"rounding_mode"_qs, labelValue("RoundingMode", u"none"_qs).toString());
    s.insert(u"rounding_step"_qs, labelValue("RoundingStep", 0.05).toDouble());
    s.insert(u"price_decimals"_qs, labelValue("PriceDecimals", 2).toInt());
    s.insert(u"currency_before"_qs, labelValue("CurrencyBefore", true).toBool());

    s.insert(u"threshold"_qs, labelValue("Threshold", 70.0).toDouble());
    s.insert(u"dither"_qs, labelValue("Dither", false).toBool());
    s.insert(u"hq"_qs, labelValue("HighQuality", true).toBool());
    s.insert(u"cut"_qs, labelValue("AutoCut", true).toBool());
    return s;
}

bool configureLabelSettings(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("DK-11204 label settings"));
    dialog.resize(620, 610);

    auto *outer = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    outer->addWidget(tabs, 1);

    // CONTENT
    auto *content = new QWidget(tabs);
    auto *contentGrid = new QGridLayout(content);
    auto makeCheck = [content](const QString &text, bool checked) {
        auto *c = new QCheckBox(text, content);
        c->setChecked(checked);
        return c;
    };

    auto *showItemId = makeCheck(QObject::tr("Item number"), labelValue("ShowItemId", true).toBool());
    auto *showName = makeCheck(QObject::tr("Item name"), labelValue("ShowName", true).toBool());
    auto *showColor = makeCheck(QObject::tr("Color"), labelValue("ShowColor", true).toBool());
    auto *showCondition = makeCheck(QObject::tr("Condition"), labelValue("ShowCondition", true).toBool());
    auto *showPrice = makeCheck(QObject::tr("Price"), labelValue("ShowPrice", true).toBool());
    auto *showCurrency = makeCheck(QObject::tr("Currency (CHF etc.)"), labelValue("ShowCurrency", true).toBool());
    auto *showQuantity = makeCheck(QObject::tr("Quantity"), labelValue("ShowQuantity", false).toBool());
    auto *showRemarks = makeCheck(QObject::tr("Remarks"), labelValue("ShowRemarks", false).toBool());
    auto *showComments = makeCheck(QObject::tr("Comments"), labelValue("ShowComments", false).toBool());
    auto *showSeparator = makeCheck(QObject::tr("Bottom alignment/separator line"), labelValue("ShowSeparator", true).toBool());

    contentGrid->addWidget(showItemId, 0, 0);
    contentGrid->addWidget(showName, 0, 1);
    contentGrid->addWidget(showColor, 1, 0);
    contentGrid->addWidget(showCondition, 1, 1);
    contentGrid->addWidget(showPrice, 2, 0);
    contentGrid->addWidget(showCurrency, 2, 1);
    contentGrid->addWidget(showQuantity, 3, 0);
    contentGrid->addWidget(showRemarks, 3, 1);
    contentGrid->addWidget(showComments, 4, 0);
    contentGrid->addWidget(showSeparator, 4, 1);
    contentGrid->setRowStretch(5, 1);
    tabs->addTab(content, QObject::tr("Content"));

    // APPEARANCE / POSITION
    auto *appearance = new QWidget(tabs);
    auto *appearanceGrid = new QGridLayout(appearance);
    appearanceGrid->addWidget(new QLabel(QObject::tr("Field"), appearance), 0, 0);
    appearanceGrid->addWidget(new QLabel(QObject::tr("Font px"), appearance), 0, 1);
    appearanceGrid->addWidget(new QLabel(QObject::tr("Y position"), appearance), 0, 2);
    appearanceGrid->addWidget(new QLabel(QObject::tr("Bold"), appearance), 0, 3);

    auto makeSpin = [appearance](int value, int minimum, int maximum) {
        auto *s = new QSpinBox(appearance);
        s->setRange(minimum, maximum);
        s->setValue(value);
        return s;
    };
    auto addFieldRow = [&](int row, const QString &name, const char *fontKey, int fontDefault,
                           const char *yKey, int yDefault, const char *boldKey, bool boldDefault,
                           QSpinBox *&fontOut, QSpinBox *&yOut, QCheckBox *&boldOut) {
        appearanceGrid->addWidget(new QLabel(name, appearance), row, 0);
        fontOut = makeSpin(labelValue(fontKey, fontDefault).toInt(), 6, 100);
        yOut = makeSpin(labelValue(yKey, yDefault).toInt(), 0, 164);
        boldOut = new QCheckBox(appearance);
        boldOut->setChecked(labelValue(boldKey, boldDefault).toBool());
        appearanceGrid->addWidget(fontOut, row, 1);
        appearanceGrid->addWidget(yOut, row, 2);
        appearanceGrid->addWidget(boldOut, row, 3);
    };

    QSpinBox *fontItemId = nullptr, *yItemId = nullptr;
    QSpinBox *fontPrice = nullptr, *yPrice = nullptr;
    QSpinBox *fontName = nullptr, *yName = nullptr;
    QSpinBox *fontMeta = nullptr, *yMeta = nullptr;
    QSpinBox *fontExtra = nullptr, *yExtra = nullptr;
    QCheckBox *boldItemId = nullptr, *boldPrice = nullptr, *boldName = nullptr,
              *boldMeta = nullptr, *boldExtra = nullptr;

    addFieldRow(1, QObject::tr("Item number"), "FontItemId", 39, "YItemId", 4,
                "BoldItemId", true, fontItemId, yItemId, boldItemId);
    addFieldRow(2, QObject::tr("Price"), "FontPrice", 39, "YPrice", 4,
                "BoldPrice", true, fontPrice, yPrice, boldPrice);
    addFieldRow(3, QObject::tr("Item name"), "FontName", 29, "YName", 57,
                "BoldName", false, fontName, yName, boldName);
    addFieldRow(4, QObject::tr("Color / condition"), "FontMeta", 24, "YMeta", 105,
                "BoldMeta", false, fontMeta, yMeta, boldMeta);
    addFieldRow(5, QObject::tr("Quantity / remarks / comments"), "FontExtra", 19, "YExtra", 132,
                "BoldExtra", false, fontExtra, yExtra, boldExtra);

    auto *layoutGroup = new QGroupBox(QObject::tr("Margins and split"), appearance);
    auto *layoutForm = new QFormLayout(layoutGroup);
    auto *marginLeft = makeSpin(labelValue("MarginLeft", 12).toInt(), 0, 100);
    auto *marginRight = makeSpin(labelValue("MarginRight", 12).toInt(), 0, 100);
    auto *topSplit = makeSpin(labelValue("TopSplit", 275).toInt(), 60, 500);
    auto *ySeparator = makeSpin(labelValue("YSeparator", 158).toInt(), 0, 164);
    auto *separatorWidth = makeSpin(labelValue("SeparatorWidth", 2).toInt(), 1, 8);
    layoutForm->addRow(QObject::tr("Left margin (px):"), marginLeft);
    layoutForm->addRow(QObject::tr("Right margin (px):"), marginRight);
    layoutForm->addRow(QObject::tr("Top row split X (px):"), topSplit);
    layoutForm->addRow(QObject::tr("Separator Y (px):"), ySeparator);
    layoutForm->addRow(QObject::tr("Separator width (px):"), separatorWidth);
    appearanceGrid->addWidget(layoutGroup, 6, 0, 1, 4);
    appearanceGrid->setRowStretch(7, 1);
    tabs->addTab(appearance, QObject::tr("Size & layout"));

    // PRICE
    auto *priceTab = new QWidget(tabs);
    auto *priceForm = new QFormLayout(priceTab);
    auto *roundingMode = new QComboBox(priceTab);
    roundingMode->addItem(QObject::tr("No rounding"), u"none"_qs);
    roundingMode->addItem(QObject::tr("Nearest step"), u"nearest"_qs);
    roundingMode->addItem(QObject::tr("Always round up"), u"up"_qs);
    roundingMode->addItem(QObject::tr("Always round down"), u"down"_qs);
    const QString currentMode = labelValue("RoundingMode", u"none"_qs).toString();
    int modeIndex = roundingMode->findData(currentMode);
    roundingMode->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);

    auto *roundingStep = new QDoubleSpinBox(priceTab);
    roundingStep->setRange(0.01, 100.0);
    roundingStep->setDecimals(2);
    roundingStep->setSingleStep(0.05);
    roundingStep->setValue(labelValue("RoundingStep", 0.05).toDouble());
    roundingStep->setSuffix(u" CHF"_qs);

    auto *priceDecimals = new QSpinBox(priceTab);
    priceDecimals->setRange(0, 4);
    priceDecimals->setValue(labelValue("PriceDecimals", 2).toInt());

    auto *currencyBefore = new QCheckBox(QObject::tr("Currency before price (CHF 1.50 instead of 1.50 CHF)"), priceTab);
    currencyBefore->setChecked(labelValue("CurrencyBefore", true).toBool());

    auto *roundingExample = new QLabel(priceTab);
    auto updateExample = [=]() {
        const QString mode = roundingMode->currentData().toString();
        const double step = roundingStep->value();
        const double raw = 12.37;
        double rounded = raw;
        if (mode != u"none"_qs && step > 0) {
            const double units = raw / step;
            if (mode == u"up"_qs)
                rounded = qCeil(units - 1e-9) * step;
            else if (mode == u"down"_qs)
                rounded = qFloor(units + 1e-9) * step;
            else
                rounded = qFloor(units + 0.5) * step;
        }
        const QString number = QString::number(rounded, 'f', priceDecimals->value());
        roundingExample->setText(QObject::tr("Example: CHF 12.37 → %1").arg(number));
    };
    QObject::connect(roundingMode, &QComboBox::currentIndexChanged, &dialog, updateExample);
    QObject::connect(roundingStep, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog, updateExample);
    QObject::connect(priceDecimals, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, updateExample);
    updateExample();

    priceForm->addRow(QObject::tr("Rounding:"), roundingMode);
    priceForm->addRow(QObject::tr("Rounding step:"), roundingStep);
    priceForm->addRow(QObject::tr("Printed decimals:"), priceDecimals);
    priceForm->addRow(currencyBefore);
    priceForm->addRow(roundingExample);
    tabs->addTab(priceTab, QObject::tr("Price"));

    // PRINTER RASTER OPTIONS
    auto *printerTab = new QWidget(tabs);
    auto *printerForm = new QFormLayout(printerTab);
    auto *threshold = new QDoubleSpinBox(printerTab);
    threshold->setRange(1.0, 99.0);
    threshold->setDecimals(0);
    threshold->setValue(labelValue("Threshold", 70.0).toDouble());
    auto *dither = new QCheckBox(QObject::tr("Dither grayscale"), printerTab);
    dither->setChecked(labelValue("Dither", false).toBool());
    auto *hq = new QCheckBox(QObject::tr("High quality mode"), printerTab);
    hq->setChecked(labelValue("HighQuality", true).toBool());
    auto *autoCut = new QCheckBox(QObject::tr("Auto-cut after each label"), printerTab);
    autoCut->setChecked(labelValue("AutoCut", true).toBool());
    auto *mediaInfo = new QLabel(QObject::tr("Media is fixed to Brother DK-11204 / 17 × 54 mm (165 × 566 printable dots)."), printerTab);
    mediaInfo->setWordWrap(true);
    printerForm->addRow(QObject::tr("Black/white threshold:"), threshold);
    printerForm->addRow(dither);
    printerForm->addRow(hq);
    printerForm->addRow(autoCut);
    printerForm->addRow(mediaInfo);
    tabs->addTab(printerTab, QObject::tr("Printer"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *reset = buttons->addButton(QObject::tr("Reset defaults"), QDialogButtonBox::ResetRole);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(reset, &QPushButton::clicked, &dialog, [=]() {
        showItemId->setChecked(true); showName->setChecked(true); showColor->setChecked(true);
        showCondition->setChecked(true); showPrice->setChecked(true); showCurrency->setChecked(true);
        showQuantity->setChecked(false); showRemarks->setChecked(false); showComments->setChecked(false);
        showSeparator->setChecked(true);
        fontItemId->setValue(39); fontPrice->setValue(39); fontName->setValue(29);
        fontMeta->setValue(24); fontExtra->setValue(19);
        yItemId->setValue(4); yPrice->setValue(4); yName->setValue(57);
        yMeta->setValue(105); yExtra->setValue(132); ySeparator->setValue(158);
        boldItemId->setChecked(true); boldPrice->setChecked(true); boldName->setChecked(false);
        boldMeta->setChecked(false); boldExtra->setChecked(false);
        marginLeft->setValue(12); marginRight->setValue(12); topSplit->setValue(275);
        separatorWidth->setValue(2);
        roundingMode->setCurrentIndex(0); roundingStep->setValue(0.05); priceDecimals->setValue(2);
        currencyBefore->setChecked(true);
        threshold->setValue(70.0); dither->setChecked(false); hq->setChecked(true); autoCut->setChecked(true);
    });

    if (dialog.exec() != QDialog::Accepted)
        return false;

    setLabelValue("ShowItemId", showItemId->isChecked());
    setLabelValue("ShowName", showName->isChecked());
    setLabelValue("ShowColor", showColor->isChecked());
    setLabelValue("ShowCondition", showCondition->isChecked());
    setLabelValue("ShowPrice", showPrice->isChecked());
    setLabelValue("ShowCurrency", showCurrency->isChecked());
    setLabelValue("ShowQuantity", showQuantity->isChecked());
    setLabelValue("ShowRemarks", showRemarks->isChecked());
    setLabelValue("ShowComments", showComments->isChecked());
    setLabelValue("ShowSeparator", showSeparator->isChecked());

    setLabelValue("FontItemId", fontItemId->value());
    setLabelValue("FontPrice", fontPrice->value());
    setLabelValue("FontName", fontName->value());
    setLabelValue("FontMeta", fontMeta->value());
    setLabelValue("FontExtra", fontExtra->value());
    setLabelValue("YItemId", yItemId->value());
    setLabelValue("YPrice", yPrice->value());
    setLabelValue("YName", yName->value());
    setLabelValue("YMeta", yMeta->value());
    setLabelValue("YExtra", yExtra->value());
    setLabelValue("BoldItemId", boldItemId->isChecked());
    setLabelValue("BoldPrice", boldPrice->isChecked());
    setLabelValue("BoldName", boldName->isChecked());
    setLabelValue("BoldMeta", boldMeta->isChecked());
    setLabelValue("BoldExtra", boldExtra->isChecked());
    setLabelValue("MarginLeft", marginLeft->value());
    setLabelValue("MarginRight", marginRight->value());
    setLabelValue("TopSplit", topSplit->value());
    setLabelValue("YSeparator", ySeparator->value());
    setLabelValue("SeparatorWidth", separatorWidth->value());

    setLabelValue("RoundingMode", roundingMode->currentData().toString());
    setLabelValue("RoundingStep", roundingStep->value());
    setLabelValue("PriceDecimals", priceDecimals->value());
    setLabelValue("CurrencyBefore", currencyBefore->isChecked());
    setLabelValue("Threshold", threshold->value());
    setLabelValue("Dither", dither->isChecked());
    setLabelValue("HighQuality", hq->isChecked());
    setLabelValue("AutoCut", autoCut->isChecked());
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
    auto *changeButton = box.addButton(QObject::tr("Change local service URL..."),
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
    payload.insert(u"quantity"_qs, lot->quantity());
    payload.insert(u"remarks"_qs, lot->remarks());
    payload.insert(u"comments"_qs, lot->comments());
    payload.insert(u"copies"_qs, copies);
    return payload;
}

void printLabel(AddItemDialog *dialog)
{
    auto *itemSelector = dialog->findChild<SelectItem *>();
    auto *colorSelector = dialog->findChild<SelectColor *>();
    auto *priceSpin = dialog->findChild<QDoubleSpinBox *>(u"w_price"_qs);
    auto *quantitySpin = dialog->findChild<QSpinBox *>(u"w_qty"_qs);
    auto *remarksEdit = dialog->findChild<QLineEdit *>(u"w_remarks"_qs);
    auto *commentsEdit = dialog->findChild<QLineEdit *>(u"w_comments"_qs);
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
    payload.insert(u"quantity"_qs, quantitySpin ? quantitySpin->value() : 1);
    payload.insert(u"remarks"_qs, remarksEdit ? remarksEdit->text() : QString());
    payload.insert(u"comments"_qs, commentsEdit ? commentsEdit->text() : QString());
    payload.insert(u"copies"_qs, copies);
    payload.insert(u"settings"_qs, labelSettingsJson());

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
    payload.insert(u"settings"_qs, labelSettingsJson());

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
    QAction *settingsAction = menu->addAction(QObject::tr("Label settings..."));
    QObject::connect(settingsAction, &QAction::triggered, window,
                     [window]() { configureLabelSettings(window); });

    QAction *configAction = menu->addAction(QObject::tr("Local Brother QL service URL..."));
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
