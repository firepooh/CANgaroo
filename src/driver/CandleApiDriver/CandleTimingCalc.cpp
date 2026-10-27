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

#include "CandleTimingCalc.h"

#include <algorithm>
#include <cmath>

namespace
{

std::optional<CandleNominalTiming> withBrp(uint32_t clock, uint32_t bitrate, uint32_t samplePoint, uint32_t brp,
                                           const CandleTimingLimits &limits)
{
    const double tqExact = static_cast<double>(clock) / (static_cast<double>(brp) * bitrate);
    const auto tq = static_cast<uint32_t>(std::lround(tqExact));
    if (tq < CandleTimingCalc::k_minTq) { return std::nullopt; }

    // The bitrate this prescaler really gives must be within 1 bit/s
    const double actual = static_cast<double>(clock) / (static_cast<double>(brp) * tq);
    if (std::fabs(actual - bitrate) >= 1.0) { return std::nullopt; }

    // The sample point must come out exactly: (2 + phase_seg1) / tq == sp / 1000
    if ((tq * samplePoint) % 1000 != 0) { return std::nullopt; }
    const uint32_t beforeSample = tq * samplePoint / 1000;      // sync + prop + phase_seg1
    if (beforeSample < 3 || beforeSample >= tq) { return std::nullopt; }

    CandleNominalTiming t;
    t.bitrate = bitrate;
    t.samplePoint = samplePoint;
    t.brp = brp;
    t.phaseSeg1 = beforeSample - 2;
    t.phaseSeg2 = tq - beforeSample;
    if (1 + t.phaseSeg1 > limits.maxTseg1 || t.phaseSeg2 > limits.maxPhaseSeg2) {
        return std::nullopt;
    }
    return t;
}

} // namespace

std::optional<CandleNominalTiming> CandleTimingCalc::nominal(uint32_t clock, uint32_t bitrate, uint32_t samplePoint,
                                                             const CandleTimingLimits &limits)
{
    if (clock == 0 || bitrate == 0 || samplePoint == 0 || samplePoint >= 1000) { return std::nullopt; }

    if (k_preferredBrp <= limits.maxBrp) {
        if (auto t = withBrp(clock, bitrate, samplePoint, k_preferredBrp, limits)) { return t; }
    }
    for (uint32_t brp = 1; brp <= limits.maxBrp; ++brp) {
        if (brp == k_preferredBrp) { continue; }
        if (auto t = withBrp(clock, bitrate, samplePoint, brp, limits)) { return t; }
    }
    return std::nullopt;
}

std::vector<CandleNominalTiming> CandleTimingCalc::missing(uint32_t clock,
                                                           const std::vector<uint32_t> &bitrates,
                                                           const std::vector<uint32_t> &samplePoints,
                                                           const std::vector<std::pair<uint32_t, uint32_t>> &existing,
                                                           const CandleTimingLimits &limits)
{
    std::vector<CandleNominalTiming> result;
    for (uint32_t bitrate : bitrates) {
        for (uint32_t sp : samplePoints) {
            if (std::find(existing.begin(), existing.end(), std::make_pair(bitrate, sp)) != existing.end()) {
                continue;
            }
            if (auto t = nominal(clock, bitrate, sp, limits)) { result.push_back(*t); }
        }
    }
    return result;
}
