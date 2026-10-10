// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTimer>

#include "additemdialog.h"
#include "selectcolor.h"
#include "selectitem.h"
#include "bricklink/color.h"
#include "bricklink/item.h"
#include "common/config.h"

namespace {

constexpr auto LabelPrinterConfigKey = "MainWindow/AddItemDialog/DK11204Printer";

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

void selectPreferredPrinter(QPrinter &printer)
{
    const QString saved = Config::inst()->value(QString::fromLatin1(LabelPrinterConfigKey)).toString();
    const auto printers = QPrinterInfo::availablePrinters();

    if (!saved.isEmpty()) {
        for (const auto &info : printers) {
            if (info.printerName() == saved) {
                printer.setPrinterName(saved);
                return;
            }
        }
    }

    for (const auto &info : printers) {
        if (info.printerName().contains(u"QL-810W"_qs, Qt::CaseInsensitive)
                || info.printerName().contains(u"Brother QL-810"_qs, Qt::CaseInsensitive)) {
            printer.setPrinterName(info.printerName());
            return;
        }
    }
}

void drawLabel(QPainter &painter, QPrinter &printer,
               const QString &itemId, const QString &itemName,
               const QString &meta, const QString &price)
{
    QRect page = printer.pageLayout().fullRectPixels(printer.resolution());
    if (!page.isValid() || page.isEmpty())
        page = painter.viewport();

    painter.save();
    painter.setViewport(page);
    painter.setWindow(0, 0, 540, 170);
    painter.fillRect(QRect(0, 0, 540, 170), Qt::white);
    painter.setPen(Qt::black);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(37);
    painter.setFont(font);
    painter.drawText(QRect(14, 8, 260, 48), Qt::AlignLeft | Qt::AlignVCenter,
                     itemId);

    font.setPixelSize(35);
    painter.setFont(font);
    painter.drawText(QRect(270, 8, 256, 48), Qt::AlignRight | Qt::AlignVCenter,
                     price);

    font.setBold(false);
    font.setPixelSize(27);
    painter.setFont(font);
    QFontMetrics nameMetrics(font);
    const QString shortName = nameMetrics.elidedText(itemName, Qt::ElideRight, 505);
    painter.drawText(QRect(14, 59, 512, 43), Qt::AlignLeft | Qt::AlignVCenter,
                     shortName);

    font.setPixelSize(23);
    painter.setFont(font);
    QFontMetrics metaMetrics(font);
    const QString shortMeta = metaMetrics.elidedText(meta, Qt::ElideRight, 505);
    painter.drawText(QRect(14, 105, 512, 39), Qt::AlignLeft | Qt::AlignVCenter,
                     shortMeta);

    painter.drawLine(14, 151, 526, 151);
    painter.restore();
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

    QPrinter printer(QPrinter::HighResolution);
    selectPreferredPrinter(printer);
    printer.setFullPage(true);
    printer.setPageSize(QPageSize(QSizeF(54.0, 17.0), QPageSize::Millimeter,
                                  u"Brother DK-11204 17x54mm"_qs));

    QPrintDialog printDialog(&printer, dialog);
    printDialog.setWindowTitle(QObject::tr("Print DK-11204 label"));
    if (printDialog.exec() != QDialog::Accepted)
        return;

    // Re-apply the target label after the native driver dialog. Some drivers reset
    // custom page sizes while the dialog is open.
    printer.setFullPage(true);
    printer.setPageSize(QPageSize(QSizeF(54.0, 17.0), QPageSize::Millimeter,
                                  u"Brother DK-11204 17x54mm"_qs));

    Config::inst()->setValue(QString::fromLatin1(LabelPrinterConfigKey), printer.printerName());

    const QString id = QString::fromLatin1(item->id());
    const QString name = item->name();
    const QString colorName = color ? color->name() : QObject::tr("No color");
    const QString condition = (usedRadio && usedRadio->isChecked())
            ? QObject::tr("Used") : QObject::tr("New");
    const QString meta = colorName + u"  ·  "_qs + condition;
    const QString price = u"%1 %2"_qs.arg(currencyCode(dialog),
                                           QString::number(priceSpin->value(), 'f', 2));

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::critical(dialog, QObject::tr("Print DK-11204 label"),
                              QObject::tr("Could not start the printer."));
        return;
    }

    painter.setRenderHint(QPainter::TextAntialiasing, true);
    for (int copy = 0; copy < copies; ++copy) {
        if (copy && !printer.newPage())
            break;
        drawLabel(painter, printer, id, name, meta, price);
    }
    painter.end();
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
                    if (dialog->property("brickstore.dk11204.installed").toBool())
                        return;

                    auto *box = dialog->findChild<QDialogButtonBox *>();
                    if (!box)
                        return;

                    auto *button = new QPushButton(QObject::tr("Print DK-11204"), box);
                    button->setObjectName(u"w_print_dk11204"_qs);
                    button->setToolTip(QObject::tr(
                        "Print the selected item, color, condition and current price on a 17 x 54 mm Brother DK-11204 label."));
                    box->addButton(button, QDialogButtonBox::ActionRole);
                    QObject::connect(button, &QPushButton::clicked, dialog,
                                     [dialog]() { printLabel(dialog); });
                    dialog->setProperty("brickstore.dk11204.installed", true);
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
