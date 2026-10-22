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

#pragma once

#include <functional>
#include <list>
#include <map>
#include <optional>

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "remote/RemoteError.h"
#include "remote/RemoteFrameLog.h"

class Backend;
class BusInterface;
class MeasurementInterface;

// Implements the remote control methods. Lives on the main thread; every
// Backend / setup access happens there, as for the GUI itself.
class RemoteApi : public QObject
{
    Q_OBJECT

public:
    static constexpr int k_apiVersion = 1;

    // Exactly one of result / error is used. Called on the main thread,
    // possibly long after call() returned (wait_for_frame, send_and_wait).
    using Reply = std::function<void(const QJsonValue &result, const RemoteError *error)>;

    RemoteApi(Backend &backend, bool allowTx, int frameLogCapacity, QObject *parent = nullptr);
    ~RemoteApi() override;

    void call(const QString &method, const QJsonObject &params, Reply reply);

    [[nodiscard]] bool allowTx() const noexcept { return _allowTx; }
    void setAllowTx(bool allowTx) noexcept { _allowTx = allowTx; }

    // Drops pending waits of a client that disconnected.
    void cancelPending(const QObject *owner);

    // Owner of the reply currently being dispatched, set by the server.
    void setCurrentOwner(const QObject *owner) noexcept { _currentOwner = owner; }

private:
    struct PendingWait
    {
        const QObject *owner = nullptr;
        uint64_t since = 0;
        RemoteFrameFilter filter;
        Reply reply;
        QTimer *timer = nullptr;
        int timeoutMs = 0;

        // send_and_wait only: the frame that was sent and where to look for its echo
        std::optional<BusMessage> sent;
        uint64_t sentSince = 0;
    };

    struct PeriodicTask
    {
        QTimer *timer = nullptr;
        BusMessage msg;
        int remaining = -1;   // -1: until stopped
        int sentCount = 0;
    };

    Backend &_backend;
    bool _allowTx;
    RemoteFrameLog _frameLog;
    std::list<PendingWait> _pending;
    std::map<int, PeriodicTask> _periodic;
    int _nextPeriodicHandle = 1;
    const QObject *_currentOwner = nullptr;

    // method handlers (synchronous ones return the result)
    QJsonValue hello(const QJsonObject &params);
    QJsonValue listInterfaces(const QJsonObject &params);
    QJsonValue configureInterface(const QJsonObject &params);
    QJsonValue startMeasurement(const QJsonObject &params);
    QJsonValue stopMeasurement(const QJsonObject &params);
    QJsonValue measurementStatus(const QJsonObject &params);
    QJsonValue busStatus(const QJsonObject &params);
    QJsonValue sendFrame(const QJsonObject &params);
    QJsonValue sendPeriodic(const QJsonObject &params);
    QJsonValue stopPeriodic(const QJsonObject &params);
    QJsonValue getFrames(const QJsonObject &params);
    QJsonValue frameSummary(const QJsonObject &params);
    QJsonValue clearTrace(const QJsonObject &params);
    QJsonValue saveTrace(const QJsonObject &params);
    QJsonValue loadDbc(const QJsonObject &params);
    QJsonValue decodeFrame(const QJsonObject &params);
    QJsonValue encodeFrame(const QJsonObject &params);
    void waitForFrame(const QJsonObject &params, Reply reply);
    void sendAndWait(const QJsonObject &params, Reply reply);

    // helpers
    BusInterface &requireInterface(const QJsonObject &params, const QString &key = "interface");
    MeasurementInterface *findMeasurementInterface(BusInterfaceId id);
    void requireTxAllowed() const;
    void transmit(BusMessage msg);
    void addPending(PendingWait wait);
    void checkPending();
    bool tryComplete(PendingWait &wait);
    void finishPending(std::list<PendingWait>::iterator it, const QJsonValue &result);
    void stopAllPeriodic();
    QJsonObject decodeToJson(const BusMessage &msg);
    QJsonObject interfaceToJson(BusInterfaceId id);
};
