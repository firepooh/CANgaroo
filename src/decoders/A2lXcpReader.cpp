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

#include "A2lXcpReader.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace A2lXcpReader
{

namespace
{

// Splits a line into whitespace-separated tokens, skipping /* block */ comments
// (which may span lines, hence inComment) and // line comments
QStringList tokenize(const QString &line, bool &inComment)
{
    QStringList tokens;
    QString current;
    auto flush = [&]()
    {
        if (!current.isEmpty()) {
            tokens << current;
            current.clear();
        }
    };

    qsizetype i = 0;
    while (i < line.size()) {
        if (inComment) {
            const qsizetype end = line.indexOf(QLatin1String("*/"), i);
            if (end < 0) { return tokens; }
            inComment = false;
            i = end + 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('*')) {
            flush();
            inComment = true;
            i += 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('/')) {
            break;
        }
        if (line[i].isSpace()) {
            flush();
            ++i;
            continue;
        }
        current += line[i++];
    }
    flush();
    return tokens;
}

std::optional<uint32_t> parseId(const QString &token)
{
    bool ok = false;
    const uint32_t v = token.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
        ? token.mid(2).toUInt(&ok, 16) : token.toUInt(&ok, 10);
    return ok ? std::optional<uint32_t>(v) : std::nullopt;
}

} // namespace

A2lXcpIds parse(QTextStream &in)
{
    A2lXcpIds ids;
    bool inBlock = false;
    bool inComment = false;
    QString pendingKey;
    QString pendingBegin;

    while (!in.atEnd()) {
        const QString raw = in.readLine();
        for (const QString &token : tokenize(raw, inComment)) {
            if (!pendingBegin.isEmpty()) {
                // "/begin XCP_ON_CAN" opens the block we care about
                if (token.compare(QLatin1String("XCP_ON_CAN"), Qt::CaseInsensitive) == 0) {
                    inBlock = pendingBegin == QLatin1String("/begin");
                }
                pendingBegin.clear();
                // The block is finished and we have what we came for
                if (!inBlock && ids.master) { return ids; }
                continue;
            }

            if (token == QLatin1String("/begin") || token == QLatin1String("/end")) {
                pendingBegin = token;
                continue;
            }
            if (!inBlock) { continue; }

            if (!pendingKey.isEmpty()) {
                if (const auto value = parseId(token)) {
                    const bool ext = (*value & extended_flag) != 0;
                    const uint32_t id = ext ? (*value & 0x1FFFFFFF) : *value;
                    ids.extended |= ext;
                    if (pendingKey == QLatin1String("CAN_ID_MASTER")) { ids.master = id; }
                    else if (pendingKey == QLatin1String("CAN_ID_SLAVE")) { ids.slave = id; }
                    else { ids.broadcast = id; }
                }
                pendingKey.clear();
                continue;
            }

            if (token == QLatin1String("CAN_ID_MASTER") || token == QLatin1String("CAN_ID_SLAVE")
                || token == QLatin1String("CAN_ID_BROADCAST")) {
                pendingKey = token;
            }
        }
    }
    return ids;
}

A2lXcpIds read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) { *error = file.errorString(); }
        return {};
    }
    QTextStream in(&file);
    // A2L files are usually Latin-1; the keywords and numbers we read are ASCII anyway
    in.setEncoding(QStringConverter::Latin1);
    return parse(in);
}

} // namespace A2lXcpReader
