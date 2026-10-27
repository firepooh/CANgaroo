/*
  Copyright (c) 2026 firepooh

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "CandleAdapterTable.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

QString CandleAdapterTable::path()
{
    const QString env = qEnvironmentVariable("CANGAROO_ADAPTERS");
    if (!env.isEmpty()) { return env; }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/cangaroo/candle_adapters.json");
}

QString CandleAdapterTable::key(const QString &serial)
{
    return serial.trimmed().left(16).toUpper();
}

QList<CandleAdapterEntry> CandleAdapterTable::load(QString *error)
{
    QList<CandleAdapterEntry> entries;
    QFile file(path());
    if (!file.exists()) { return entries; }
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error) { *error = QStringLiteral("cannot read %1: %2").arg(file.fileName(), file.errorString()); }
        return entries;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull())
    {
        if (error) { *error = QStringLiteral("%1: %2").arg(file.fileName(), parseError.errorString()); }
        return entries;
    }

    for (const QJsonValue &v : doc.object().value("adapters").toArray())
    {
        const QJsonObject o = v.toObject();
        CandleAdapterEntry e;
        e.serial = o.value("serial").toString().trimmed();
        e.number = o.value("number").toInt(-1);
        e.alias = o.value("alias").toString().trimmed();
        if (!e.serial.isEmpty()) { entries.append(e); }
    }
    return entries;
}

bool CandleAdapterTable::save(const QList<CandleAdapterEntry> &entries, QString *error)
{
    QJsonArray arr;
    for (const CandleAdapterEntry &e : entries)
    {
        if (e.serial.isEmpty() || (e.number < 0 && e.alias.isEmpty())) { continue; }
        QJsonObject o;
        o["serial"] = e.serial;
        o["number"] = e.number;
        o["alias"] = e.alias;
        arr.append(o);
    }
    QJsonObject root;
    root["adapters"] = arr;

    const QString file = path();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)
        || out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !out.commit())
    {
        if (error) { *error = QStringLiteral("cannot write %1: %2").arg(file, out.errorString()); }
        return false;
    }
    return true;
}

const CandleAdapterEntry *CandleAdapterTable::find(const QList<CandleAdapterEntry> &entries, const QString &serial)
{
    const QString k = key(serial);
    if (k.isEmpty()) { return nullptr; }
    for (const CandleAdapterEntry &e : entries)
    {
        if (key(e.serial) == k) { return &e; }
    }
    return nullptr;
}

QList<int> CandleAdapterTable::assignNumbers(const QStringList &serials, const QList<CandleAdapterEntry> &entries)
{
    QList<int> numbers(serials.size(), -1);

    QSet<int> reserved;
    for (const CandleAdapterEntry &e : entries)
    {
        if (e.number >= 0) { reserved.insert(e.number); }
    }

    QSet<int> used;
    for (int i = 0; i < serials.size(); ++i)
    {
        const CandleAdapterEntry *e = find(entries, serials[i]);
        if (e && e->number >= 0 && !used.contains(e->number))
        {
            numbers[i] = e->number;
            used.insert(e->number);
        }
    }

    QList<int> rest;
    for (int i = 0; i < serials.size(); ++i)
    {
        if (numbers[i] < 0) { rest.append(i); }
    }
    std::stable_sort(rest.begin(), rest.end(), [&](int a, int b)
    {
        const QString ka = key(serials[a]);
        const QString kb = key(serials[b]);
        if (ka.isEmpty() != kb.isEmpty()) { return kb.isEmpty(); }
        return ka < kb;
    });

    int next = 0;
    for (int i : rest)
    {
        while (reserved.contains(next) || used.contains(next)) { ++next; }
        numbers[i] = next;
        used.insert(next);
    }
    return numbers;
}
