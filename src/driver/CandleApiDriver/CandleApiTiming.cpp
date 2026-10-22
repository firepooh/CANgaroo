#include "CandleApiTiming.h"

CandleApiTiming::CandleApiTiming(uint32_t baseClk, uint32_t bitrate, uint32_t samplePoint, uint32_t brp, uint32_t phase_seg1, uint32_t phase_seg2)
  : _baseClk(baseClk),
    _bitrate(bitrate),
    _samplePoint(samplePoint)
{
    _timing.brp = brp;
    _timing.phase_seg1 = phase_seg1;
    _timing.phase_seg2 = phase_seg2;
    // SJW = phase_seg2, i.e. min(seg1, seg2) as recommended by the Elmue
    // Candlelight 2.5 manual. 1 tq leaves almost no room to resynchronize
    // (6.25 ns in a 5 Mbit/s data phase). setBitTiming() limits it to the
    // device's sjw_max.
    _timing.sjw = phase_seg2;
    _timing.prop_seg = 1;
}

uint32_t CandleApiTiming::getBaseClk() const
{
    return _baseClk;
}

uint32_t CandleApiTiming::getBitrate() const
{
    return _bitrate;
}

uint32_t CandleApiTiming::getSamplePoint() const
{
    return _samplePoint;
}

candle_bittiming_t CandleApiTiming::getTiming() const
{
    return _timing;
}
