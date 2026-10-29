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

#include "ObdDecoder.h"

#include <iterator>

#include <QStringList>

namespace
{

constexpr uint32_t k_extendedSessionFlag = 0x80000000u;
constexpr uint32_t k_txSessionFlag       = 0x40000000u;

// Same key as UdsDecoder: per interface, direction and ID, so loopback copies and a
// second bus with the same diagnostic IDs cannot break a multi-frame response
uint64_t sessionKey(uint32_t id, bool extended, bool isRX, uint16_t interfaceId) noexcept
{
    uint32_t key32 = extended ? (id | k_extendedSessionFlag) : id;
    if (!isRX) { key32 |= k_txSessionFlag; }
    return (static_cast<uint64_t>(interfaceId) << 32) | key32;
}

uint8_t byteAt(const QByteArray &data, int i)
{
    return i < data.size() ? static_cast<uint8_t>(data.at(i)) : 0;
}

bool isObdService(uint8_t service)
{
    return service >= 0x01 && service <= 0x0A;
}

// Requests 0x01..0x0A, responses 0x41..0x4A, negative responses to 0x01..0x0A.
// A negative response needs its second byte, so a too short start is not OBD.
bool isObdStart(const QByteArray &data)
{
    if (data.isEmpty()) { return false; }
    const uint8_t sid = byteAt(data, 0);
    if (sid == 0x7F) { return data.size() >= 2 && isObdService(byteAt(data, 1)); }
    return isObdService(sid) || (sid >= 0x41 && sid <= 0x4A);
}

QString hex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

// Two upper-case hex digits; the "0x" prefix stays lower case
QString hexByte(uint32_t v)
{
    return QString::number(v & 0xFFu, 16).rightJustified(2, QChar('0')).toUpper();
}

// Up to 'decimals' places without trailing zeros: 1726, 1726.25, 0.45
QString num(double value, int decimals = 2)
{
    QString s = QString::number(value, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) { s.chop(1); }
        if (s.endsWith(QLatin1Char('.'))) { s.chop(1); }
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

QString withUnit(double value, const QString &unit, int decimals = 2)
{
    return num(value, decimals) + QLatin1Char(' ') + unit;
}

// Bit 7 of the first byte = base + 1 ... bit 0 of the fourth byte = base + 0x20
QString supportedPids(uint8_t base, const QByteArray &data)
{
    QStringList pids;
    for (int i = 0; i < 32; ++i) {
        if (byteAt(data, i / 8) & (0x80 >> (i % 8))) {
            pids << QStringLiteral("%1").arg(base + 1 + i, 2, 16, QChar('0')).toUpper();
        }
    }
    return pids.isEmpty() ? QStringLiteral("none") : pids.join(QLatin1Char(' '));
}

QString fuelSystemStatus(uint8_t v)
{
    switch (v) {
        case 0x00: return QStringLiteral("not present");
        case 0x01: return QStringLiteral("open loop, engine not warm");
        case 0x02: return QStringLiteral("closed loop");
        case 0x04: return QStringLiteral("open loop, engine load or fuel cut");
        case 0x08: return QStringLiteral("open loop, system failure");
        case 0x10: return QStringLiteral("closed loop, feedback fault");
        default:   return QStringLiteral("0x%1").arg(hexByte(v));
    }
}

QString secondaryAirStatus(uint8_t v)
{
    switch (v) {
        case 0x01: return QStringLiteral("upstream");
        case 0x02: return QStringLiteral("downstream of catalytic converter");
        case 0x04: return QStringLiteral("from outside atmosphere or off");
        case 0x08: return QStringLiteral("pump on for diagnostics");
        default:   return QStringLiteral("0x%1").arg(hexByte(v));
    }
}

QString obdStandard(uint8_t v)
{
    static const char *const names[] = {
        nullptr, "OBD-II (CARB)", "OBD (EPA)", "OBD and OBD-II", "OBD-I", "not OBD compliant",
        "EOBD", "EOBD and OBD-II", "EOBD and OBD", "EOBD, OBD and OBD-II", "JOBD", "JOBD and OBD-II",
        "JOBD and EOBD", "JOBD, EOBD and OBD-II",
    };
    if (v < std::size(names) && names[v]) { return QString::fromLatin1(names[v]); }
    return QStringLiteral("code %1").arg(v);
}

QString fuelType(uint8_t v)
{
    static const char *const names[] = {
        "not available", "gasoline", "methanol", "ethanol", "diesel", "LPG", "CNG", "propane",
        "electric", "bifuel gasoline", "bifuel methanol", "bifuel ethanol", "bifuel LPG", "bifuel CNG",
        "bifuel propane", "bifuel electricity", "bifuel electric and combustion", "hybrid gasoline",
        "hybrid ethanol", "hybrid diesel", "hybrid electric", "hybrid electric and combustion",
        "hybrid regenerative", "bifuel diesel",
    };
    if (v < std::size(names)) { return QString::fromLatin1(names[v]); }
    return QStringLiteral("code %1").arg(v);
}

QString fuelTrim(uint8_t v)
{
    return withUnit(v * 100.0 / 128.0 - 100.0, QStringLiteral("%"), 1);
}

QString percent(double v)
{
    return withUnit(v, QStringLiteral("%"), 1);
}

QString celsius(double v)
{
    return withUnit(v, QStringLiteral("°C"), 1);
}

// Printable characters only: VIN and calibration IDs are padded with 0x00 (or 0xFF)
QString ascii(const QByteArray &data)
{
    QString s;
    for (char c : data) {
        if (c >= 0x20 && c <= 0x7E) { s += QLatin1Char(c); }
    }
    return s;
}

QString nrcText(uint8_t nrc)
{
    switch (nrc) {
        case 0x10: return QStringLiteral("General Reject");
        case 0x11: return QStringLiteral("Service Not Supported");
        case 0x12: return QStringLiteral("SubFunction Not Supported");
        case 0x13: return QStringLiteral("Incorrect Message Length Or Invalid Format");
        case 0x21: return QStringLiteral("Busy Repeat Request");
        case 0x22: return QStringLiteral("Conditions Not Correct");
        case 0x31: return QStringLiteral("Request Out Of Range");
        case 0x78: return QStringLiteral("Request Correctly Received - Response Pending");
        default:   return QStringLiteral("unknown (0x%1)").arg(hexByte(nrc));
    }
}

struct PidInfo
{
    uint8_t pid;
    uint8_t length;      // data bytes after the PID; 0 = unknown here
    const char *name;
};

// SAE J1979 / ISO 15031-5 service 01 PIDs
constexpr PidInfo k_pids[] = {
    { 0x00, 4, "Supported PIDs [01-20]" },
    { 0x01, 4, "Monitor status since DTCs cleared" },
    { 0x02, 2, "Freeze frame DTC" },
    { 0x03, 2, "Fuel system status" },
    { 0x04, 1, "Calculated engine load" },
    { 0x05, 1, "Engine coolant temperature" },
    { 0x06, 1, "Short term fuel trim (bank 1)" },
    { 0x07, 1, "Long term fuel trim (bank 1)" },
    { 0x08, 1, "Short term fuel trim (bank 2)" },
    { 0x09, 1, "Long term fuel trim (bank 2)" },
    { 0x0A, 1, "Fuel pressure" },
    { 0x0B, 1, "Intake manifold absolute pressure" },
    { 0x0C, 2, "Engine speed" },
    { 0x0D, 1, "Vehicle speed" },
    { 0x0E, 1, "Timing advance" },
    { 0x0F, 1, "Intake air temperature" },
    { 0x10, 2, "MAF air flow rate" },
    { 0x11, 1, "Throttle position" },
    { 0x12, 1, "Commanded secondary air status" },
    { 0x13, 1, "Oxygen sensors present (2 banks)" },
    { 0x14, 2, "Oxygen sensor 1 (bank 1)" },
    { 0x15, 2, "Oxygen sensor 2 (bank 1)" },
    { 0x16, 2, "Oxygen sensor 3 (bank 1)" },
    { 0x17, 2, "Oxygen sensor 4 (bank 1)" },
    { 0x18, 2, "Oxygen sensor 1 (bank 2)" },
    { 0x19, 2, "Oxygen sensor 2 (bank 2)" },
    { 0x1A, 2, "Oxygen sensor 3 (bank 2)" },
    { 0x1B, 2, "Oxygen sensor 4 (bank 2)" },
    { 0x1C, 1, "OBD standard" },
    { 0x1D, 1, "Oxygen sensors present (4 banks)" },
    { 0x1E, 1, "Auxiliary input status" },
    { 0x1F, 2, "Run time since engine start" },
    { 0x20, 4, "Supported PIDs [21-40]" },
    { 0x21, 2, "Distance traveled with MIL on" },
    { 0x22, 2, "Fuel rail pressure (relative to manifold vacuum)" },
    { 0x23, 2, "Fuel rail gauge pressure" },
    { 0x24, 4, "Oxygen sensor 1 (ratio, voltage)" },
    { 0x25, 4, "Oxygen sensor 2 (ratio, voltage)" },
    { 0x26, 4, "Oxygen sensor 3 (ratio, voltage)" },
    { 0x27, 4, "Oxygen sensor 4 (ratio, voltage)" },
    { 0x28, 4, "Oxygen sensor 5 (ratio, voltage)" },
    { 0x29, 4, "Oxygen sensor 6 (ratio, voltage)" },
    { 0x2A, 4, "Oxygen sensor 7 (ratio, voltage)" },
    { 0x2B, 4, "Oxygen sensor 8 (ratio, voltage)" },
    { 0x2C, 1, "Commanded EGR" },
    { 0x2D, 1, "EGR error" },
    { 0x2E, 1, "Commanded evaporative purge" },
    { 0x2F, 1, "Fuel tank level input" },
    { 0x30, 1, "Warm-ups since codes cleared" },
    { 0x31, 2, "Distance traveled since codes cleared" },
    { 0x32, 2, "Evap system vapor pressure" },
    { 0x33, 1, "Absolute barometric pressure" },
    { 0x34, 4, "Oxygen sensor 1 (ratio, current)" },
    { 0x35, 4, "Oxygen sensor 2 (ratio, current)" },
    { 0x36, 4, "Oxygen sensor 3 (ratio, current)" },
    { 0x37, 4, "Oxygen sensor 4 (ratio, current)" },
    { 0x38, 4, "Oxygen sensor 5 (ratio, current)" },
    { 0x39, 4, "Oxygen sensor 6 (ratio, current)" },
    { 0x3A, 4, "Oxygen sensor 7 (ratio, current)" },
    { 0x3B, 4, "Oxygen sensor 8 (ratio, current)" },
    { 0x3C, 2, "Catalyst temperature (bank 1, sensor 1)" },
    { 0x3D, 2, "Catalyst temperature (bank 2, sensor 1)" },
    { 0x3E, 2, "Catalyst temperature (bank 1, sensor 2)" },
    { 0x3F, 2, "Catalyst temperature (bank 2, sensor 2)" },
    { 0x40, 4, "Supported PIDs [41-60]" },
    { 0x41, 4, "Monitor status this drive cycle" },
    { 0x42, 2, "Control module voltage" },
    { 0x43, 2, "Absolute load value" },
    { 0x44, 2, "Commanded air-fuel equivalence ratio" },
    { 0x45, 1, "Relative throttle position" },
    { 0x46, 1, "Ambient air temperature" },
    { 0x47, 1, "Absolute throttle position B" },
    { 0x48, 1, "Absolute throttle position C" },
    { 0x49, 1, "Accelerator pedal position D" },
    { 0x4A, 1, "Accelerator pedal position E" },
    { 0x4B, 1, "Accelerator pedal position F" },
    { 0x4C, 1, "Commanded throttle actuator" },
    { 0x4D, 2, "Time run with MIL on" },
    { 0x4E, 2, "Time since trouble codes cleared" },
    { 0x4F, 4, "Maximum values (ratio, O2 voltage, O2 current, intake pressure)" },
    { 0x50, 4, "Maximum MAF air flow rate" },
    { 0x51, 1, "Fuel type" },
    { 0x52, 1, "Ethanol fuel" },
    { 0x53, 2, "Absolute evap system vapor pressure" },
    { 0x54, 2, "Evap system vapor pressure (wide range)" },
    { 0x55, 2, "Short term secondary O2 trim (bank 1, bank 3)" },
    { 0x56, 2, "Long term secondary O2 trim (bank 1, bank 3)" },
    { 0x57, 2, "Short term secondary O2 trim (bank 2, bank 4)" },
    { 0x58, 2, "Long term secondary O2 trim (bank 2, bank 4)" },
    { 0x59, 2, "Fuel rail absolute pressure" },
    { 0x5A, 1, "Relative accelerator pedal position" },
    { 0x5B, 1, "Hybrid battery pack remaining life" },
    { 0x5C, 1, "Engine oil temperature" },
    { 0x5D, 2, "Fuel injection timing" },
    { 0x5E, 2, "Engine fuel rate" },
    { 0x5F, 1, "Emission requirements" },
    { 0x60, 4, "Supported PIDs [61-80]" },
    { 0x61, 1, "Driver's demand engine percent torque" },
    { 0x62, 1, "Actual engine percent torque" },
    { 0x63, 2, "Engine reference torque" },
    { 0x64, 5, "Engine percent torque data" },
    { 0x65, 2, "Auxiliary input / output supported" },
    // Names only: the value is shown as bytes, and a request that asks for one of
    // these together with other PIDs is split only up to here
    { 0x66, 0, "Mass air flow sensor" },
    { 0x67, 0, "Engine coolant temperature (sensors)" },
    { 0x68, 0, "Intake air temperature (sensors)" },
    { 0x69, 0, "Commanded EGR and EGR error" },
    { 0x6A, 0, "Commanded diesel intake air flow" },
    { 0x6B, 0, "EGR temperature" },
    { 0x6C, 0, "Commanded throttle actuator control" },
    { 0x6D, 0, "Fuel pressure control system" },
    { 0x6E, 0, "Injection pressure control system" },
    { 0x6F, 0, "Turbocharger compressor inlet pressure" },
    { 0x70, 0, "Boost pressure control" },
    { 0x71, 0, "Variable geometry turbo control" },
    { 0x72, 0, "Wastegate control" },
    { 0x73, 0, "Exhaust pressure" },
    { 0x74, 0, "Turbocharger speed" },
    { 0x75, 0, "Turbocharger temperature (A)" },
    { 0x76, 0, "Turbocharger temperature (B)" },
    { 0x77, 0, "Charge air cooler temperature" },
    { 0x78, 0, "Exhaust gas temperature (bank 1)" },
    { 0x79, 0, "Exhaust gas temperature (bank 2)" },
    { 0x7A, 0, "Diesel particulate filter (bank 1)" },
    { 0x7B, 0, "Diesel particulate filter (bank 2)" },
    { 0x7C, 0, "Diesel particulate filter temperature" },
    { 0x7D, 0, "NOx NTE control area status" },
    { 0x7E, 0, "PM NTE control area status" },
    { 0x7F, 0, "Engine run time" },
    { 0x80, 4, "Supported PIDs [81-A0]" },
    { 0xA0, 4, "Supported PIDs [A1-C0]" },
    { 0xA6, 4, "Odometer" },
    { 0xC0, 4, "Supported PIDs [C1-E0]" },
    { 0xE0, 4, "Supported PIDs [E1-FF]" },
};

const PidInfo *findPid(uint8_t pid)
{
    for (const PidInfo &p : k_pids) {
        if (p.pid == pid) { return &p; }
    }
    return nullptr;
}

QString pidLabel(uint8_t pid)
{
    return QStringLiteral("%1 %2").arg(pid, 2, 16, QChar('0')).toUpper().arg(ObdDecoder::pidName(pid));
}

// Service 0x01 response: PID, data, PID, data, ... (a request may ask for up to six)
void describeCurrentData(const QByteArray &p, ProtocolMessage &msg)
{
    QStringList names;
    QStringList values;
    QVariantMap items;
    int i = 1;
    while (i < p.size()) {
        const uint8_t pid = byteAt(p, i);
        int length = ObdDecoder::pidDataLength(pid);
        if (length == 0 || i + 1 + length > p.size()) {
            length = static_cast<int>(p.size()) - i - 1;     // unknown or short: the rest
        }
        const QString value = ObdDecoder::pidValue(pid, p.mid(i + 1, length));
        names << ObdDecoder::pidName(pid);
        values << value;
        items.insert(pidLabel(pid), value);
        i += 1 + length;
    }
    msg.name = names.join(QStringLiteral(", "));
    if (values.size() == 1) {
        msg.description = values.first();
    } else {
        QStringList parts;
        for (int k = 0; k < values.size(); ++k) { parts << names[k] + QStringLiteral(": ") + values[k]; }
        msg.description = parts.join(QStringLiteral("; "));
        msg.metadata = items;
    }
}

// Services 0x03 / 0x07 / 0x0A on CAN: count, then two bytes per DTC
void describeDtcs(const QByteArray &p, ProtocolMessage &msg)
{
    QStringList dtcs;
    for (int i = 2; i + 1 < p.size(); i += 2) {
        const uint8_t a = byteAt(p, i);
        const uint8_t b = byteAt(p, i + 1);
        if (a == 0 && b == 0) { continue; }                 // padding
        dtcs << ObdDecoder::formatDtc(a, b);
    }
    msg.description = dtcs.isEmpty() ? QStringLiteral("no DTCs")
                                     : QStringLiteral("%1 DTC(s): %2").arg(dtcs.size()).arg(dtcs.join(QStringLiteral(", ")));
}

// Service 0x06 response: MID, TID, unit and scaling ID, value, min, max (9 bytes per test)
void describeMonitoring(const QByteArray &p, ProtocolMessage &msg)
{
    const uint8_t mid = byteAt(p, 1);
    if (mid % 0x20 == 0) {
        msg.description = QStringLiteral("supported MIDs: ") + supportedPids(mid, p.mid(2));
        return;
    }
    QStringList tests;
    for (int i = 1; i + 9 <= p.size(); i += 9) {
        const uint8_t tid = byteAt(p, i + 1);
        const int value = (byteAt(p, i + 3) << 8) | byteAt(p, i + 4);
        const int min = (byteAt(p, i + 5) << 8) | byteAt(p, i + 6);
        const int max = (byteAt(p, i + 7) << 8) | byteAt(p, i + 8);
        const bool pass = value >= min && value <= max;
        const QString text = QStringLiteral("%1 (min %2, max %3, raw) %4")
                                 .arg(value).arg(min).arg(max).arg(pass ? QStringLiteral("pass") : QStringLiteral("FAIL"));
        tests << QStringLiteral("TID %1: %2").arg(tid, 2, 16, QChar('0')).toUpper().arg(text);
        msg.metadata.insert(QStringLiteral("TID %1").arg(tid, 2, 16, QChar('0')).toUpper(), text);
    }
    msg.description = tests.isEmpty() ? hex(p.mid(2)) : tests.join(QStringLiteral("; "));
}

// Service 0x09 response: PID, then for the data PIDs the number of data items
void describeVehicleInfo(const QByteArray &p, ProtocolMessage &msg)
{
    const uint8_t pid = byteAt(p, 1);
    const QByteArray data = p.mid(2);
    if (pid % 0x20 == 0) {
        msg.description = QStringLiteral("supported: ") + supportedPids(pid, data);
        return;
    }
    switch (pid) {
        case 0x01: case 0x03: case 0x05: case 0x07: case 0x09:
            msg.description = QStringLiteral("%1 message(s)").arg(byteAt(data, 0));
            return;
        case 0x02:      // VIN
        case 0x0A:      // ECU name
        case 0x0D:      // engine serial number
        case 0x0F:      // exhaust regulation or type approval number
            msg.description = ascii(data.mid(1));
            return;
        case 0x04: {    // calibration IDs, 16 bytes each
            QStringList ids;
            for (int i = 1; i < data.size(); i += 16) { ids << ascii(data.mid(i, 16)); }
            msg.description = ids.join(QStringLiteral(", "));
            return;
        }
        case 0x06: {    // calibration verification numbers, 4 bytes each
            QStringList cvns;
            for (int i = 1; i + 4 <= data.size(); i += 4) { cvns << QString::fromLatin1(data.mid(i, 4).toHex()).toUpper(); }
            msg.description = cvns.join(QStringLiteral(", "));
            return;
        }
        case 0x08: case 0x0B: {     // in-use performance counters, 2 bytes each
            QStringList counters;
            for (int i = 1; i + 2 <= data.size(); i += 2) { counters << QString::number((byteAt(data, i) << 8) | byteAt(data, i + 1)); }
            msg.description = counters.join(QLatin1Char(' '));
            return;
        }
        default:
            msg.description = hex(data);
            return;
    }
}

} // namespace

bool ObdDecoder::isObdId(uint32_t id, bool extended)
{
    if (!extended) {
        return id == 0x7DF || (id >= 0x7E0 && id <= 0x7EF);
    }
    // 29-bit: functional 0x18DB33F1, physical request 0x18DAxxF1, response 0x18DAF1xx
    return id == 0x18DB33F1u
        || (id & 0xFFFF00FFu) == 0x18DA00F1u
        || (id & 0xFFFFFF00u) == 0x18DAF100u;
}

QString ObdDecoder::serviceName(uint8_t service)
{
    switch (service) {
        case 0x01: return QStringLiteral("Current data");
        case 0x02: return QStringLiteral("Freeze frame data");
        case 0x03: return QStringLiteral("Stored DTCs");
        case 0x04: return QStringLiteral("Clear DTCs");
        case 0x05: return QStringLiteral("Oxygen sensor monitoring");
        case 0x06: return QStringLiteral("On-board monitoring");
        case 0x07: return QStringLiteral("Pending DTCs");
        case 0x08: return QStringLiteral("Control of on-board system");
        case 0x09: return QStringLiteral("Vehicle information");
        case 0x0A: return QStringLiteral("Permanent DTCs");
        default:   return QStringLiteral("Service 0x%1").arg(hexByte(service));
    }
}

QString ObdDecoder::pidName(uint8_t pid)
{
    const PidInfo *p = findPid(pid);
    return p ? QString::fromLatin1(p->name) : QStringLiteral("PID 0x%1").arg(pid, 2, 16, QChar('0')).toUpper();
}

QString ObdDecoder::vehicleInfoName(uint8_t pid)
{
    switch (pid) {
        case 0x00: return QStringLiteral("Supported PIDs [01-20]");
        case 0x01: return QStringLiteral("VIN message count");
        case 0x02: return QStringLiteral("VIN");
        case 0x03: return QStringLiteral("Calibration ID message count");
        case 0x04: return QStringLiteral("Calibration ID");
        case 0x05: return QStringLiteral("CVN message count");
        case 0x06: return QStringLiteral("Calibration verification numbers");
        case 0x07: return QStringLiteral("In-use performance message count");
        case 0x08: return QStringLiteral("In-use performance tracking (spark ignition)");
        case 0x09: return QStringLiteral("ECU name message count");
        case 0x0A: return QStringLiteral("ECU name");
        case 0x0B: return QStringLiteral("In-use performance tracking (compression ignition)");
        case 0x0D: return QStringLiteral("Engine serial number");
        case 0x0F: return QStringLiteral("Exhaust regulation or type approval number");
        default:   return QStringLiteral("Vehicle information PID 0x%1").arg(pid, 2, 16, QChar('0')).toUpper();
    }
}

int ObdDecoder::pidDataLength(uint8_t pid)
{
    const PidInfo *p = findPid(pid);
    return p ? p->length : 0;
}

QString ObdDecoder::formatDtc(uint8_t a, uint8_t b)
{
    // Bits 7..6 of the first byte: P, C, B, U; then four hex digits
    static const char letters[] = { 'P', 'C', 'B', 'U' };
    return QStringLiteral("%1%2%3%4")
        .arg(QLatin1Char(letters[a >> 6]))
        .arg((a >> 4) & 0x3)
        .arg(a & 0x0F, 1, 16)
        .arg(b, 2, 16, QChar('0'))
        .toUpper();
}

QString ObdDecoder::pidValue(uint8_t pid, const QByteArray &data)
{
    const int need = pidDataLength(pid);
    if (need > 0 && data.size() < need) {
        return QStringLiteral("incomplete: ") + hex(data);
    }
    const int A = byteAt(data, 0);
    const int B = byteAt(data, 1);
    const int C = byteAt(data, 2);
    const int D = byteAt(data, 3);
    const int AB = A * 256 + B;
    const int CD = C * 256 + D;

    switch (pid) {
        case 0x00: case 0x20: case 0x40: case 0x60: case 0x80: case 0xA0: case 0xC0: case 0xE0:
            return supportedPids(pid, data);
        case 0x01:
            return QStringLiteral("MIL %1, %2 DTC(s)").arg((A & 0x80) ? QStringLiteral("on") : QStringLiteral("off")).arg(A & 0x7F);
        case 0x02:
            return (A == 0 && B == 0) ? QStringLiteral("no DTC") : formatDtc(static_cast<uint8_t>(A), static_cast<uint8_t>(B));
        case 0x03:
            return B ? fuelSystemStatus(static_cast<uint8_t>(A)) + QStringLiteral(" / ") + fuelSystemStatus(static_cast<uint8_t>(B))
                     : fuelSystemStatus(static_cast<uint8_t>(A));
        case 0x04: case 0x11: case 0x2C: case 0x2E: case 0x2F: case 0x45: case 0x47: case 0x48:
        case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x52: case 0x5A: case 0x5B:
            return percent(A * 100.0 / 255.0);
        case 0x05: case 0x0F: case 0x46: case 0x5C:
            return celsius(A - 40);
        case 0x06: case 0x07: case 0x08: case 0x09: case 0x2D:
            return fuelTrim(static_cast<uint8_t>(A));
        case 0x0A:
            return withUnit(3 * A, QStringLiteral("kPa"));
        case 0x0B: case 0x33:
            return withUnit(A, QStringLiteral("kPa"));
        case 0x0C:
            return withUnit(AB / 4.0, QStringLiteral("rpm"));
        case 0x0D:
            return withUnit(A, QStringLiteral("km/h"));
        case 0x0E:
            return withUnit(A / 2.0 - 64.0, QStringLiteral("° before TDC"), 1);
        case 0x10:
            return withUnit(AB / 100.0, QStringLiteral("g/s"));
        case 0x12:
            return secondaryAirStatus(static_cast<uint8_t>(A));
        case 0x13: case 0x1D:
            return QStringLiteral("0x%1").arg(A, 2, 16, QChar('0')).toUpper();
        case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:
            return withUnit(A / 200.0, QStringLiteral("V"), 3) + QStringLiteral(", trim ")
                 + (B == 0xFF ? QStringLiteral("not used") : fuelTrim(static_cast<uint8_t>(B)));
        case 0x1C:
            return obdStandard(static_cast<uint8_t>(A));
        case 0x1E:
            return (A & 0x01) ? QStringLiteral("PTO active") : QStringLiteral("PTO not active");
        case 0x1F:
            return withUnit(AB, QStringLiteral("s"));
        case 0x21: case 0x31:
            return withUnit(AB, QStringLiteral("km"));
        case 0x22:
            return withUnit(AB * 0.079, QStringLiteral("kPa"), 3);
        case 0x23: case 0x59:
            return withUnit(AB * 10, QStringLiteral("kPa"));
        case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B:
            return QStringLiteral("ratio %1, %2").arg(num(AB * 2.0 / 65536.0, 3), withUnit(CD * 8.0 / 65536.0, QStringLiteral("V"), 3));
        case 0x30:
            return QString::number(A);
        case 0x32:
            return withUnit(static_cast<int16_t>(AB) / 4.0, QStringLiteral("Pa"));
        case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B:
            return QStringLiteral("ratio %1, %2").arg(num(AB * 2.0 / 65536.0, 3), withUnit(CD / 256.0 - 128.0, QStringLiteral("mA"), 3));
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            return celsius(AB / 10.0 - 40.0);
        case 0x42:
            return withUnit(AB / 1000.0, QStringLiteral("V"), 3);
        case 0x43:
            return percent(AB * 100.0 / 255.0);
        case 0x44:
            return num(AB * 2.0 / 65536.0, 3);
        case 0x4D: case 0x4E:
            return withUnit(AB, QStringLiteral("min"));
        case 0x4F:
            return QStringLiteral("ratio %1, %2, %3, %4").arg(QString::number(A), withUnit(B, QStringLiteral("V")),
                                                             withUnit(C, QStringLiteral("mA")), withUnit(D * 10, QStringLiteral("kPa")));
        case 0x50:
            return withUnit(A * 10, QStringLiteral("g/s"));
        case 0x51:
            return fuelType(static_cast<uint8_t>(A));
        case 0x53:
            return withUnit(AB / 200.0, QStringLiteral("kPa"), 3);
        case 0x54:
            return withUnit(AB - 32767, QStringLiteral("Pa"));
        case 0x55: case 0x56: case 0x57: case 0x58:
            return fuelTrim(static_cast<uint8_t>(A)) + QStringLiteral(" / ") + fuelTrim(static_cast<uint8_t>(B));
        case 0x5D:
            return withUnit(AB / 128.0 - 210.0, QStringLiteral("°"), 3);
        case 0x5E:
            return withUnit(AB / 20.0, QStringLiteral("L/h"));
        case 0x61: case 0x62:
            return percent(A - 125);
        case 0x63:
            return withUnit(AB, QStringLiteral("N·m"));
        case 0xA6:
            return withUnit(((static_cast<uint32_t>(A) << 24) | (static_cast<uint32_t>(B) << 16)
                             | (static_cast<uint32_t>(C) << 8) | static_cast<uint32_t>(D)) / 10.0, QStringLiteral("km"), 1);
        default:
            return hex(data);
    }
}

void ObdDecoder::describe(const QByteArray &p, ProtocolMessage &msg)
{
    const uint8_t sid = byteAt(p, 0);
    msg.metadata.clear();

    if (sid == 0x7F) {
        const uint8_t service = byteAt(p, 1);
        msg.type = MessageType::NegativeResponse;
        // No PID in a negative response: its own row, not the one of the service's PID 00
        msg.id = 0x7F00u | service;
        msg.name = serviceName(service);
        msg.description = p.size() >= 3 ? QStringLiteral("negative response: ") + nrcText(byteAt(p, 2))
                                        : QStringLiteral("negative response: incomplete");
        return;
    }

    const bool response = sid >= 0x41;
    const uint8_t service = response ? static_cast<uint8_t>(sid - 0x40) : sid;
    const uint8_t pid = byteAt(p, 1);
    msg.type = response ? MessageType::PositiveResponse : MessageType::Request;
    msg.id = static_cast<uint32_t>(service) << 8;
    msg.name = serviceName(service);
    msg.description = response ? QString() : QStringLiteral("request");

    switch (service) {
        case 0x01:
            msg.id |= pid;
            if (response) {
                describeCurrentData(p, msg);
            } else {
                QStringList names;
                for (int i = 1; i < p.size(); ++i) { names << pidName(byteAt(p, i)); }
                msg.name = names.join(QStringLiteral(", "));
            }
            return;
        case 0x02: {
            // Request: PID, frame number. Response: PID, frame number, data
            msg.id |= pid;
            msg.name = QStringLiteral("%1 (freeze frame %2)").arg(pidName(pid)).arg(byteAt(p, 2));
            if (response) { msg.description = pidValue(pid, p.mid(3)); }
            return;
        }
        case 0x03: case 0x07: case 0x0A:
            if (response) { describeDtcs(p, msg); }
            return;
        case 0x04:
            if (response) { msg.description = QStringLiteral("DTCs and stored values cleared"); }
            return;
        case 0x06:
            msg.id |= pid;
            msg.name += QStringLiteral(" MID ") + QStringLiteral("%1").arg(pid, 2, 16, QChar('0')).toUpper();
            if (response) { describeMonitoring(p, msg); }
            return;
        case 0x08:
            msg.id |= pid;
            msg.name += QStringLiteral(" TID ") + QStringLiteral("%1").arg(pid, 2, 16, QChar('0')).toUpper();
            if (response) { msg.description = hex(p.mid(2)); }
            return;
        case 0x09:
            msg.id |= pid;
            msg.name = vehicleInfoName(pid);
            if (response) { describeVehicleInfo(p, msg); }
            return;
        default:
            if (response) { msg.description = hex(p.mid(1)); }
            return;
    }
}

DecodeStatus ObdDecoder::tryDecode(const BusMessage &frame, ProtocolMessage &outMsg)
{
    if (frame.isErrorFrame() || frame.isRTR() || frame.getLength() < 1 || frame.busType() != BusType::CAN) {
        return DecodeStatus::Ignored;
    }
    const uint32_t id = frame.getId();
    if (!isObdId(id, frame.isExtended())) {
        return DecodeStatus::Ignored;
    }

    const uint64_t key = sessionKey(id, frame.isExtended(), frame.isRX(), frame.getInterfaceId());
    const uint8_t type = (frame.getByte(0) >> 4) & 0x0F;

    auto complete = [&](const QByteArray &payload, const QVector<BusMessage> &frames) {
        outMsg = ProtocolMessage();
        outMsg.protocol = QStringLiteral("obd2");
        outMsg.payload = payload;
        outMsg.rawFrames = frames;
        outMsg.timestamp = static_cast<uint64_t>(frames.first().getFloatTimestamp() * 1000000.0);
        describe(payload, outMsg);
        return DecodeStatus::Completed;
    };

    if (type == 0) {                                // single frame
        int size = frame.getByte(0) & 0x0F;
        int offset = 1;
        if (size == 0 && frame.getLength() > 1) {   // CAN FD single frame with a length byte
            size = frame.getByte(1);
            offset = 2;
        }
        if (size == 0 || offset + size > frame.getLength()) {
            return DecodeStatus::Ignored;
        }
        QByteArray payload;
        for (int i = 0; i < size; ++i) { payload.append(static_cast<char>(frame.getByte(offset + i))); }
        m_sessions.remove(key);
        return isObdStart(payload) ? complete(payload, { frame }) : DecodeStatus::Ignored;
    }

    if (type == 1) {                                // first frame
        int size = ((frame.getByte(0) & 0x0F) << 8) | frame.getByte(1);
        int offset = 2;
        if (size == 0 && frame.getLength() >= 6) {  // CAN FD first frame with a 32-bit length
            size = (frame.getByte(2) << 24) | (frame.getByte(3) << 16) | (frame.getByte(4) << 8) | frame.getByte(5);
            offset = 6;
        }
        QByteArray data;
        for (int i = offset; i < frame.getLength(); ++i) { data.append(static_cast<char>(frame.getByte(i))); }
        if (!isObdStart(data)) {
            m_sessions.remove(key);                 // a UDS transfer on the same ID
            return DecodeStatus::Ignored;
        }
        Session &s = m_sessions[key];
        s.data = data;
        s.expectedSize = size;
        s.nextSn = 1;
        s.frames = { frame };
        return DecodeStatus::Consumed;
    }

    if (type == 2) {                                // consecutive frame
        auto it = m_sessions.find(key);
        if (it == m_sessions.end()) {
            return DecodeStatus::Ignored;           // not ours (UDS) or the first frame was missed
        }
        Session &s = *it;
        if ((frame.getByte(0) & 0x0F) != s.nextSn) {
            m_sessions.erase(it);
            return DecodeStatus::Ignored;
        }
        s.frames.append(frame);
        for (int i = 1; i < frame.getLength() && s.data.size() < s.expectedSize; ++i) {
            s.data.append(static_cast<char>(frame.getByte(i)));
        }
        s.nextSn = (s.nextSn + 1) % 16;
        if (s.data.size() < s.expectedSize) {
            return DecodeStatus::Consumed;
        }
        const Session done = s;
        m_sessions.erase(it);
        return complete(done.data, done.frames);
    }

    return DecodeStatus::Ignored;                   // flow control: shown in the Monitor only
}

void ObdDecoder::reset()
{
    m_sessions.clear();
}
