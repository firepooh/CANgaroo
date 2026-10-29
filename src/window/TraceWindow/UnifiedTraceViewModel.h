#pragma once

#include "BaseTraceViewModel.h"
#include "UnifiedTraceItem.h"
#include "decoders/ProtocolManager.h"
#include <memory>
#include <map>
#include <QHash>
#include <QTimer>

class UnifiedTraceViewModel : public BaseTraceViewModel
{
    Q_OBJECT

public:
    enum Category {
        Cat_All,
        Cat_UDS,
        Cat_J1939,
        Cat_XCP,
        Cat_OBD
    };

    UnifiedTraceViewModel(Backend &backend, Category category = Cat_All);
    ~UnifiedTraceViewModel();

    void setCategory(Category category) { m_category = category; }
    void setAggregating(bool aggregating) { m_aggregating = aggregating; }

    virtual QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;
    virtual bool hasChildren(const QModelIndex &parent) const override;

    virtual BusMessage getMessage(const QModelIndex &index) const override;

    virtual QVariant data(const QModelIndex &index, int role) const override;

    // Pause (UDS / J1939 / XCP / OBD2 tabs): stop taking new frames from the trace. The
    // trace keeps recording; resuming processes everything recorded meanwhile in
    // one go. Rebuilds (setup or XCP session change, clear) still run while paused.
    void setPaused(bool paused);
    [[nodiscard]] bool isPaused() const { return m_paused; }
    // Frames recorded since the last processed one (waiting while paused)
    [[nodiscard]] int pendingFrames() const;
    // Frames the trace dropped (size limit) before this view took them, since the pause began
    [[nodiscard]] qint64 framesSkippedWhilePaused() const { return m_skippedWhilePaused; }
    // Erase (GUI_DESIGN.md T-4, T-5): empties this view only. The trace and the other
    // views keep their frames; row numbers, Δt and change highlights start again.
    void erase();

private slots:
    void onTraceDirty();
    void processNewMessages();
    void beforeRemove(int count);
    void afterRemove(int count);
    void beforeClear();
    void afterClear();
    void onSetupChanged();

private:
    std::shared_ptr<UnifiedTraceItem> m_rootItem;
    std::shared_ptr<UnifiedTraceItem> m_aggHeader;
    std::shared_ptr<UnifiedTraceItem> m_udsHeader;
    std::shared_ptr<UnifiedTraceItem> m_j1939Header;
    
    Category m_category;
    ProtocolManager m_protocolManager;
    int m_lastProcessedIndex = -1;
    int m_firstIndex = 0;          // first trace index after erase(); rebuilds start here
    bool m_paused = false;
    qint64 m_skippedWhilePaused = 0;
    uint32_t m_globalIndexCounter = 1;
    uint64_t m_firstTimestamp = 0;
    uint64_t m_previousRowTimestamp = 0;
    int m_maxRows = 10000;

    QTimer m_processTimer;

    bool m_aggregating;
    std::map<uint64_t, std::shared_ptr<UnifiedTraceItem>> m_j1939AggregatedMap;
    std::map<uint64_t, std::shared_ptr<UnifiedTraceItem>> m_udsAggregatedMap;
    std::map<uint64_t, std::shared_ptr<UnifiedTraceItem>> m_xcpAggregatedMap;
    std::map<uint64_t, std::shared_ptr<UnifiedTraceItem>> m_obdAggregatedMap;
    uint64_t getJ1939Key(const ProtocolMessage& pmsg) const;
    uint64_t getUdsKey(const ProtocolMessage& pmsg) const;
    uint64_t getXcpKey(const ProtocolMessage& pmsg) const;
    // OBD2: interface, CAN ID, service and PID (ObdDecoder puts both in the message id)
    uint64_t getObdKey(const ProtocolMessage& pmsg) const { return getUdsKey(pmsg); }

    QHash<uint64_t, uint64_t>   m_prevTimestampByKey;
    QHash<uint64_t, BusMessage> m_prevMessageByKey;
    static uint64_t makeDeltaKey(const BusMessage &msg);
    static uint64_t makeDeltaKey(const ProtocolMessage &pmsg);

    void applyProtocolConfig();

    QVariant data_DisplayRole(const QModelIndex &index, int role) const override;
    QVariant data_TextColorRole(const QModelIndex &index, int role) const override;
    QString formatUnifiedTimestamp(uint64_t ts, uint64_t prevTs) const;
};
