// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <cmath>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
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

    s.insert(u"x_index"_qs, value("XIndex", 430).toInt());
    s.insert(u"y_index"_qs, value("YIndex", 126).toInt());
    s.insert(u"index_max_width"_qs, value("IndexMaxWidth", 120).toInt());
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

QJsonObject selectedLotPayload(MainWindow *window, QString *indexOut = nullptr)
{
    QJsonObject p;
    View *view = window ? window->activeView() : nullptr;
    if (!view || view->selectedLots().isEmpty())
        return p;
    const BrickLink::Lot *lot = view->selectedLots().first();
    DocumentModel *model = view->model();
    const QString index = displayedIndex(model, lot);
    if (indexOut)
        *indexOut = index;
    p.insert(u"index"_qs, index);
    p.insert(u"item_id"_qs, QString::fromLatin1(lot->itemId()));
    p.insert(u"name"_qs, lot->itemName());
    p.insert(u"color"_qs, lot->colorName().isEmpty() ? QObject::tr("No color") : lot->colorName());
    p.insert(u"condition"_qs, lot->condition() == BrickLink::Condition::Used
                                  ? QObject::tr("Used") : QObject::tr("New"));
    p.insert(u"currency"_qs, model ? model->currencyCode() : u"CHF"_qs);
    p.insert(u"price"_qs, roundedPrice(lot->price()));
    p.insert(u"quantity"_qs, lot->quantity());
    p.insert(u"remarks"_qs, lot->remarks());
    p.insert(u"comments"_qs, lot->comments());
    p.insert(u"copies"_qs, 1);
    return p;
}

QJsonObject addItemPayload(AddItemDialog *dialog)
{
    QJsonObject p;
    auto *itemSelector = dialog->findChild<SelectItem *>();
    auto *colorSelector = dialog->findChild<SelectColor *>();
    auto *price = dialog->findChild<QDoubleSpinBox *>(u"w_price"_qs);
    auto *quantity = dialog->findChild<QSpinBox *>(u"w_qty"_qs);
    auto *remarks = dialog->findChild<QLineEdit *>(u"w_remarks"_qs);
    auto *comments = dialog->findChild<QLineEdit *>(u"w_comments"_qs);
    auto *used = dialog->findChild<QRadioButton *>(u"w_radio_used"_qs);
    const BrickLink::Item *item = itemSelector ? itemSelector->currentItem() : nullptr;
    const BrickLink::Color *color = colorSelector ? colorSelector->currentColor() : nullptr;
    if (!item || !price)
        return p;

    QString currency = u"CHF"_qs;
    if (auto *label = dialog->findChild<QLabel *>(u"w_label_currency"_qs)) {
        const QString text = label->text();
        const int open = text.indexOf(u'(');
        const int close = text.indexOf(u')', open + 1);
        if (open >= 0 && close > open)
            currency = text.mid(open + 1, close - open - 1).remove(u'&').trimmed();
    }

    p.insert(u"index"_qs, QString());
    p.insert(u"item_id"_qs, QString::fromLatin1(item->id()));
    p.insert(u"name"_qs, item->name());
    p.insert(u"color"_qs, color ? color->name() : QObject::tr("No color"));
    p.insert(u"condition"_qs, used && used->isChecked() ? QObject::tr("Used") : QObject::tr("New"));
    p.insert(u"currency"_qs, currency);
    p.insert(u"price"_qs, roundedPrice(price->value()));
    p.insert(u"quantity"_qs, quantity ? quantity->value() : 1);
    p.insert(u"remarks"_qs, remarks ? remarks->text() : QString());
    p.insert(u"comments"_qs, comments ? comments->text() : QString());
    p.insert(u"copies"_qs, 1);
    return p;
}

struct PreviewResult
{
    QImage image;
    QString error;
};

PreviewResult requestPreview(const QJsonObject &label)
{
    QString base = serviceUrl();
    while (base.endsWith(u'/'))
        base.chop(1);

    QJsonObject payload;
    payload.insert(u"label"_qs, label);
    payload.insert(u"settings"_qs, settingsJson());

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(base + u"/preview"_qs));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_qs);
    QNetworkReply *reply = manager.post(request,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    if (!timeout.isActive()) {
        reply->abort();
        return { { }, QObject::tr("The label service did not respond within 10 seconds.") };
    }
    timeout.stop();

    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
        return { { }, QObject::tr("%1\n%2").arg(reply->errorString(), QString::fromUtf8(raw)) };

    QImage image;
    if (!image.loadFromData(raw, "PNG"))
        return { { }, QObject::tr("The label service did not return a valid PNG preview.") };
    return { image, { } };
}

void showPreview(QWidget *parent, const QJsonObject &label, const QString &indexText = { })
{
    if (label.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("DK-11204 preview"),
                                 QObject::tr("Select an item first."));
        return;
    }

    const PreviewResult result = requestPreview(label);
    if (result.image.isNull()) {
        QMessageBox::critical(parent, QObject::tr("DK-11204 preview"),
                              QObject::tr("Could not load preview from %1.\n\n%2")
                                  .arg(serviceUrl(), result.error));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("DK-11204 label preview"));
    auto *layout = new QVBoxLayout(&dialog);

    auto *info = new QLabel(&dialog);
    QString infoText = QObject::tr("Exact Brother QL renderer preview · 566 × 165 px");
    if (!indexText.isEmpty())
        infoText += QObject::tr(" · Index payload: %1").arg(indexText);
    info->setText(infoText);
    layout->addWidget(info);

    auto *preview = new QLabel(&dialog);
    preview->setFrameShape(QFrame::Box);
    preview->setAlignment(Qt::AlignCenter);
    const QPixmap pixmap = QPixmap::fromImage(result.image).scaled(
        result.image.width() * 2, result.image.height() * 2,
        Qt::KeepAspectRatio, Qt::FastTransformation);
    preview->setPixmap(pixmap);
    preview->setMinimumSize(pixmap.size());
    layout->addWidget(preview, 0, Qt::AlignCenter);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void previewSelected(MainWindow *window)
{
    QString index;
    const QJsonObject label = selectedLotPayload(window, &index);
    showPreview(window, label, index);
}

