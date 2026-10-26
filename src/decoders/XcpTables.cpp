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

#include "XcpTables.h"

#include <QStringList>

namespace XcpTables
{

namespace
{

QString hex2(uint8_t v)
{
    return QStringLiteral("0x") + QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace

QString command(uint8_t pid)
{
    switch (pid) {
        case 0xFF: return QStringLiteral("CONNECT");
        case 0xFE: return QStringLiteral("DISCONNECT");
        case 0xFD: return QStringLiteral("GET_STATUS");
        case 0xFC: return QStringLiteral("SYNCH");
        case 0xFB: return QStringLiteral("GET_COMM_MODE_INFO");
        case 0xFA: return QStringLiteral("GET_ID");
        case 0xF9: return QStringLiteral("SET_REQUEST");
        case 0xF8: return QStringLiteral("GET_SEED");
        case 0xF7: return QStringLiteral("UNLOCK");
        case 0xF6: return QStringLiteral("SET_MTA");
        case 0xF5: return QStringLiteral("UPLOAD");
        case 0xF4: return QStringLiteral("SHORT_UPLOAD");
        case 0xF3: return QStringLiteral("BUILD_CHECKSUM");
        case 0xF2: return QStringLiteral("TRANSPORT_LAYER_CMD");
        case 0xF1: return QStringLiteral("USER_CMD");
        case 0xF0: return QStringLiteral("DOWNLOAD");
        case 0xEF: return QStringLiteral("DOWNLOAD_NEXT");
        case 0xEE: return QStringLiteral("DOWNLOAD_MAX");
        case 0xED: return QStringLiteral("SHORT_DOWNLOAD");
        case 0xEC: return QStringLiteral("MODIFY_BITS");
        case 0xEB: return QStringLiteral("SET_CAL_PAGE");
        case 0xEA: return QStringLiteral("GET_CAL_PAGE");
        case 0xE9: return QStringLiteral("GET_PAG_PROCESSOR_INFO");
        case 0xE8: return QStringLiteral("GET_SEGMENT_INFO");
        case 0xE7: return QStringLiteral("GET_PAGE_INFO");
        case 0xE6: return QStringLiteral("SET_SEGMENT_MODE");
        case 0xE5: return QStringLiteral("GET_SEGMENT_MODE");
        case 0xE4: return QStringLiteral("COPY_CAL_PAGE");
        case 0xE3: return QStringLiteral("CLEAR_DAQ_LIST");
        case 0xE2: return QStringLiteral("SET_DAQ_PTR");
        case 0xE1: return QStringLiteral("WRITE_DAQ");
        case 0xE0: return QStringLiteral("SET_DAQ_LIST_MODE");
        case 0xDF: return QStringLiteral("GET_DAQ_LIST_MODE");
        case 0xDE: return QStringLiteral("START_STOP_DAQ_LIST");
        case 0xDD: return QStringLiteral("START_STOP_SYNCH");
        case 0xDC: return QStringLiteral("GET_DAQ_CLOCK");
        case 0xDB: return QStringLiteral("READ_DAQ");
        case 0xDA: return QStringLiteral("GET_DAQ_PROCESSOR_INFO");
        case 0xD9: return QStringLiteral("GET_DAQ_RESOLUTION_INFO");
        case 0xD8: return QStringLiteral("GET_DAQ_LIST_INFO");
        case 0xD7: return QStringLiteral("GET_DAQ_EVENT_INFO");
        case 0xD6: return QStringLiteral("FREE_DAQ");
        case 0xD5: return QStringLiteral("ALLOC_DAQ");
        case 0xD4: return QStringLiteral("ALLOC_ODT");
        case 0xD3: return QStringLiteral("ALLOC_ODT_ENTRY");
        case 0xD2: return QStringLiteral("PROGRAM_START");
        case 0xD1: return QStringLiteral("PROGRAM_CLEAR");
        case 0xD0: return QStringLiteral("PROGRAM");
        case 0xCF: return QStringLiteral("PROGRAM_RESET");
        case 0xCE: return QStringLiteral("GET_PGM_PROCESSOR_INFO");
        case 0xCD: return QStringLiteral("GET_SECTOR_INFO");
        case 0xCC: return QStringLiteral("PROGRAM_PREPARE");
        case 0xCB: return QStringLiteral("PROGRAM_FORMAT");
        case 0xCA: return QStringLiteral("PROGRAM_NEXT");
        case 0xC9: return QStringLiteral("PROGRAM_MAX");
        case 0xC8: return QStringLiteral("PROGRAM_VERIFY");
        case 0xC7: return QStringLiteral("WRITE_DAQ_MULTIPLE");
        case 0xC6: return QStringLiteral("TIME_CORRELATION_PROPERTIES");
        case 0xC5: return QStringLiteral("DTO_CTR_PROPERTIES");
        case 0xC0: return QStringLiteral("LEVEL1_CMD");
        default:   return QString();
    }
}

QString level1(uint8_t sub)
{
    switch (sub) {
        case 0x00: return QStringLiteral("GET_VERSION");
        case 0x01: return QStringLiteral("SET_DAQ_PACKED_MODE");
        case 0x02: return QStringLiteral("GET_DAQ_PACKED_MODE");
        case 0xFC: return QStringLiteral("SW_DBG_OVER_XCP");
        case 0xFD: return QStringLiteral("POD_BTL");
        default:   return QStringLiteral("SUB ") + hex2(sub);
    }
}

QString transportLayer(uint8_t sub)
{
    switch (sub) {
        case 0xFF: return QStringLiteral("GET_SLAVE_ID");
        case 0xFE: return QStringLiteral("GET_DAQ_ID");
        case 0xFD: return QStringLiteral("SET_DAQ_ID");
        default:   return QStringLiteral("SUB ") + hex2(sub);
    }
}

QString error(uint8_t code)
{
    switch (code) {
        case 0x00: return QStringLiteral("ERR_CMD_SYNCH");
        case 0x10: return QStringLiteral("ERR_CMD_BUSY");
        case 0x11: return QStringLiteral("ERR_DAQ_ACTIVE");
        case 0x12: return QStringLiteral("ERR_PGM_ACTIVE");
        case 0x20: return QStringLiteral("ERR_CMD_UNKNOWN");
        case 0x21: return QStringLiteral("ERR_CMD_SYNTAX");
        case 0x22: return QStringLiteral("ERR_OUT_OF_RANGE");
        case 0x23: return QStringLiteral("ERR_WRITE_PROTECTED");
        case 0x24: return QStringLiteral("ERR_ACCESS_DENIED");
        case 0x25: return QStringLiteral("ERR_ACCESS_LOCKED");
        case 0x26: return QStringLiteral("ERR_PAGE_NOT_VALID");
        case 0x27: return QStringLiteral("ERR_MODE_NOT_VALID");
        case 0x28: return QStringLiteral("ERR_SEGMENT_NOT_VALID");
        case 0x29: return QStringLiteral("ERR_SEQUENCE");
        case 0x2A: return QStringLiteral("ERR_DAQ_CONFIG");
        case 0x30: return QStringLiteral("ERR_MEMORY_OVERFLOW");
        case 0x31: return QStringLiteral("ERR_GENERIC");
        case 0x32: return QStringLiteral("ERR_VERIFY");
        case 0x33: return QStringLiteral("ERR_RESOURCE_TEMPORARY_NOT_ACCESSIBLE");
        case 0x34: return QStringLiteral("ERR_SUBCMD_UNKNOWN");
        case 0x35: return QStringLiteral("ERR_TIMECORR_STATE_CHANGE");
        default:   return QStringLiteral("ERR ") + hex2(code);
    }
}

QString event(uint8_t code)
{
    switch (code) {
        case 0x00: return QStringLiteral("EV_RESUME_MODE");
        case 0x01: return QStringLiteral("EV_CLEAR_DAQ");
        case 0x02: return QStringLiteral("EV_STORE_DAQ");
        case 0x03: return QStringLiteral("EV_STORE_CAL");
        case 0x05: return QStringLiteral("EV_CMD_PENDING");
        case 0x06: return QStringLiteral("EV_DAQ_OVERLOAD");
        case 0x07: return QStringLiteral("EV_SESSION_TERMINATED");
        case 0x08: return QStringLiteral("EV_TIME_SYNC");
        case 0x09: return QStringLiteral("EV_STIM_TIMEOUT");
        case 0x0A: return QStringLiteral("EV_SLEEP");
        case 0x0B: return QStringLiteral("EV_WAKE_UP");
        case 0xFE: return QStringLiteral("EV_USER");
        case 0xFF: return QStringLiteral("EV_TRANSPORT");
        default:   return QStringLiteral("EV ") + hex2(code);
    }
}

QString service(uint8_t code)
{
    switch (code) {
        case 0x00: return QStringLiteral("SERV_RESET");
        case 0x01: return QStringLiteral("SERV_TEXT");
        default:   return QStringLiteral("SERV ") + hex2(code);
    }
}

QString getIdType(uint8_t type)
{
    switch (type) {
        case 0: return QStringLiteral("ASCII text");
        case 1: return QStringLiteral("ASAM-MC2 filename w/o path+ext");
        case 2: return QStringLiteral("ASAM-MC2 filename with path+ext");
        case 3: return QStringLiteral("ASAM-MC2 URL");
        case 4: return QStringLiteral("ASAM-MC2 upload");
        case 5: return QStringLiteral("ECU name");
        case 6: return QStringLiteral("ECU-ID");
        default: return QStringLiteral("type %1").arg(type);
    }
}

QString resource(uint8_t r)
{
    QStringList parts;
    if (r & 0x01) { parts << QStringLiteral("CAL/PAG"); }
    if (r & 0x04) { parts << QStringLiteral("DAQ"); }
    if (r & 0x08) { parts << QStringLiteral("STIM"); }
    if (r & 0x10) { parts << QStringLiteral("PGM"); }
    if (r & 0x40) { parts << QStringLiteral("DBG"); }
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QLatin1Char(','));
}

QString sessionStatus(uint8_t s)
{
    QStringList parts;
    if (s & 0x01) { parts << QStringLiteral("STORE_CAL_REQ"); }
    if (s & 0x04) { parts << QStringLiteral("STORE_DAQ_REQ"); }
    if (s & 0x08) { parts << QStringLiteral("CLEAR_DAQ_REQ"); }
    if (s & 0x40) { parts << QStringLiteral("DAQ_RUNNING"); }
    if (s & 0x80) { parts << QStringLiteral("RESUME"); }
    return parts.isEmpty() ? QStringLiteral("idle") : parts.join(QLatin1Char(','));
}

} // namespace XcpTables
