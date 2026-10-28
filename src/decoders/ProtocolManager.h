#pragma once

#include "IDecoder.h"
#include <QVector>
#include <memory>
#include <vector>

class XcpDecoder;

struct ProtocolConfig {
    bool enableUds29Bit = true;
};

class ProtocolManager {
public:
    ProtocolManager();
    ~ProtocolManager();

    /**
     * @brief Processes a CAN frame through all registered decoders.
     * @return The status of the decoding process.
     */
    DecodeStatus processFrame(const BusMessage& frame, ProtocolMessage& outMsg);

    void reset();
    
    ProtocolConfig& config() { return m_config; }

private:
    std::shared_ptr<IDecoder> m_udsDecoder;
    std::shared_ptr<IDecoder> m_j1939Decoder;
    std::shared_ptr<IDecoder> m_obdDecoder;
    // One per configured XCP session (XcpSessions); rebuilt when the sessions change
    std::vector<std::unique_ptr<XcpDecoder>> m_xcpDecoders;
    int m_xcpRevision = -1;
    ProtocolConfig m_config;

    void syncXcpSessions();
    uint32_t m_msgCounter = 0;
};