void previewAddItem(AddItemDialog *dialog)
{
    showPreview(dialog, addItemPayload(dialog));
}

void migrateIndexLayout()
{
    if (value("IndexLayoutV3Migrated", false).toBool())
        return;

    // v2 default was bottom-left, where optional extra fields could cover it.
    // Only move untouched/default-looking values; preserve explicit user layouts.
    const int oldX = value("XIndex", 12).toInt();
    const int oldY = value("YIndex", 132).toInt();
    if (oldX == 12 && oldY == 132) {
        setValue("XIndex", 430);
        setValue("YIndex", 126);
        setValue("IndexMaxWidth", 120);
    }
    setValue("IndexLayoutV3Migrated", true);
}

void patchMainWindow(MainWindow *window)
{
    if (!window || window->property("isaku.label-preview-installed").toBool())
        return;
    QMenu *menu = window->findChild<QMenu *>(u"menu_isaku_labels"_qs);
    if (!menu)
        return;

    QAction *printAction = window->findChild<QAction *>(u"action_print_selected_dk11204"_qs);
    QAction *previewAction = new QAction(QObject::tr("Preview selected DK-11204 label..."), menu);
    previewAction->setObjectName(u"action_preview_selected_dk11204"_qs);
    if (printAction)
        menu->insertAction(printAction, previewAction);
    else
        menu->addAction(previewAction);
    QObject::connect(previewAction, &QAction::triggered, window,
                     [window]() { previewSelected(window); });
    QObject::connect(menu, &QMenu::aboutToShow, window, [window, previewAction]() {
        View *view = window->activeView();
        previewAction->setEnabled(view && !view->selectedLots().isEmpty());
    });

    window->setProperty("isaku.label-preview-installed", true);
}

void patchAddItem(AddItemDialog *dialog)
{
    if (!dialog || dialog->property("isaku.label-preview-installed").toBool())
        return;
    auto *box = dialog->findChild<QDialogButtonBox *>();
    if (!box)
        return;
    auto *button = new QPushButton(QObject::tr("Preview DK-11204"), box);
    button->setObjectName(u"w_preview_dk11204"_qs);
    box->addButton(button, QDialogButtonBox::ActionRole);
    QObject::connect(button, &QPushButton::clicked, dialog,
                     [dialog]() { previewAddItem(dialog); });
    dialog->setProperty("isaku.label-preview-installed", true);
}

void patchSettingsDialog(QDialog *dialog)
{
    if (!dialog || dialog->property("isaku.label-preview-settings-installed").toBool())
        return;
    if (!dialog->windowTitle().contains(u"DK-11204 label settings"_qs, Qt::CaseInsensitive))
        return;

    // Extend the existing Index group with a max-width control.
    const auto groups = dialog->findChildren<QGroupBox *>();
    for (QGroupBox *group : groups) {
        if (!group->title().contains(QObject::tr("Index number"), Qt::CaseInsensitive))
            continue;
        auto *form = qobject_cast<QFormLayout *>(group->layout());
        if (!form)
            continue;
        auto *maxWidth = new QSpinBox(group);
        maxWidth->setObjectName(u"isaku_index_max_width"_qs);
        maxWidth->setRange(30, 500);
        maxWidth->setValue(value("IndexMaxWidth", 120).toInt());
        form->addRow(QObject::tr("Max width (px):"), maxWidth);
        QObject::connect(dialog, &QDialog::accepted, dialog, [maxWidth]() {
            setValue("IndexMaxWidth", maxWidth->value());
        });
        break;
    }

    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    if (buttons) {
        auto *previewButton = new QPushButton(QObject::tr("Preview selected label"), buttons);
        buttons->addButton(previewButton, QDialogButtonBox::ActionRole);
        QObject::connect(previewButton, &QPushButton::clicked, dialog, [dialog]() {
            MainWindow *window = MainWindow::inst();
            if (!window) {
                QMessageBox::information(dialog, QObject::tr("DK-11204 preview"),
                                         QObject::tr("No BrickStore main window is available."));
                return;
            }
            previewSelected(window);
        });
    }

    dialog->setProperty("isaku.label-preview-settings-installed", true);
}

class PreviewFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return QObject::eventFilter(watched, event);

        if (auto *window = qobject_cast<MainWindow *>(watched)) {
            QTimer::singleShot(100, window, [window]() { patchMainWindow(window); });
        } else if (auto *add = qobject_cast<AddItemDialog *>(watched)) {
            QTimer::singleShot(100, add, [add]() { patchAddItem(add); });
        } else if (auto *dialog = qobject_cast<QDialog *>(watched)) {
            if (dialog->windowTitle().contains(u"DK-11204 label settings"_qs, Qt::CaseInsensitive))
                QTimer::singleShot(0, dialog, [dialog]() { patchSettingsDialog(dialog); });
        }
        return QObject::eventFilter(watched, event);
    }
};

void installPreviewIntegration()
{
    if (!qApp)
        return;
    migrateIndexLayout();
    auto *filter = new PreviewFilter(qApp);
    qApp->installEventFilter(filter);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installPreviewIntegration)
