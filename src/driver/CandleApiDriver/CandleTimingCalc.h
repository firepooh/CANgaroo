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

#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Nominal (classic CAN / CAN FD arbitration) bit timings computed for a CAN
// clock, in the form of the CandleApiTiming table rows:
//   total tq = 1 (sync) + 1 (prop) + phaseSeg1 + phaseSeg2
//   bitrate  = clock / (brp * total tq)
//   sample point = (2 + phaseSeg1) / total tq
struct CandleNominalTiming
{
    uint32_t bitrate = 0;
    uint32_t samplePoint = 0;   // permille
    uint32_t brp = 0;
    uint32_t phaseSeg1 = 0;
    uint32_t phaseSeg2 = 0;
};

namespace CandleTimingCalc
{
// FDCAN nominal limits: TSEG1 (prop + phase_seg1) 1..256, phase_seg2 1..128, BRP 1..512
constexpr uint32_t k_maxTseg1 = 256;
constexpr uint32_t k_maxPhaseSeg2 = 128;
constexpr uint32_t k_maxBrp = 512;
constexpr uint32_t k_minTq = 8;

// The prescaler tried first: the CAN FD data phase rows use BRP 2, and the
// same time quantum in both phases is what CiA 601 recommends.
constexpr uint32_t k_preferredBrp = 2;

// A timing that gives exactly this sample point and the bitrate within 1 bit/s
// (83333 for 83.333k), or nothing if the limits do not allow one.
std::optional<CandleNominalTiming> nominal(uint32_t clock, uint32_t bitrate, uint32_t samplePoint);

// Timings for every bitrate / sample point pair that is not in `existing`
// (pairs of bitrate and sample point), in bitrate order, then sample point.
std::vector<CandleNominalTiming> missing(uint32_t clock,
                                         const std::vector<uint32_t> &bitrates,
                                         const std::vector<uint32_t> &samplePoints,
                                         const std::vector<std::pair<uint32_t, uint32_t>> &existing);
}
