// Copyright (C) 2004-2026 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QCoreApplication>

#include "common/config.h"

namespace {

constexpr auto Prefix = "Isaku/DK11204/";

QString key(const char *name)
{
    return QString::fromLatin1(Prefix) + QString::fromLatin1(name);
}

QVariant value(const char *name, const QVariant &fallback)
{
    return Config::inst()->value(key(name), fallback);
}

void setValue(const char *name, const QVariant &v)
{
    Config::inst()->setValue(key(name), v);
}

void migrateInt(const char *name, int oldValue, int newValue)
{
    if (value(name, oldValue).toInt() == oldValue)
        setValue(name, newValue);
}

void migrateIntEither(const char *name, int oldA, int oldB, int newValue)
{
    const int current = value(name, oldA).toInt();
    if (current == oldA || current == oldB)
        setValue(name, newValue);
}

void migrateCompactLabelDefaults()
{
    if (!qApp || value("CleanDefaultsV4Migrated", false).toBool())
        return;

    // Only replace values that still look like one of our previous defaults.
    // Anything the user actually customized is left alone.
    migrateInt("FontItemId", 39, 32);
    migrateInt("FontPrice", 39, 32);
    migrateInt("FontName", 29, 23);
    migrateInt("FontMeta", 24, 18);
    migrateInt("FontExtra", 19, 16);
    migrateInt("FontIndex", 19, 18);

    migrateInt("YName", 57, 50);
    migrateInt("YMeta", 105, 108);
    migrateIntEither("XIndex", 12, 430, 470);
    migrateIntEither("YIndex", 132, 126, 108);
    migrateInt("IndexMaxWidth", 120, 72);

    migrateInt("YSeparator", 158, 156);
    migrateInt("SeparatorWidth", 2, 1);
    migrateInt("MarginLeft", 12, 14);
    migrateInt("MarginRight", 12, 14);
    migrateInt("TopSplit", 275, 285);

    setValue("CleanDefaultsV4Migrated", true);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(migrateCompactLabelDefaults)
