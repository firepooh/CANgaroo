/*

  Copyright (c) 2016 Hubert Denkmair <hubert@denkmair.de>
  Copyright (c) 2026 Schildkroet

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
#include "api/candle_defs.h"
#include "CandleApiDriver.h"
#include "api/candle.h"

#include "core/Log.h"
#include "CandleAdapterDialog.h"
#include "CandleAdapterTable.h"
#include "CandleApiInterface.h"
#include "driver/GenericCanSetupPage.h"

#include <algorithm>
#include <cwctype>
#include <numeric>
#include <vector>
#include <QString>

#include <windows.h>
#include <cfgmgr32.h>

// Composite USB devices expose each CAN interface as a separate Windows device
// path containing "&mi_XX" (e.g. "&mi_00", "&mi_02"). Strip that segment so
// we can recognise multiple interface paths as the same physical device.
// Windows returns device paths in lowercase, so compare case-insensitively.
static std::wstring baseDevicePath(const std::wstring &path)
{
    // Normalise to lowercase so the map key is consistent regardless of capitalisation.
    std::wstring result = path;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);

    // Pass 1: strip every &mi_NNNN segment.
    // On serial-number-based composite USB devices &mi_XX appears in BOTH the
    // hardware-ID part AND the instance-ID part of the path, so a single erase
    // is not enough.  Loop until all occurrences are gone.
    const std::wstring mi_tag = L"&mi_";
    auto pos = result.find(mi_tag);
    while (pos != std::wstring::npos) {
        auto end = pos + mi_tag.size();
        while (end < result.size() && iswxdigit(result[end]))
            ++end;
        result.erase(pos, end - pos);
        pos = result.find(mi_tag, pos);
    }

    // Pass 2: for location-based instance IDs (no USB serial number) the
    // interface number is the last segment before #{GUID}, encoded as exactly
    // 4 hex digits (e.g. &0000 for MI_00, &0002 for MI_02).  Strip it so all
    // interfaces of the same physical device share the same key.
    const auto guid_pos = result.rfind(L"#{");
    if (guid_pos >= 5 && result[guid_pos - 5] == L'&') {
        bool all_hex = true;
        for (std::size_t k = guid_pos - 4; k < guid_pos; ++k) {
            if (!iswxdigit(result[k])) { all_hex = false; break; }
        }
        if (all_hex)
            result.erase(guid_pos - 5, 5);
    }

    return result;
}


// Map known VID/PID combinations to human-readable product names.
// The Windows device path is lower-case and contains "vid_XXXX&pid_XXXX".
static QString productNameFromPath(const std::wstring &path)
{
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    auto extract = [&](const wchar_t *tag) -> uint16_t {
        auto pos = lower.find(tag);
        if (pos == std::wstring::npos)
            return 0;
        return static_cast<uint16_t>(wcstoul(lower.c_str() + pos + wcslen(tag), nullptr, 16));
    };

    const uint16_t vid = extract(L"vid_");
    const uint16_t pid = extract(L"pid_");

    if (vid == 0x1209 && pid == 0xCA01) return QStringLiteral("CANnectivity");

    return QStringLiteral("candle");
}

// USB serial of the adapter behind a Candlelight device path, empty if it has none.
// "\\?\usb#vid_1d50&pid_606f&mi_00#7&1730e3ef&0&0000#{guid}" is the interface
// USB\VID_1D50&PID_606F&MI_00\7&1730E3EF&0&0000 of a composite device; its parent
// USB\VID_1D50&PID_606F\<serial> is named after the serial.
static QString usbSerialFromPath(const std::wstring &path)
{
    std::wstring id = path;
    if (id.rfind(L"\\\\?\\", 0) == 0) { id.erase(0, 4); }
    const auto guid = id.rfind(L"#{");
    if (guid != std::wstring::npos) { id.erase(guid); }
    std::replace(id.begin(), id.end(), L'#', L'\\');

    DEVINST inst = 0;
    if (CM_Locate_DevNodeW(&inst, id.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) { return QString(); }

    std::wstring lower = id;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.find(L"&mi_") != std::wstring::npos && CM_Get_Parent(&inst, inst, 0) != CR_SUCCESS) { return QString(); }

    wchar_t buf[MAX_DEVICE_ID_LEN] = {};
    if (CM_Get_Device_IDW(inst, buf, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) { return QString(); }
    const QString serial = QString::fromWCharArray(buf).section('\\', -1);
    // Without a serial Windows makes up a location-based ID such as 5&2a3b4c&0&3
    return serial.contains('&') ? QString() : serial;
}

CandleApiDriver::CandleApiDriver(Backend &backend)
  : CanDriver(backend),
    setupPage(new GenericCanSetupPage(0))
{
    candle_log_fn = nullptr;
    candle_log_verbose = false;
    QObject::connect(&backend, &Backend::onSetupDialogCreated, setupPage, &GenericCanSetupPage::onSetupDialogCreated);
}

QString CandleApiDriver::getName() const
{
    return "CandleAPI";
}

bool CandleApiDriver::update()
{
    // Destroy existing interfaces first, then release shared devices.
    // Interfaces hold shared_ptr refs to the devices, so deleting them first
    // ensures the devices are freed in the correct order.
    deleteAllInterfaces();
    _devices.clear();

    candle_list_handle clist;
    if (!candle_list_scan(&clist)) {
        return true;
    }

    uint8_t num_devices = 0;
    if (!candle_list_length(clist, &num_devices)) {
        candle_list_free(clist);
        return true;
    }

    // All adapters are collected first; their device numbers (the N in
    // candle<N>_ch<M>) come from the adapter table and the USB serials below.
    struct FoundDevice {
        candle_handle handle;
        std::wstring path;
        std::wstring baseKey;
        uint8_t channels;
        QString serial;
    };
    std::vector<FoundDevice> found;

    for (uint8_t i = 0; i < num_devices; i++) {
        candle_handle dev;
        if (!candle_dev_get(clist, i, &dev)) {
            log_error(QStringLiteral("CandleAPI: candle_dev_get failed for index %1").arg(static_cast<int>(i)));
            continue;
        }

        // Open temporarily to read channel count and per-channel capabilities.
        if (!candle_dev_open(dev)) {
            log_error(QStringLiteral("CandleAPI: discovery open failed for index %1").arg(static_cast<int>(i)));
            candle_dev_free(dev);
            continue;
        }

        uint8_t num_channels = 0;
        if (!candle_channel_count(dev, &num_channels) || num_channels == 0) {
            candle_dev_close(dev);
            candle_dev_free(dev);
            continue;
        }

        const std::wstring devPath(candle_dev_get_path(dev));
        const std::wstring baseKey = baseDevicePath(devPath);

        // Skip additional USB interfaces of a device we already processed.
        // On composite devices each CAN channel has its own interface path
        // (MI_00, MI_02, …) but they all share the same USB endpoint and
        // the firmware reports the total icount from any interface.
        if (std::any_of(found.begin(), found.end(), [&](const FoundDevice &f) { return f.baseKey == baseKey; })) {
            candle_dev_close(dev);
            candle_dev_free(dev);
            continue;
        }

        // Create a single shared handle for all channels on this device.
        // candle_dev_get() allocates a fresh candle_device_t; we pre-populate
        // its capability fields so getAvailableBitrates() works before open().
        candle_handle shared_handle;
        candle_dev_get(clist, i, &shared_handle);
        {
            candle_device_t *src = static_cast<candle_device_t*>(dev);
            candle_device_t *dst = static_cast<candle_device_t*>(shared_handle);
            wcscpy(dst->path, src->path);
            dst->state   = src->state;
            dst->dconf   = src->dconf;
            dst->bt_const = src->bt_const;
            memcpy(dst->ch_caps, src->ch_caps, sizeof(src->ch_caps));
        }

        candle_dev_close(dev);
        candle_dev_free(dev);

        found.push_back({ shared_handle, devPath, baseKey, num_channels, usbSerialFromPath(devPath) });
    }

    // Fixed numbers from the adapter table, the rest in USB serial order, so the
    // names do not depend on the order Windows lists the devices in
    QString tableError;
    const QList<CandleAdapterEntry> table = CandleAdapterTable::load(&tableError);
    if (!tableError.isEmpty()) {
        log_warning(QStringLiteral("CandleAPI: adapter table ignored: %1").arg(tableError));
    }
    QStringList serials;
    for (const FoundDevice &f : found) {
        serials.append(f.serial);
    }
    const QList<int> numbers = CandleAdapterTable::assignNumbers(serials, table);

    std::vector<qsizetype> order(found.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) { return numbers[a] < numbers[b]; });

    for (qsizetype idx : order) {
        const FoundDevice &f = found[static_cast<size_t>(idx)];
        auto sharedDev = std::make_shared<CandleSharedDevice>();
        sharedDev->handle = f.handle;
        sharedDev->productName = productNameFromPath(f.path);
        sharedDev->deviceIndex = numbers[idx];
        sharedDev->serial = f.serial;
        sharedDev->swVersion = static_cast<candle_device_t*>(f.handle)->dconf.sw_version;
        if (const CandleAdapterEntry *e = CandleAdapterTable::find(table, f.serial)) {
            sharedDev->alias = e->alias;
        }
        _devices[f.baseKey] = sharedDev;

        // One BusInterface per channel, all sharing the same physical device.
        for (uint8_t ch = 0; ch < f.channels; ch++) {
            addInterface(new CandleApiInterface(this, sharedDev, ch));
        }
    }

    candle_list_free(clist);
    return true;
}

bool CandleApiDriver::assignAdapters(QWidget *parent)
{
    CandleAdapterDialog dlg(*this, parent);
    return dlg.exec() == QDialog::Accepted;
}

QList<CandleApiDriver::Adapter> CandleApiDriver::adapters() const
{
    QList<Adapter> result;
    for (const auto &[key, dev] : _devices) {
        Adapter a;
        a.serial = dev->serial;
        a.number = dev->deviceIndex;
        a.alias = dev->alias;
        for (BusInterface *intf : getInterfaces()) {
            auto *candle = dynamic_cast<CandleApiInterface *>(intf);
            if (candle && candle->sharesDevice(dev)) {
                a.interfaces.append(intf);
            }
        }
        result.append(a);
    }
    std::sort(result.begin(), result.end(), [](const Adapter &a, const Adapter &b) { return a.number < b.number; });
    return result;
}
