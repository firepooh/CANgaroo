/*

  Copyright (c) 2026 jypark7

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

#include "RemoteApi.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include "core/Backend.h"
#include "core/BusTrace.h"
#include "core/Log.h"
#include "core/MeasurementInterface.h"
#include "core/MeasurementNetwork.h"
#include "core/MeasurementSetup.h"
#include "core/TraceFileFormat.h"
#include "db/model/CanDb.h"
#include "db/model/CanDbMessage.h"
#include "db/model/CanDbSignal.h"
#include "driver/BusInterface.h"
#include "driver/CanTiming.h"
#include "remote/RemoteJson.h"

namespace
{
    constexpr int k_maxFramesPerRead = 2000;
    constexpr int k_maxTimeoutMs = 600000;

    CanDbMessage *findDbMessageByName(MeasurementSetup &setup, const QString &name)
    {
        for (MeasurementNetwork *net : setup.getNetworks())
        {
            for (const pCanDb &db : std::as_const(net->_canDbs))
            {
                for (CanDbMessage *dbMsg : db->getMessageList())
                {
                    if (dbMsg->getName() == name) { return dbMsg; }
                }
            }
        }
        return nullptr;
    }

    QJsonArray capabilityNames(uint32_t caps)
    {
        static const std::pair<uint32_t, const char *> k_names[] = {
            { BusInterface::capability_canfd,                "canfd" },
            { BusInterface::capability_listen_only,          "listen_only" },
            { BusInterface::capability_triple_sampling,      "triple_sampling" },
            { BusInterface::capability_one_shot,             "one_shot" },
            { BusInterface::capability_auto_restart,         "auto_restart" },
            { BusInterface::capability_custom_bitrate,       "custom_bitrate" },
            { BusInterface::capability_custom_canfd_bitrate, "custom_canfd_bitrate" },
            { BusInterface::capability_bus_off_recovery,     "bus_off_recovery" },
            { BusInterface::capability_tdc,                  "tdc" },
        };

        QJsonArray result;
        for (const auto &[flag, name] : k_names)
        {
            if (caps & flag) { result.append(name); }
        }
        return result;
    }

    QString stateName(uint32_t state)
    {
        switch (state)
        {
            case BusInterface::state_ok:         return "ok";
            case BusInterface::state_warning:    return "warning";
            case BusInterface::state_passive:    return "passive";
            case BusInterface::state_bus_off:    return "bus_off";
            case BusInterface::state_stopped:    return "stopped";
            case BusInterface::state_tx_success: return "tx_success";
            case BusInterface::state_tx_fail:    return "tx_fail";
            default:                             return "unknown";
        }
    }

    int timeoutFrom(const QJsonObject &params, int def)
    {
        return static_cast<int>(RemoteJson::toUInt(params, "timeout_ms", k_maxTimeoutMs, def));
    }
}

RemoteApi::RemoteApi(Backend &backend, bool allowTx, int frameLogCapacity, QObject *parent)
    : QObject(parent),
      _backend(backend),
      _allowTx(allowTx),
      _frameLog(*backend.getTrace(), frameLogCapacity, this)
{
    connect(&_frameLog, &RemoteFrameLog::framesAppended, this, &RemoteApi::checkPending);
    connect(&_backend, &Backend::endMeasurement, this, &RemoteApi::stopAllPeriodic);
}

RemoteApi::~RemoteApi()
{
    stopAllPeriodic();
}

void RemoteApi::call(const QString &method, const QJsonObject &params, Reply reply)
{
    using Handler = QJsonValue (RemoteApi::*)(const QJsonObject &);
    static const std::map<QString, Handler> k_sync = {
        { "hello",               &RemoteApi::hello },
        { "list_interfaces",     &RemoteApi::listInterfaces },
        { "configure_interface", &RemoteApi::configureInterface },
        { "start_measurement",   &RemoteApi::startMeasurement },
        { "stop_measurement",    &RemoteApi::stopMeasurement },
        { "measurement_status",  &RemoteApi::measurementStatus },
        { "bus_status",          &RemoteApi::busStatus },
        { "send_frame",          &RemoteApi::sendFrame },
        { "send_periodic",       &RemoteApi::sendPeriodic },
        { "stop_periodic",       &RemoteApi::stopPeriodic },
        { "get_frames",          &RemoteApi::getFrames },
        { "frame_summary",       &RemoteApi::frameSummary },
        { "clear_trace",         &RemoteApi::clearTrace },
        { "save_trace",          &RemoteApi::saveTrace },
        { "load_dbc",            &RemoteApi::loadDbc },
        { "decode_frame",        &RemoteApi::decodeFrame },
        { "encode_frame",        &RemoteApi::encodeFrame },
    };

    try
    {
        if (method == "wait_for_frame")
        {
            waitForFrame(params, reply);
        }
        else if (method == "send_and_wait")
        {
            sendAndWait(params, reply);
        }
        else if (auto it = k_sync.find(method); it != k_sync.end())
        {
            reply((this->*(it->second))(params), nullptr);
        }
        else
        {
            throw RemoteError(RemoteError::MethodNotFound, QString("unknown method '%1'").arg(method));
        }
    }
    catch (const RemoteError &e)
    {
        reply(QJsonValue(), &e);
    }
    catch (const std::exception &e)
    {
        const RemoteError err(RemoteError::InternalError, QString::fromUtf8(e.what()));
        reply(QJsonValue(), &err);
    }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

BusInterface &RemoteApi::requireInterface(const QJsonObject &params, const QString &key)
{
    const auto id = static_cast<BusInterfaceId>(RemoteJson::toUInt(params, key, 0xFFFF));
    BusInterface *intf = _backend.getInterfaceById(id);
    if (!intf)
    {
        throw RemoteError(RemoteError::NotFound,
                          QString("interface %1 not found; see list_interfaces").arg(id));
    }
    return *intf;
}

MeasurementInterface *RemoteApi::findMeasurementInterface(BusInterfaceId id)
{
    for (MeasurementNetwork *net : _backend.getSetup().getNetworks())
    {
        for (MeasurementInterface *mi : net->interfaces())
        {
            if (mi->busInterface() == id) { return mi; }
        }
    }
    return nullptr;
}

void RemoteApi::requireTxAllowed() const
{
    if (!_allowTx)
    {
        throw RemoteError(RemoteError::TxNotAllowed,
                          "transmitting is disabled; start cangaroo with --remote-allow-tx");
    }
}

void RemoteApi::transmit(BusMessage msg)
{
    requireTxAllowed();
    if (!_backend.isMeasurementRunning())
    {
        throw RemoteError(RemoteError::NotRunning, "measurement is not running; call start_measurement first");
    }

    BusInterface *intf = _backend.getInterfaceById(msg.getInterfaceId());
    if (!intf)
    {
        throw RemoteError(RemoteError::NotFound,
                          QString("interface %1 not found").arg(msg.getInterfaceId()));
    }
    if (msg.isFD() && !(intf->getCapabilities() & BusInterface::capability_canfd))
    {
        throw RemoteError(RemoteError::InvalidParams, "interface does not support CAN FD");
    }

    msg.setRX(false);
    intf->sendMessage(msg);
}

QJsonObject RemoteApi::interfaceToJson(BusInterfaceId id)
{
    QJsonObject o;
    o["id"]     = id;
    o["name"]   = _backend.getInterfaceName(id);
    o["driver"] = _backend.getDriverName(id);

    BusInterface *intf = _backend.getInterfaceById(id);
    if (intf)
    {
        o["bus_type"]     = intf->busType() == BusType::LIN ? "LIN" : "CAN";
        o["state"]        = stateName(intf->getState());
        o["capabilities"] = capabilityNames(intf->getCapabilities());
        o["details"]      = intf->getDetailsStr();

        std::set<unsigned> bitrates, fdBitrates, samplePoints;
        for (CanTiming t : intf->getAvailableBitrates())
        {
            if (t.getBitrate()) { bitrates.insert(t.getBitrate()); }
            if (t.getBitrateFD()) { fdBitrates.insert(t.getBitrateFD()); }
            if (t.getSamplePoint()) { samplePoints.insert(t.getSamplePoint()); }
        }
        QJsonArray br, fdbr, sp;
        for (unsigned v : bitrates) { br.append(static_cast<qint64>(v)); }
        for (unsigned v : fdBitrates) { fdbr.append(static_cast<qint64>(v)); }
        for (unsigned v : samplePoints) { sp.append(static_cast<qint64>(v)); }
        o["available_bitrates"] = br;
        o["available_fd_bitrates"] = fdbr;
        o["available_sample_points"] = sp;
    }

    if (MeasurementInterface *mi = findMeasurementInterface(id))
    {
        QJsonObject cfg;
        cfg["enabled"]         = mi->isEnabled();
        cfg["bitrate"]         = static_cast<qint64>(mi->bitrate());
        cfg["sample_point"]    = mi->samplePoint();
        cfg["fd"]              = mi->isCanFD();
        cfg["fd_bitrate"]      = static_cast<qint64>(mi->fdBitrate());
        cfg["fd_sample_point"] = static_cast<qint64>(mi->fdSamplePoint());
        cfg["listen_only"]     = mi->isListenOnlyMode();
        o["config"] = cfg;
    }
    return o;
}

QJsonObject RemoteApi::decodeToJson(const BusMessage &msg)
{
    CanDbMessage *dbMsg = _backend.findDbMessage(msg);
    if (!dbMsg) { return {}; }

    QJsonObject sigs;
    for (CanDbSignal *sig : dbMsg->getSignals())
    {
        if (!sig->isPresentInMessage(msg)) { continue; }

        const uint64_t raw = sig->extractRawDataFromMessage(msg);
        QJsonObject s;
        s["value"] = sig->convertRawValueToPhysical(raw);
        s["raw"]   = static_cast<qint64>(raw);
        s["unit"]  = sig->getUnit();
        const QString valueName = sig->getValueName(raw);
        if (!valueName.isEmpty()) { s["value_name"] = valueName; }
        sigs[sig->name()] = s;
    }

    QJsonObject o;
    o["message"] = dbMsg->getName();
    o["signals"] = sigs;
    return o;
}

// ---------------------------------------------------------------------------
// pending waits (wait_for_frame, send_and_wait)
// ---------------------------------------------------------------------------

void RemoteApi::addPending(PendingWait wait)
{
    if (tryComplete(wait)) { return; }

    _pending.push_back(std::move(wait));
    PendingWait &added = _pending.back();
    added.timer = new QTimer(this);
    added.timer->setSingleShot(true);
    added.timer->setTimerType(Qt::PreciseTimer);

    QTimer *timer = added.timer;
    connect(timer, &QTimer::timeout, this, [this, timer]()
    {
        auto it = std::find_if(_pending.begin(), _pending.end(),
                               [timer](const PendingWait &w) { return w.timer == timer; });
        if (it == _pending.end()) { return; }

        // A frame may have arrived in the same event loop pass.
        if (tryComplete(*it))
        {
            timer->deleteLater();
            _pending.erase(it);
            return;
        }

        QJsonObject result;
        result["matched"]    = false;
        result["timeout_ms"] = it->timeoutMs;
        result["cursor"]     = static_cast<qint64>(_frameLog.nextSeq());
        if (it->sent) { result["sent"] = RemoteJson::messageToJson(*it->sent); }
        finishPending(it, result);
    });
    timer->start(added.timeoutMs);
}

bool RemoteApi::tryComplete(PendingWait &wait)
{
    uint64_t next = 0;
    const QList<RemoteFrame> frames = _frameLog.read(wait.since, 1, wait.filter, &next);
    if (frames.isEmpty())
    {
        wait.since = next;
        return false;
    }

    const RemoteFrame &rx = frames.first();
    QJsonObject result;
    result["matched"] = true;
    result["frame"]   = RemoteJson::frameToJson(rx);
    result["cursor"]  = static_cast<qint64>(rx.seq + 1);

    if (wait.sent)
    {
        // The device reports our own frame back as TX echo with its transmit time.
        RemoteFrameFilter echo;
        echo.interfaceId = wait.sent->getInterfaceId();
        echo.id = wait.sent->getId();
        echo.extended = wait.sent->isExtended();
        echo.rx = false;

        uint64_t unused = 0;
        const QList<RemoteFrame> tx = _frameLog.read(wait.sentSince, 1, echo, &unused);
        result["sent"] = RemoteJson::messageToJson(*wait.sent);
        if (!tx.isEmpty())
        {
            result["tx_echo"] = RemoteJson::frameToJson(tx.first());
            result["latency_us"] = static_cast<qint64>(rx.msg.getTimestamp_us() - tx.first().msg.getTimestamp_us());
        }
    }

    if (wait.reply)
    {
        Reply reply = std::move(wait.reply);
        wait.reply = nullptr;
        reply(result, nullptr);
    }
    return true;
}

void RemoteApi::finishPending(std::list<PendingWait>::iterator it, const QJsonValue &result)
{
    Reply reply = std::move(it->reply);
    if (it->timer)
    {
        it->timer->stop();
        it->timer->deleteLater();
    }
    _pending.erase(it);
    if (reply) { reply(result, nullptr); }
}

void RemoteApi::checkPending()
{
    for (auto it = _pending.begin(); it != _pending.end();)
    {
        if (tryComplete(*it))
        {
            if (it->timer)
            {
                it->timer->stop();
                it->timer->deleteLater();
            }
            it = _pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RemoteApi::cancelPending(const QObject *owner)
{
    for (auto it = _pending.begin(); it != _pending.end();)
    {
        if (it->owner == owner)
        {
            if (it->timer)
            {
                it->timer->stop();
                it->timer->deleteLater();
            }
            it = _pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RemoteApi::stopAllPeriodic()
{
    for (auto &[handle, task] : _periodic)
    {
        task.timer->stop();
        task.timer->deleteLater();
    }
    _periodic.clear();
}

// ---------------------------------------------------------------------------
// methods
// ---------------------------------------------------------------------------

QJsonValue RemoteApi::hello(const QJsonObject &)
{
    QJsonObject o;
    o["server"]          = "cangaroo";
    o["version"]         = VERSION_STRING;
    o["api_version"]     = k_apiVersion;
    o["allow_tx"]        = _allowTx;
    o["frame_log_size"]  = _frameLog.capacity();
    return o;
}

QJsonValue RemoteApi::listInterfaces(const QJsonObject &)
{
    QJsonArray result;
    for (BusInterfaceId id : _backend.getInterfaceList())
    {
        result.append(interfaceToJson(id));
    }
    return result;
}

QJsonValue RemoteApi::configureInterface(const QJsonObject &params)
{
    if (_backend.isMeasurementRunning())
    {
        throw RemoteError(RemoteError::Running, "stop the measurement before changing the configuration");
    }

    BusInterface &intf = requireInterface(params);
    MeasurementInterface *mi = findMeasurementInterface(intf.getId());
    if (!mi)
    {
        throw RemoteError(RemoteError::NotFound, "interface is not part of the measurement setup");
    }

    const uint32_t caps = intf.getCapabilities();
    if (params.contains("enabled"))      { mi->setEnabled(RemoteJson::toBool(params, "enabled")); }
    if (params.contains("bitrate"))      { mi->setBitrate(RemoteJson::toUInt(params, "bitrate", 1000000)); }
    if (params.contains("sample_point")) { mi->setSamplePoint(static_cast<int>(RemoteJson::toUInt(params, "sample_point", 999))); }
    if (params.contains("fd"))
    {
        const bool fd = RemoteJson::toBool(params, "fd");
        if (fd && !(caps & BusInterface::capability_canfd))
        {
            throw RemoteError(RemoteError::InvalidParams, "interface does not support CAN FD");
        }
        mi->setCanFD(fd);
    }
    if (params.contains("fd_bitrate"))      { mi->setFdBitrate(RemoteJson::toUInt(params, "fd_bitrate", 20000000)); }
    if (params.contains("fd_sample_point")) { mi->setFdSamplePoint(RemoteJson::toUInt(params, "fd_sample_point", 999)); }
    if (params.contains("listen_only"))
    {
        const bool listenOnly = RemoteJson::toBool(params, "listen_only");
        if (listenOnly && !(caps & BusInterface::capability_listen_only))
        {
            throw RemoteError(RemoteError::InvalidParams, "interface does not support listen-only mode");
        }
        mi->setListenOnlyMode(listenOnly);
    }

    emit _backend.onSetupChanged();
    log_info(tr("Remote: configured interface %1").arg(intf.getName()));

    // The driver only finds out about an unsupported timing when it opens the
    // channel, so check it against the advertised table here.
    QJsonArray warnings;
    bool nominalFound = false;
    bool dataFound = !mi->isCanFD();
    for (CanTiming t : intf.getAvailableBitrates())
    {
        if (t.getBitrate() == mi->bitrate() && static_cast<int>(t.getSamplePoint()) == mi->samplePoint()) { nominalFound = true; }
        if (t.getBitrateFD() == mi->fdBitrate() && t.getSamplePointFD() == mi->fdSamplePoint()) { dataFound = true; }
    }
    if (!nominalFound)
    {
        warnings.append(QString("bitrate %1 with sample point %2 is not in the interface's timing table")
                            .arg(mi->bitrate()).arg(mi->samplePoint()));
    }
    if (!dataFound)
    {
        warnings.append(QString("fd_bitrate %1 with sample point %2 is not in the interface's timing table")
                            .arg(mi->fdBitrate()).arg(mi->fdSamplePoint()));
    }

    QJsonObject result = interfaceToJson(intf.getId());
    result["warnings"] = warnings;
    return result;
}

QJsonValue RemoteApi::startMeasurement(const QJsonObject &params)
{
    if (!_backend.isMeasurementRunning())
    {
        if (RemoteJson::toBool(params, "clear_trace", false))
        {
            _backend.clearTrace();
            _frameLog.clear();
        }
        _backend.startMeasurement();
    }
    return measurementStatus({});
}

QJsonValue RemoteApi::stopMeasurement(const QJsonObject &)
{
    stopAllPeriodic();
    if (_backend.isMeasurementRunning())
    {
        _backend.stopMeasurement();
    }
    return measurementStatus({});
}

QJsonValue RemoteApi::measurementStatus(const QJsonObject &)
{
    QJsonObject o;
    o["running"]       = _backend.isMeasurementRunning();
    o["allow_tx"]      = _allowTx;
    o["trace_size"]    = static_cast<qint64>(_backend.getTrace()->size());
    o["cursor"]        = static_cast<qint64>(_frameLog.nextSeq());
    o["oldest"]        = static_cast<qint64>(_frameLog.oldestSeq());
    o["periodic_tasks"] = static_cast<int>(_periodic.size());

    QJsonArray intfs;
    for (BusInterfaceId id : _backend.getInterfaceList())
    {
        MeasurementInterface *mi = findMeasurementInterface(id);
        if (!mi || !mi->isEnabled()) { continue; }

        BusInterface *intf = _backend.getInterfaceById(id);
        QJsonObject i;
        i["id"]    = id;
        i["name"]  = _backend.getInterfaceName(id);
        i["open"]  = intf && intf->isOpen();
        i["state"] = intf ? stateName(intf->getState()) : "unknown";
        intfs.append(i);
    }
    o["interfaces"] = intfs;
    return o;
}

QJsonValue RemoteApi::busStatus(const QJsonObject &params)
{
    BusInterface &intf = requireInterface(params);
    intf.updateStatistics();

    QJsonObject o;
    o["interface"]   = intf.getId();
    o["name"]        = intf.getName();
    o["open"]        = intf.isOpen();
    o["state"]       = stateName(intf.getState());
    o["state_text"]  = intf.getStateText();
    o["rx_frames"]   = intf.getNumRxFrames();
    o["rx_errors"]   = intf.getNumRxErrors();
    o["tx_frames"]   = intf.getNumTxFrames();
    o["tx_errors"]   = intf.getNumTxErrors();
    o["rx_overruns"] = intf.getNumRxOverruns();
    o["tx_dropped"]  = intf.getNumTxDropped();
    return o;
}

QJsonValue RemoteApi::sendFrame(const QJsonObject &params)
{
    requireInterface(params);
    const BusMessage msg = RemoteJson::frameFromJson(params);
    const uint64_t cursor = _frameLog.nextSeq();
    transmit(msg);

    QJsonObject o;
    o["sent"]   = RemoteJson::messageToJson(msg);
    o["cursor"] = static_cast<qint64>(cursor);
    return o;
}

QJsonValue RemoteApi::sendPeriodic(const QJsonObject &params)
{
    requireInterface(params);
    const BusMessage msg = RemoteJson::frameFromJson(params);
    const int intervalMs = static_cast<int>(RemoteJson::toUInt(params, "interval_ms", 60000));
    if (intervalMs < 1)
    {
        throw RemoteError(RemoteError::InvalidParams, "'interval_ms' must be at least 1");
    }
    const int count = params.contains("count") ? static_cast<int>(RemoteJson::toUInt(params, "count", 1000000)) : -1;
    if (count == 0)
    {
        throw RemoteError(RemoteError::InvalidParams, "'count' must be at least 1; omit it to send until stopped");
    }

    transmit(msg);   // first frame right away; also validates TX permission and state

    const int handle = _nextPeriodicHandle++;
    PeriodicTask task;
    task.msg = msg;
    task.remaining = (count < 0) ? -1 : count - 1;
    task.sentCount = 1;

    if (task.remaining != 0)
    {
        task.timer = new QTimer(this);
        task.timer->setTimerType(Qt::PreciseTimer);
        connect(task.timer, &QTimer::timeout, this, [this, handle]()
        {
            auto it = _periodic.find(handle);
            if (it == _periodic.end()) { return; }

            PeriodicTask &t = it->second;
            try
            {
                transmit(t.msg);
                ++t.sentCount;
            }
            catch (const RemoteError &e)
            {
                log_warning(tr("Remote: periodic task %1 stopped: %2").arg(handle).arg(e.message()));
                t.remaining = 1;
            }

            if (t.remaining > 0 && --t.remaining == 0)
            {
                t.timer->stop();
                t.timer->deleteLater();
                _periodic.erase(it);
            }
        });
        task.timer->start(intervalMs);
        _periodic.emplace(handle, task);
    }

    QJsonObject o;
    o["handle"]      = handle;
    o["interval_ms"] = intervalMs;
    o["count"]       = count;
    o["active"]      = task.remaining != 0;
    return o;
}

QJsonValue RemoteApi::stopPeriodic(const QJsonObject &params)
{
    QJsonObject o;
    if (params.value("handle").toString() == "all")
    {
        o["stopped"] = static_cast<int>(_periodic.size());
        stopAllPeriodic();
        return o;
    }

    const int handle = static_cast<int>(RemoteJson::toUInt(params, "handle", 0x7FFFFFFF));
    auto it = _periodic.find(handle);
    if (it == _periodic.end())
    {
        throw RemoteError(RemoteError::NotFound, QString("periodic task %1 is not active").arg(handle));
    }
    o["stopped"]    = 1;
    o["sent_count"] = it->second.sentCount;
    it->second.timer->stop();
    it->second.timer->deleteLater();
    _periodic.erase(it);
    return o;
}

QJsonValue RemoteApi::getFrames(const QJsonObject &params)
{
    const RemoteFrameFilter filter = RemoteFrameFilter::fromJson(params);
    const uint64_t since = params.contains("since")
                               ? RemoteJson::toUInt(params, "since", 0xFFFFFFFF)
                               : _frameLog.oldestSeq();
    const int limit = static_cast<int>(RemoteJson::toUInt(params, "limit", k_maxFramesPerRead, 100));

    uint64_t next = 0;
    bool dropped = false;
    const QList<RemoteFrame> frames = _frameLog.read(since, limit, filter, &next, &dropped);

    QJsonArray arr;
    for (const RemoteFrame &f : frames) { arr.append(RemoteJson::frameToJson(f)); }

    QJsonObject o;
    o["frames"]  = arr;
    o["count"]   = static_cast<int>(frames.size());
    o["next"]    = static_cast<qint64>(next);
    o["more"]    = next < _frameLog.nextSeq();
    o["dropped"] = dropped;
    o["oldest"]  = static_cast<qint64>(_frameLog.oldestSeq());
    return o;
}

QJsonValue RemoteApi::frameSummary(const QJsonObject &params)
{
    const RemoteFrameFilter filter = RemoteFrameFilter::fromJson(params);
    uint64_t since = params.contains("since")
                         ? RemoteJson::toUInt(params, "since", 0xFFFFFFFF)
                         : _frameLog.oldestSeq();
    const uint64_t until = params.contains("until")
                               ? RemoteJson::toUInt(params, "until", 0xFFFFFFFF)
                               : _frameLog.nextSeq();

    struct Stats
    {
        int count = 0;
        int64_t firstUs = 0;
        int64_t lastUs = 0;
        int64_t minPeriodUs = 0;
        int64_t maxPeriodUs = 0;
        QString lastData;
        int len = 0;
        bool fd = false;
    };
    // key: interface, dir (0 = rx), extended, id
    std::map<std::tuple<int, int, bool, uint32_t>, Stats> stats;
    int total = 0;
    int64_t spanFirst = 0, spanLast = 0;

    bool dropped = false;
    bool firstRead = true;
    while (since < until)
    {
        uint64_t next = 0;
        bool chunkDropped = false;
        const QList<RemoteFrame> frames = _frameLog.read(since, 10000, filter, &next, &chunkDropped);
        if (firstRead) { dropped = chunkDropped; firstRead = false; }

        for (const RemoteFrame &f : frames)
        {
            if (f.seq >= until) { next = until; break; }

            const BusMessage &m = f.msg;
            const int64_t t = m.getTimestamp_us();
            Stats &s = stats[{ m.getInterfaceId(), m.isRX() ? 0 : 1, m.isExtended(), m.getId() }];
            if (s.count > 0)
            {
                const int64_t period = t - s.lastUs;
                s.minPeriodUs = (s.count == 1) ? period : std::min(s.minPeriodUs, period);
                s.maxPeriodUs = (s.count == 1) ? period : std::max(s.maxPeriodUs, period);
            }
            else
            {
                s.firstUs = t;
            }
            s.lastUs = t;
            s.count++;
            s.lastData = RemoteJson::dataToHex(m);
            s.len = m.getLength();
            s.fd = m.isFD();

            if (total == 0) { spanFirst = t; }
            spanLast = t;
            total++;
        }

        if (next <= since) { break; }
        since = next;
    }

    QJsonArray arr;
    for (const auto &[key, s] : stats)
    {
        const auto &[intf, dir, ext, id] = key;
        QJsonObject o;
        o["interface"] = intf;
        o["dir"]       = dir == 0 ? "rx" : "tx";
        o["id"]        = static_cast<qint64>(id);
        o["id_hex"]    = QString("0x%1").arg(id, ext ? 8 : 3, 16, QChar('0')).toUpper().replace("0X", "0x");
        o["extended"]  = ext;
        o["fd"]        = s.fd;
        o["count"]     = s.count;
        o["len"]       = s.len;
        o["last_data"] = s.lastData;
        if (s.count > 1)
        {
            o["period_avg_ms"] = std::round(static_cast<double>(s.lastUs - s.firstUs) / (s.count - 1) / 10.0) / 100.0;
            o["period_min_ms"] = s.minPeriodUs / 1000.0;
            o["period_max_ms"] = s.maxPeriodUs / 1000.0;
        }
        arr.append(o);
    }

    QJsonObject result;
    result["messages"]    = arr;
    result["total"]       = total;
    result["span_ms"]     = (spanLast - spanFirst) / 1000.0;
    result["dropped"]     = dropped;
    result["until"]       = static_cast<qint64>(until);
    return result;
}

QJsonValue RemoteApi::clearTrace(const QJsonObject &)
{
    _backend.clearTrace();
    _frameLog.clear();

    QJsonObject o;
    o["cursor"] = static_cast<qint64>(_frameLog.nextSeq());
    return o;
}

QJsonValue RemoteApi::saveTrace(const QJsonObject &params)
{
    const QString path = RemoteJson::toString(params, "path");

    std::optional<TraceFileFormat> format;
    if (params.contains("format"))
    {
        format = traceFormatFromName(RemoteJson::toString(params, "format"));
    }
    else
    {
        format = traceFormatFromPath(path);
    }
    if (!format)
    {
        throw RemoteError(RemoteError::InvalidParams,
                          QString("unknown trace format; use one of %1").arg(supportedTraceFormatNames().join(", ")));
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        throw RemoteError(RemoteError::IoError, QString("cannot open '%1': %2").arg(path, file.errorString()));
    }
    _backend.getTrace()->save(file, *format);
    file.close();
    if (file.error() != QFileDevice::NoError)
    {
        throw RemoteError(RemoteError::IoError, QString("error writing '%1': %2").arg(path, file.errorString()));
    }

    QJsonObject o;
    o["path"]   = QFileInfo(path).absoluteFilePath();
    o["format"] = traceFormatName(*format);
    o["frames"] = static_cast<qint64>(_backend.getTrace()->size());
    return o;
}

QJsonValue RemoteApi::loadDbc(const QJsonObject &params)
{
    const QString path = QFileInfo(RemoteJson::toString(params, "path")).absoluteFilePath();
    MeasurementSetup &setup = _backend.getSetup();

    MeasurementNetwork *network = nullptr;
    if (params.contains("network"))
    {
        network = setup.getNetworkByName(RemoteJson::toString(params, "network"));
    }
    else if (params.contains("interface"))
    {
        const BusInterfaceId id = requireInterface(params).getId();
        for (MeasurementNetwork *net : setup.getNetworks())
        {
            if (net->getReferencedBusInterfaces().contains(id)) { network = net; break; }
        }
    }
    else if (setup.countNetworks() > 0)
    {
        network = setup.getNetwork(0);
    }
    if (!network)
    {
        throw RemoteError(RemoteError::NotFound, "network not found in the measurement setup");
    }

    QString errorMsg;
    pCanDb db = _backend.loadDbc(path, &errorMsg);
    if (!db)
    {
        throw RemoteError(RemoteError::IoError, QString("cannot load '%1': %2").arg(path, errorMsg));
    }

    setup.beginDatabaseReload();
    network->_canDbs.removeIf([&path](const pCanDb &existing) { return existing->getPath() == path; });
    network->addCanDb(db);
    setup.endDatabaseReload();
    emit _backend.onSetupChanged();
    log_info(tr("Remote: loaded DBC %1 into %2").arg(path, network->name()));

    QJsonObject o;
    o["path"]     = path;
    o["network"]  = network->name();
    o["messages"] = static_cast<int>(db->getMessageList().size());
    return o;
}

QJsonValue RemoteApi::decodeFrame(const QJsonObject &params)
{
    BusMessage msg;
    if (params.contains("seq"))
    {
        const uint64_t seq = RemoteJson::toUInt(params, "seq", 0xFFFFFFFF);
        RemoteFrameFilter any;
        any.includeErrorFrames = true;
        uint64_t next = 0;
        const QList<RemoteFrame> frames = _frameLog.read(seq, 1, any, &next);
        if (frames.isEmpty() || frames.first().seq != seq)
        {
            throw RemoteError(RemoteError::NotFound, QString("frame %1 is no longer in the frame log").arg(seq));
        }
        msg = frames.first().msg;
    }
    else
    {
        msg = RemoteJson::frameFromJson(params);
    }

    const QJsonObject decoded = decodeToJson(msg);
    if (decoded.isEmpty())
    {
        QJsonObject o;
        o["message"] = QJsonValue::Null;
        o["frame"] = RemoteJson::messageToJson(msg);
        return o;
    }
    QJsonObject o = decoded;
    o["frame"] = RemoteJson::messageToJson(msg);
    return o;
}

QJsonValue RemoteApi::encodeFrame(const QJsonObject &params)
{
    MeasurementSetup &setup = _backend.getSetup();
    const QJsonValue key = params.value("message");

    CanDbMessage *dbMsg = nullptr;
    if (key.isString() && !key.toString().startsWith("0x", Qt::CaseInsensitive))
    {
        dbMsg = findDbMessageByName(setup, key.toString());
    }
    else
    {
        BusMessage dummy;
        dummy.setRawId(RemoteJson::toUInt(params, "message", 0xFFFFFFFF));
        dbMsg = _backend.findDbMessage(dummy);
    }
    if (!dbMsg)
    {
        throw RemoteError(RemoteError::NotFound, "message not found in the loaded DBC files");
    }

    BusMessage msg;
    msg.setRawId(dbMsg->getRaw_id());
    msg.setLength(dbMsg->getDlc());
    msg.setRX(false);
    msg.setFD(dbMsg->getDlc() > 8 || RemoteJson::toBool(params, "fd", false));
    msg.setBRS(msg.isFD() && RemoteJson::toBool(params, "brs", false));

    const QJsonObject values = params.value("signals").toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        CanDbSignal *sig = dbMsg->getSignalByName(it.key());
        if (!sig)
        {
            throw RemoteError(RemoteError::InvalidParams, QString("signal '%1' not found in %2").arg(it.key(), dbMsg->getName()));
        }
        if (!it.value().isDouble())
        {
            throw RemoteError(RemoteError::InvalidParams, QString("signal '%1' needs a number").arg(it.key()));
        }
        if (sig->getFactor() == 0.0)
        {
            throw RemoteError(RemoteError::InvalidParams, QString("signal '%1' has factor 0").arg(it.key()));
        }

        // physical -> raw, rounded and clamped to the signal width (same as the Python encode())
        const double rawD = (it.value().toDouble() - sig->getOffset()) / sig->getFactor();
        uint64_t raw = 0;
        const uint8_t len = sig->length();
        if (sig->isUnsigned())
        {
            raw = static_cast<uint64_t>(rawD < 0.0 ? 0.0 : rawD + 0.5);
            const uint64_t maxRaw = (len < 64) ? ((1ULL << len) - 1) : ~0ULL;
            raw = std::min(raw, maxRaw);
        }
        else
        {
            const int64_t minRaw = (len > 0 && len < 64) ? -(1LL << (len - 1)) : INT64_MIN;
            const int64_t maxRaw = (len > 0 && len < 64) ? ((1LL << (len - 1)) - 1) : INT64_MAX;
            raw = static_cast<uint64_t>(std::clamp(static_cast<int64_t>(rawD < 0.0 ? rawD - 0.5 : rawD + 0.5), minRaw, maxRaw));
        }
        msg.injectRawSignal(sig->startBit(), sig->length(), sig->isBigEndian(), raw);
    }

    QJsonObject o;
    o["message"] = dbMsg->getName();
    if (params.contains("interface"))
    {
        msg.setInterfaceId(requireInterface(params).getId());
    }
    if (RemoteJson::toBool(params, "send", false))
    {
        if (!params.contains("interface"))
        {
            throw RemoteError(RemoteError::InvalidParams, "'interface' is required with send=true");
        }
        transmit(msg);
        o["sent"] = true;
    }
    o["frame"] = RemoteJson::messageToJson(msg);
    return o;
}

void RemoteApi::waitForFrame(const QJsonObject &params, Reply reply)
{
    PendingWait wait;
    wait.owner = _currentOwner;
    wait.filter = RemoteFrameFilter::fromJson(params);
    if (!params.contains("dir")) { wait.filter.rx = true; }
    wait.since = params.contains("since")
                     ? RemoteJson::toUInt(params, "since", 0xFFFFFFFF)
                     : _frameLog.nextSeq();
    wait.timeoutMs = timeoutFrom(params, 1000);
    wait.reply = std::move(reply);
    addPending(std::move(wait));
}

void RemoteApi::sendAndWait(const QJsonObject &params, Reply reply)
{
    requireInterface(params);
    const BusMessage msg = RemoteJson::frameFromJson(params);

    if (!params.value("expect").isObject())
    {
        throw RemoteError(RemoteError::InvalidParams, "'expect' must be an object like {\"id\": 0x7E8}");
    }
    const QJsonObject expect = params.value("expect").toObject();

    PendingWait wait;
    wait.owner = _currentOwner;
    wait.filter = RemoteFrameFilter::fromJson(expect);
    if (!wait.filter.interfaceId) { wait.filter.interfaceId = msg.getInterfaceId(); }
    if (!expect.contains("dir")) { wait.filter.rx = true; }
    wait.timeoutMs = timeoutFrom(params, 1000);
    wait.sent = msg;
    wait.sentSince = _frameLog.nextSeq();
    wait.since = wait.sentSince;

    transmit(msg);

    wait.reply = std::move(reply);
    addPending(std::move(wait));
}
